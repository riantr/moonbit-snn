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

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt6Tripod;

struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWEu;

struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt8Dendrite;

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

struct _M0TP26RiantR8snn__mbt6Tripod {
  struct _M0TP26RiantR8snn__mbt13AdExParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $2;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
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
  float $19;
  float $20;
  float $21;
  float $22;
  float $23;
  float $24;
  int32_t $25;
  struct _M0TPB5ArrayGfE* $26;
  struct _M0TPB5ArrayGfE* $27;
  struct _M0TPB5ArrayGfE* $28;
  struct _M0TPB5ArrayGfE* $29;
  struct _M0TPB5ArrayGbE* $30;
  struct _M0TPB5ArrayGfE* $31;
  struct _M0TPB5ArrayGiE* $32;
  struct _M0TPB5ArrayGfE* $33;
  struct _M0TPB5ArrayGfE* $34;
  struct _M0TPB5ArrayGfE* $35;
  struct _M0TPB5ArrayGfE* $36;
  struct _M0TPB5ArrayGfE* $37;
  
};

struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
};

struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS957(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS950(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS945(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS922(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S915(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt12step__tripod(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt18tripod__heun__step(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt6Tripod*
);

int32_t _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt6Tripod*
);

int32_t _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
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

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

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

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

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

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[111]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 110, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS957$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS957
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[87] =
  {
    sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945)
    / 4, 1,
    offsetof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945, $1)
    / 4
    * 2,
    sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950)
    / 4, 1,
    offsetof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt6Tripod) / 4, 31,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $16) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $17) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $18) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $26) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $27) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $28) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $29) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $30) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $31) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $32) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $33) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $34) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $35) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $36) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $37) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite) / 4, 7,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $7) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2383
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS978,
  moonbit_string_t _M0L8filenameS947,
  int32_t _M0L5indexS949
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945* _closure_2406;
  struct _M0TWEu* _M0L13handle__startS945;
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950* _closure_2407;
  struct _M0TWssbEu* _M0L14handle__resultS950;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS957;
  void* _M0L11_2atry__errS972;
  struct moonbit_result_0 _tmp_2409;
  int32_t _handle__error__result_2410;
  int32_t _M0L6_2atmpS2371;
  void* _M0L3errS973;
  moonbit_string_t _M0L4nameS975;
  struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS976;
  moonbit_string_t _M0L7_2anameS977;
  int32_t _M0L6_2acntS2400;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS947);
  _closure_2406
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945*)moonbit_malloc(sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945));
  Moonbit_object_header(_closure_2406)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2406->code
  = &_M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS945;
  _closure_2406->$0 = _M0L5indexS949;
  _closure_2406->$1 = _M0L8filenameS947;
  _M0L13handle__startS945 = (struct _M0TWEu*)_closure_2406;
  moonbit_incref_cycle_free(_M0L8filenameS947);
  _closure_2407
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950*)moonbit_malloc(sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950));
  Moonbit_object_header(_closure_2407)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2407->code
  = &_M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS950;
  _closure_2407->$0 = _M0L5indexS949;
  _closure_2407->$1 = _M0L8filenameS947;
  _M0L14handle__resultS950 = (struct _M0TWssbEu*)_closure_2407;
  _M0L17error__to__stringS957
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS957$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2409
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS978, _M0L8filenameS947, _M0L5indexS949, _M0L13handle__startS945, _M0L14handle__resultS950, _M0L17error__to__stringS957);
  if (_tmp_2409.tag) {
    int32_t const _M0L5_2aokS2380 = _tmp_2409.data.ok;
    _handle__error__result_2410 = _M0L5_2aokS2380;
  } else {
    void* const _M0L6_2aerrS2381 = _tmp_2409.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS957);
    moonbit_decref_cycle_free(_M0L13handle__startS945);
    _M0L11_2atry__errS972 = _M0L6_2aerrS2381;
    goto join_971;
  }
  if (_handle__error__result_2410) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS957);
    moonbit_decref_cycle_free(_M0L13handle__startS945);
    _M0L6_2atmpS2371 = 1;
  } else {
    struct moonbit_result_0 _tmp_2411;
    int32_t _handle__error__result_2412;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2411
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS978, _M0L8filenameS947, _M0L5indexS949, _M0L13handle__startS945, _M0L14handle__resultS950, _M0L17error__to__stringS957);
    if (_tmp_2411.tag) {
      int32_t const _M0L5_2aokS2378 = _tmp_2411.data.ok;
      _handle__error__result_2412 = _M0L5_2aokS2378;
    } else {
      void* const _M0L6_2aerrS2379 = _tmp_2411.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS957);
      moonbit_decref_cycle_free(_M0L13handle__startS945);
      _M0L11_2atry__errS972 = _M0L6_2aerrS2379;
      goto join_971;
    }
    if (_handle__error__result_2412) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS957);
      moonbit_decref_cycle_free(_M0L13handle__startS945);
      _M0L6_2atmpS2371 = 1;
    } else {
      struct moonbit_result_0 _tmp_2413;
      int32_t _handle__error__result_2414;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2413
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS978, _M0L8filenameS947, _M0L5indexS949, _M0L13handle__startS945, _M0L14handle__resultS950, _M0L17error__to__stringS957);
      if (_tmp_2413.tag) {
        int32_t const _M0L5_2aokS2376 = _tmp_2413.data.ok;
        _handle__error__result_2414 = _M0L5_2aokS2376;
      } else {
        void* const _M0L6_2aerrS2377 = _tmp_2413.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS957);
        moonbit_decref_cycle_free(_M0L13handle__startS945);
        _M0L11_2atry__errS972 = _M0L6_2aerrS2377;
        goto join_971;
      }
      if (_handle__error__result_2414) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS957);
        moonbit_decref_cycle_free(_M0L13handle__startS945);
        _M0L6_2atmpS2371 = 1;
      } else {
        struct moonbit_result_0 _tmp_2415;
        int32_t _handle__error__result_2416;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2415
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS978, _M0L8filenameS947, _M0L5indexS949, _M0L13handle__startS945, _M0L14handle__resultS950, _M0L17error__to__stringS957);
        if (_tmp_2415.tag) {
          int32_t const _M0L5_2aokS2374 = _tmp_2415.data.ok;
          _handle__error__result_2416 = _M0L5_2aokS2374;
        } else {
          void* const _M0L6_2aerrS2375 = _tmp_2415.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS957);
          moonbit_decref_cycle_free(_M0L13handle__startS945);
          _M0L11_2atry__errS972 = _M0L6_2aerrS2375;
          goto join_971;
        }
        if (_handle__error__result_2416) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS957);
          moonbit_decref_cycle_free(_M0L13handle__startS945);
          _M0L6_2atmpS2371 = 1;
        } else {
          struct moonbit_result_0 _tmp_2417;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2417
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS978, _M0L8filenameS947, _M0L5indexS949, _M0L13handle__startS945, _M0L14handle__resultS950, _M0L17error__to__stringS957);
          moonbit_decref_cycle_free(_M0L13handle__startS945);
          moonbit_decref_cycle_free(_M0L17error__to__stringS957);
          if (_tmp_2417.tag) {
            int32_t const _M0L5_2aokS2372 = _tmp_2417.data.ok;
            _M0L6_2atmpS2371 = _M0L5_2aokS2372;
          } else {
            void* const _M0L6_2aerrS2373 = _tmp_2417.data.err;
            _M0L11_2atry__errS972 = _M0L6_2aerrS2373;
            goto join_971;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2371) {
    void* _M0L125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2382 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2382)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2382)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS972
    = _M0L125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2382;
    goto join_971;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS950);
  }
  goto joinlet_2408;
  join_971:;
  _M0L3errS973 = _M0L11_2atry__errS972;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS976
  = (struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS973;
  _M0L7_2anameS977 = _M0L36_2aMoonBitTestDriverInternalSkipTestS976->$0;
  _M0L6_2acntS2400
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS976));
  if (_M0L6_2acntS2400 > 1) {
    int32_t _M0L11_2anew__cntS2401 = _M0L6_2acntS2400 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS976), _M0L11_2anew__cntS2401);
    moonbit_incref_cycle_free(_M0L7_2anameS977);
  } else if (_M0L6_2acntS2400 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS976);
  }
  _M0L4nameS975 = _M0L7_2anameS977;
  goto join_974;
  goto joinlet_2418;
  join_974:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS950(_M0L14handle__resultS950, _M0L4nameS975, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS950);
  moonbit_decref_cycle_free(_M0L4nameS975);
  joinlet_2418:;
  joinlet_2408:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS957(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2370,
  void* _M0L3errS958
) {
  void* _M0L1eS960;
  moonbit_string_t _M0L1eS962;
  moonbit_string_t _result_2421;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS958)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS963 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS958;
      moonbit_string_t _M0L4_2aeS964 = _M0L10_2aFailureS963->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS964);
      _M0L1eS962 = _M0L4_2aeS964;
      goto join_961;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS965 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS958;
      moonbit_string_t _M0L4_2aeS966 = _M0L15_2aInspectErrorS965->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS966);
      _M0L1eS962 = _M0L4_2aeS966;
      goto join_961;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS967 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS958;
      moonbit_string_t _M0L4_2aeS968 = _M0L16_2aSnapshotErrorS967->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS968);
      _M0L1eS962 = _M0L4_2aeS968;
      goto join_961;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS969 =
        (struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS958;
      moonbit_string_t _M0L4_2aeS970 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS969->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS970);
      _M0L1eS962 = _M0L4_2aeS970;
      goto join_961;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS958);
      _M0L1eS960 = _M0L3errS958;
      goto join_959;
      break;
    }
  }
  join_961:;
  return _M0L1eS962;
  join_959:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2421 = _M0FP15Error10to__string(_M0L1eS960);
  moonbit_decref_cycle_free(_M0L1eS960);
  return _result_2421;
}

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS950(
  struct _M0TWssbEu* _M0L6_2aenvS2367,
  moonbit_string_t _M0L10__testnameS951,
  moonbit_string_t _M0L7messageS952,
  int32_t _M0L7skippedS953
) {
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950* _M0L14_2acasted__envS2368;
  moonbit_string_t _M0L8filenameS947;
  int32_t _M0L5indexS949;
  moonbit_string_t _M0L10file__nameS954;
  moonbit_string_t _M0L7messageS955;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS956;
  moonbit_string_t _M0L6_2atmpS2369;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2368
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c950*)_M0L6_2aenvS2367;
  _M0L8filenameS947 = _M0L14_2acasted__envS2368->$1;
  _M0L5indexS949 = _M0L14_2acasted__envS2368->$0;
  if (!_M0L7skippedS953 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS954
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS947, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS955
  = _M0MPC16string6String14escape_2einner(_M0L7messageS952, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS956
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS956, _M0L10file__nameS954);
  moonbit_decref_cycle_free(_M0L10file__nameS954);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS956, _M0L5indexS949);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS956, _M0L7messageS955);
  moonbit_decref_cycle_free(_M0L7messageS955);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2369
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS956);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS956);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2369);
  moonbit_decref_cycle_free(_M0L6_2atmpS2369);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS945(
  struct _M0TWEu* _M0L6_2aenvS2364
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945* _M0L14_2acasted__envS2365;
  moonbit_string_t _M0L8filenameS947;
  int32_t _M0L5indexS949;
  moonbit_string_t _M0L10file__nameS946;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS948;
  moonbit_string_t _M0L6_2atmpS2366;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2365
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2ftripod__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c945*)_M0L6_2aenvS2364;
  _M0L8filenameS947 = _M0L14_2acasted__envS2365->$1;
  _M0L5indexS949 = _M0L14_2acasted__envS2365->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS946
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS947, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS948
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS948, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS948, _M0L10file__nameS946);
  moonbit_decref_cycle_free(_M0L10file__nameS946);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS948, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS948, _M0L5indexS949);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS948, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2366
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS948);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS948);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2366);
  moonbit_decref_cycle_free(_M0L6_2atmpS2366);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S915;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS922;
  struct _M0TUsiE** _M0L6_2atmpS2363;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS929;
  moonbit_string_t* _M0L9cli__argsS930;
  moonbit_string_t _M0L6_2atmpS2362;
  moonbit_string_t _M0L6_2atmpS2361;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS931;
  int32_t _M0L7_2abindS932;
  moonbit_string_t* _M0L7_2abindS933;
  int32_t _M0L6_2acntS2402;
  int32_t _M0L2__S934;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S915 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS922 = 0;
  _M0L6_2atmpS2363 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS929
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS929)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS929->$0 = _M0L6_2atmpS2363;
  _M0L16file__and__indexS929->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS930
  = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS930)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2362 = (moonbit_string_t)_M0L9cli__argsS930[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2362);
  moonbit_decref_cycle_free(_M0L9cli__argsS930);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2361
  = _M0MP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2362);
  moonbit_decref_cycle_free(_M0L6_2atmpS2362);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS931
  = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS922(_M0L51moonbit__test__driver__internal__split__mbt__stringS922, _M0L6_2atmpS2361, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2361);
  _M0L7_2abindS932 = _M0L10test__argsS931->$1;
  _M0L7_2abindS933 = _M0L10test__argsS931->$0;
  _M0L6_2acntS2402
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS931));
  if (_M0L6_2acntS2402 > 1) {
    int32_t _M0L11_2anew__cntS2403 = _M0L6_2acntS2402 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS931), _M0L11_2anew__cntS2403);
    moonbit_incref_cycle_free(_M0L7_2abindS933);
  } else if (_M0L6_2acntS2402 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS931);
  }
  _M0L2__S934 = 0;
  while (1) {
    if (_M0L2__S934 < _M0L7_2abindS932) {
      moonbit_string_t _M0L3argS935 =
        (moonbit_string_t)_M0L7_2abindS933[_M0L2__S934];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS936;
      moonbit_string_t _M0L4fileS937;
      moonbit_string_t _M0L5rangeS938;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS939;
      moonbit_string_t _M0L6_2atmpS2359;
      int32_t _M0L5startS940;
      moonbit_string_t _M0L6_2atmpS2358;
      int32_t _M0L3endS941;
      int32_t _M0L1iS942;
      int32_t _M0L6_2atmpS2360;
      moonbit_incref_cycle_free(_M0L3argS935);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS936
      = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS922(_M0L51moonbit__test__driver__internal__split__mbt__stringS922, _M0L3argS935, 58);
      moonbit_decref_cycle_free(_M0L3argS935);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS937
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS936, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS938
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS936, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS936);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS939
      = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS922(_M0L51moonbit__test__driver__internal__split__mbt__stringS922, _M0L5rangeS938, 45);
      moonbit_decref_cycle_free(_M0L5rangeS938);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2359
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS939, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS940
      = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S915(_M0L45moonbit__test__driver__internal__parse__int__S915, _M0L6_2atmpS2359);
      moonbit_decref_cycle_free(_M0L6_2atmpS2359);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2358
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS939, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS939);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS941
      = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S915(_M0L45moonbit__test__driver__internal__parse__int__S915, _M0L6_2atmpS2358);
      moonbit_decref_cycle_free(_M0L6_2atmpS2358);
      _M0L1iS942 = _M0L5startS940;
      while (1) {
        if (_M0L1iS942 < _M0L3endS941) {
          struct _M0TUsiE* _M0L8_2atupleS2356;
          int32_t _M0L6_2atmpS2357;
          moonbit_incref_cycle_free(_M0L4fileS937);
          _M0L8_2atupleS2356
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2356)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2356->$0 = _M0L4fileS937;
          _M0L8_2atupleS2356->$1 = _M0L1iS942;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS929, _M0L8_2atupleS2356);
          _M0L6_2atmpS2357 = _M0L1iS942 + 1;
          _M0L1iS942 = _M0L6_2atmpS2357;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS937);
        }
        break;
      }
      _M0L6_2atmpS2360 = _M0L2__S934 + 1;
      _M0L2__S934 = _M0L6_2atmpS2360;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS933);
    }
    break;
  }
  return _M0L16file__and__indexS929;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS922(
  int32_t _M0L6_2aenvS2337,
  moonbit_string_t _M0L1sS923,
  int32_t _M0L3sepS924
) {
  moonbit_string_t* _M0L6_2atmpS2355;
  struct _M0TPB5ArrayGsE* _M0L3resS925;
  struct _M0TPB8MutLocalGiE* _M0L1iS926;
  struct _M0TPB8MutLocalGiE* _M0L5startS927;
  int32_t _M0L3valS2350;
  int32_t _M0L6_2atmpS2351;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2355 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS925
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS925->$0 = _M0L6_2atmpS2355;
  _M0L3resS925->$1 = 0;
  _M0L1iS926
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS926->$0 = 0;
  _M0L5startS927
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS927->$0 = 0;
  while (1) {
    int32_t _M0L3valS2338 = _M0L1iS926->$0;
    int32_t _M0L6_2atmpS2339 = Moonbit_array_length(_M0L1sS923);
    if (_M0L3valS2338 < _M0L6_2atmpS2339) {
      int32_t _M0L3valS2342 = _M0L1iS926->$0;
      int32_t _M0L6_2atmpS2341;
      int32_t _M0L6_2atmpS2340;
      int32_t _M0L3valS2349;
      int32_t _M0L6_2atmpS2348;
      if (
        _M0L3valS2342 < 0
        || _M0L3valS2342 >= Moonbit_array_length(_M0L1sS923)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2341 = _M0L1sS923[_M0L3valS2342];
      _M0L6_2atmpS2340 = _M0L6_2atmpS2341;
      if (_M0L6_2atmpS2340 == _M0L3sepS924) {
        int32_t _M0L3valS2344 = _M0L5startS927->$0;
        int32_t _M0L3valS2345 = _M0L1iS926->$0;
        moonbit_string_t _M0L6_2atmpS2343;
        int32_t _M0L3valS2347;
        int32_t _M0L6_2atmpS2346;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2343
        = _M0MPC16string6String17unsafe__substring(_M0L1sS923, _M0L3valS2344, _M0L3valS2345);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS925, _M0L6_2atmpS2343);
        _M0L3valS2347 = _M0L1iS926->$0;
        _M0L6_2atmpS2346 = _M0L3valS2347 + 1;
        _M0L5startS927->$0 = _M0L6_2atmpS2346;
      }
      _M0L3valS2349 = _M0L1iS926->$0;
      _M0L6_2atmpS2348 = _M0L3valS2349 + 1;
      _M0L1iS926->$0 = _M0L6_2atmpS2348;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS926);
    }
    break;
  }
  _M0L3valS2350 = _M0L5startS927->$0;
  _M0L6_2atmpS2351 = Moonbit_array_length(_M0L1sS923);
  if (_M0L3valS2350 < _M0L6_2atmpS2351) {
    int32_t _M0L3valS2353 = _M0L5startS927->$0;
    int32_t _M0L6_2atmpS2354;
    moonbit_string_t _M0L6_2atmpS2352;
    moonbit_decref_cycle_free(_M0L5startS927);
    _M0L6_2atmpS2354 = Moonbit_array_length(_M0L1sS923);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2352
    = _M0MPC16string6String17unsafe__substring(_M0L1sS923, _M0L3valS2353, _M0L6_2atmpS2354);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS925, _M0L6_2atmpS2352);
  } else {
    moonbit_decref_cycle_free(_M0L5startS927);
  }
  return _M0L3resS925;
}

int32_t _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S915(
  int32_t _M0L6_2aenvS2330,
  moonbit_string_t _M0L1sS916
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS917;
  int32_t _M0L3lenS918;
  int32_t _M0L7_2abindS919;
  int32_t _M0L1iS920;
  int32_t _result_2426;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS917
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS917)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS917->$0 = 0;
  _M0L3lenS918 = Moonbit_array_length(_M0L1sS916);
  _M0L7_2abindS919 = 0;
  _M0L1iS920 = _M0L7_2abindS919;
  while (1) {
    if (_M0L1iS920 < _M0L3lenS918) {
      int32_t _M0L3valS2335 = _M0L3resS917->$0;
      int32_t _M0L6_2atmpS2332 = _M0L3valS2335 * 10;
      int32_t _M0L6_2atmpS2334;
      int32_t _M0L6_2atmpS2333;
      int32_t _M0L6_2atmpS2331;
      int32_t _M0L6_2atmpS2336;
      if (_M0L1iS920 < 0 || _M0L1iS920 >= Moonbit_array_length(_M0L1sS916)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2334 = _M0L1sS916[_M0L1iS920];
      _M0L6_2atmpS2333 = _M0L6_2atmpS2334 - 48;
      _M0L6_2atmpS2331 = _M0L6_2atmpS2332 + _M0L6_2atmpS2333;
      _M0L3resS917->$0 = _M0L6_2atmpS2331;
      _M0L6_2atmpS2336 = _M0L1iS920 + 1;
      _M0L1iS920 = _M0L6_2atmpS2336;
      continue;
    }
    break;
  }
  _result_2426 = _M0L3resS917->$0;
  moonbit_decref_cycle_free(_M0L3resS917);
  return _result_2426;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples22tripod__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS914
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS914);
  return _M0L4selfS914;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S884,
  moonbit_string_t _M0L12_2adiscard__S885,
  int32_t _M0L12_2adiscard__S886,
  struct _M0TWEu* _M0L12_2adiscard__S887,
  struct _M0TWssbEu* _M0L12_2adiscard__S888,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S889
) {
  struct moonbit_result_0 _result_2427;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2427.tag = 1;
  _result_2427.data.ok = 0;
  return _result_2427;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S890,
  moonbit_string_t _M0L12_2adiscard__S891,
  int32_t _M0L12_2adiscard__S892,
  struct _M0TWEu* _M0L12_2adiscard__S893,
  struct _M0TWssbEu* _M0L12_2adiscard__S894,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S895
) {
  struct moonbit_result_0 _result_2428;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2428.tag = 1;
  _result_2428.data.ok = 0;
  return _result_2428;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S896,
  moonbit_string_t _M0L12_2adiscard__S897,
  int32_t _M0L12_2adiscard__S898,
  struct _M0TWEu* _M0L12_2adiscard__S899,
  struct _M0TWssbEu* _M0L12_2adiscard__S900,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S901
) {
  struct moonbit_result_0 _result_2429;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2429.tag = 1;
  _result_2429.data.ok = 0;
  return _result_2429;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S902,
  moonbit_string_t _M0L12_2adiscard__S903,
  int32_t _M0L12_2adiscard__S904,
  struct _M0TWEu* _M0L12_2adiscard__S905,
  struct _M0TWssbEu* _M0L12_2adiscard__S906,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S907
) {
  struct moonbit_result_0 _result_2430;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2430.tag = 1;
  _result_2430.data.ok = 0;
  return _result_2430;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22tripod__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S908,
  moonbit_string_t _M0L12_2adiscard__S909,
  int32_t _M0L12_2adiscard__S910,
  struct _M0TWEu* _M0L12_2adiscard__S911,
  struct _M0TWssbEu* _M0L12_2adiscard__S912,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S913
) {
  struct moonbit_result_0 _result_2431;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _result_2431.tag = 1;
  _result_2431.data.ok = 0;
  return _result_2431;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S883
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS870;
  float _M0L2glS871;
  float _M0L2tmS872;
  float _M0L1rS873;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_2432;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS870 = 0x1.19p+8f;
  _M0L2glS871 = 0x1.4p+5f;
  _M0L2tmS872 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS873 = 0x1p+0f / 0x1.4p+5f;
  _block_2432
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_2432)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2432->$0 = _M0L1cS870;
  _block_2432->$1 = _M0L2glS871;
  _block_2432->$2 = -0x1.9p+5f;
  _block_2432->$3 = -0x1.1a66666666666p+6f;
  _block_2432->$4 = -0x1.1a66666666666p+6f;
  _block_2432->$5 = _M0L2tmS872;
  _block_2432->$6 = _M0L1rS873;
  _block_2432->$7 = 0x1p+1f;
  _block_2432->$8 = 0x1.2p+7f;
  _block_2432->$9 = 0x1p+2f;
  _block_2432->$10 = 0x1.42p+6f;
  return _block_2432;
}

int32_t _M0FP26RiantR8snn__mbt12step__tripod(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS849,
  float _M0L2dtS860
) {
  int32_t _M0L1nS848;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S850;
  float _M0L2vtS851;
  float _M0L2vrS852;
  float _M0L1bS853;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2329;
  float _M0L2atS854;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2328;
  float _M0L6tau__aS855;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2327;
  float _M0L11tabs__constS856;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2326;
  float _M0L2upS857;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2325;
  float _M0L12ap__membraneS858;
  float _M0L6_2atmpS2324;
  float _M0L6_2atmpS2323;
  int32_t _M0L11tabs__stepsS859;
  int32_t _M0L7_2abindS861;
  int32_t _M0L7_2abindS862;
  int32_t _M0L1iS863;
  int32_t _M0L7_2abindS865;
  int32_t _M0L1kS866;
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS848 = _M0L1pS849->$25;
  _M0L3p__S850 = _M0L1pS849->$0;
  _M0L2vtS851 = _M0L3p__S850->$2;
  _M0L2vrS852 = _M0L3p__S850->$3;
  _M0L1bS853 = _M0L3p__S850->$10;
  _M0L11soma__spikeS2329 = _M0L1pS849->$1;
  _M0L2atS854 = _M0L11soma__spikeS2329->$0;
  _M0L11soma__spikeS2328 = _M0L1pS849->$1;
  _M0L6tau__aS855 = _M0L11soma__spikeS2328->$1;
  _M0L11soma__spikeS2327 = _M0L1pS849->$1;
  _M0L11tabs__constS856 = _M0L11soma__spikeS2327->$3;
  _M0L11soma__spikeS2326 = _M0L1pS849->$1;
  _M0L2upS857 = _M0L11soma__spikeS2326->$4;
  _M0L11soma__spikeS2325 = _M0L1pS849->$1;
  _M0L12ap__membraneS858 = _M0L11soma__spikeS2325->$2;
  _M0L6_2atmpS2324 = _M0L2upS857 + _M0L11tabs__constS856;
  _M0L6_2atmpS2323 = _M0L6_2atmpS2324 / _M0L2dtS860;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L11tabs__stepsS859 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2323);
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(_M0L1pS849, _M0L2dtS860);
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(_M0L1pS849, _M0L2dtS860);
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(_M0L1pS849);
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(_M0L1pS849);
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS849, _M0L2dtS860, 0);
  _M0L7_2abindS861 = 0;
  _M0L7_2abindS862 = _M0L1nS848 * 4;
  _M0L1iS863 = _M0L7_2abindS861;
  while (1) {
    if (_M0L1iS863 < _M0L7_2abindS862) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2184 = _M0L1pS849->$34;
      struct _M0TPB5ArrayGfE* _M0L2dvS2186 = _M0L1pS849->$33;
      float _M0L6_2atmpS2185;
      int32_t _M0L6_2atmpS2187;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2185 = _M0MPC15array5Array2atGfE(_M0L2dvS2186, _M0L1iS863);
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2184, _M0L1iS863, _M0L6_2atmpS2185);
      _M0L6_2atmpS2187 = _M0L1iS863 + 1;
      _M0L1iS863 = _M0L6_2atmpS2187;
      continue;
    }
    break;
  }
  #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS849, _M0L2dtS860, 1);
  _M0L7_2abindS865 = 0;
  _M0L1kS866 = _M0L7_2abindS865;
  while (1) {
    if (_M0L1kS866 < _M0L1nS848) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2189 = _M0L1pS849->$32;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2192 = _M0L1pS849->$32;
      int32_t _M0L6_2atmpS2191;
      int32_t _M0L6_2atmpS2190;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2193;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2201;
      float _M0L6_2atmpS2195;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2200;
      float _M0L6_2atmpS2199;
      float _M0L6_2atmpS2198;
      float _M0L6_2atmpS2197;
      float _M0L6_2atmpS2196;
      float _M0L6_2atmpS2194;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2203;
      int32_t _M0L6_2atmpS2202;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2322;
      float _M0L6_2atmpS2317;
      struct _M0TPB5ArrayGfE* _M0L2dvS2320;
      int32_t _M0L6_2atmpS2321;
      float _M0L6_2atmpS2319;
      float _M0L6_2atmpS2318;
      float _M0L10v__s__predS869;
      struct _M0TPB5ArrayGbE* _M0L4fireS2241;
      int32_t _M0L6_2atmpS2242;
      struct _M0TPB5ArrayGbE* _M0L4fireS2243;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2259;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2271;
      float _M0L6_2atmpS2261;
      float _M0L6_2atmpS2263;
      struct _M0TPB5ArrayGfE* _M0L2dvS2269;
      int32_t _M0L6_2atmpS2270;
      float _M0L6_2atmpS2265;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2267;
      int32_t _M0L6_2atmpS2268;
      float _M0L6_2atmpS2266;
      float _M0L6_2atmpS2264;
      float _M0L6_2atmpS2262;
      float _M0L6_2atmpS2260;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2272;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2286;
      float _M0L6_2atmpS2274;
      float _M0L6_2atmpS2276;
      struct _M0TPB5ArrayGfE* _M0L2dvS2283;
      int32_t _M0L6_2atmpS2285;
      int32_t _M0L6_2atmpS2284;
      float _M0L6_2atmpS2278;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2280;
      int32_t _M0L6_2atmpS2282;
      int32_t _M0L6_2atmpS2281;
      float _M0L6_2atmpS2279;
      float _M0L6_2atmpS2277;
      float _M0L6_2atmpS2275;
      float _M0L6_2atmpS2273;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2287;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2301;
      float _M0L6_2atmpS2289;
      float _M0L6_2atmpS2291;
      struct _M0TPB5ArrayGfE* _M0L2dvS2298;
      int32_t _M0L6_2atmpS2300;
      int32_t _M0L6_2atmpS2299;
      float _M0L6_2atmpS2293;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2295;
      int32_t _M0L6_2atmpS2297;
      int32_t _M0L6_2atmpS2296;
      float _M0L6_2atmpS2294;
      float _M0L6_2atmpS2292;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2288;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2302;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2316;
      float _M0L6_2atmpS2304;
      float _M0L6_2atmpS2306;
      struct _M0TPB5ArrayGfE* _M0L2dvS2313;
      int32_t _M0L6_2atmpS2315;
      int32_t _M0L6_2atmpS2314;
      float _M0L6_2atmpS2308;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2310;
      int32_t _M0L6_2atmpS2312;
      int32_t _M0L6_2atmpS2311;
      float _M0L6_2atmpS2309;
      float _M0L6_2atmpS2307;
      float _M0L6_2atmpS2305;
      float _M0L6_2atmpS2303;
      int32_t _M0L6_2atmpS2188;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2191
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2192, _M0L1kS866);
      _M0L6_2atmpS2190 = _M0L6_2atmpS2191 - 1;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2189, _M0L1kS866, _M0L6_2atmpS2190);
      _M0L9thresholdS2193 = _M0L1pS849->$31;
      _M0L9thresholdS2201 = _M0L1pS849->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2195
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2201, _M0L1kS866);
      _M0L9thresholdS2200 = _M0L1pS849->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2199
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2200, _M0L1kS866);
      _M0L6_2atmpS2198 = _M0L2vtS851 - _M0L6_2atmpS2199;
      _M0L6_2atmpS2197 = _M0L2dtS860 * _M0L6_2atmpS2198;
      _M0L6_2atmpS2196 = _M0L6_2atmpS2197 / _M0L6tau__aS855;
      _M0L6_2atmpS2194 = _M0L6_2atmpS2195 + _M0L6_2atmpS2196;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2193, _M0L1kS866, _M0L6_2atmpS2194);
      _M0L4tabsS2203 = _M0L1pS849->$32;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2202
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2203, _M0L1kS866);
      if (_M0L6_2atmpS2202 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2204 = _M0L1pS849->$26;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2205;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2222;
        float _M0L6_2atmpS2207;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2221;
        float _M0L6_2atmpS2218;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2220;
        float _M0L6_2atmpS2219;
        float _M0L6_2atmpS2217;
        float _M0L6_2atmpS2213;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2216;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2215;
        float _M0L6_2atmpS2214;
        float _M0L6_2atmpS2209;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2212;
        struct _M0TPB5ArrayGfE* _M0L1cS2211;
        float _M0L6_2atmpS2210;
        float _M0L6_2atmpS2208;
        float _M0L6_2atmpS2206;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2223;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2240;
        float _M0L6_2atmpS2225;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2239;
        float _M0L6_2atmpS2236;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2238;
        float _M0L6_2atmpS2237;
        float _M0L6_2atmpS2235;
        float _M0L6_2atmpS2231;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2234;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2233;
        float _M0L6_2atmpS2232;
        float _M0L6_2atmpS2227;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2230;
        struct _M0TPB5ArrayGfE* _M0L1cS2229;
        float _M0L6_2atmpS2228;
        float _M0L6_2atmpS2226;
        float _M0L6_2atmpS2224;
        #line 324 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2204, _M0L1kS866, _M0L2vrS852);
        _M0L5v__d1S2205 = _M0L1pS849->$28;
        _M0L5v__d1S2222 = _M0L1pS849->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2207
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2222, _M0L1kS866);
        _M0L4v__sS2221 = _M0L1pS849->$26;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2218
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2221, _M0L1kS866);
        _M0L5v__d1S2220 = _M0L1pS849->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2219
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2220, _M0L1kS866);
        _M0L6_2atmpS2217 = _M0L6_2atmpS2218 - _M0L6_2atmpS2219;
        _M0L6_2atmpS2213 = _M0L2dtS860 * _M0L6_2atmpS2217;
        _M0L2d1S2216 = _M0L1pS849->$2;
        _M0L3gaxS2215 = _M0L2d1S2216->$3;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2214
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2215, _M0L1kS866);
        _M0L6_2atmpS2209 = _M0L6_2atmpS2213 * _M0L6_2atmpS2214;
        _M0L2d1S2212 = _M0L1pS849->$2;
        _M0L1cS2211 = _M0L2d1S2212->$2;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2210 = _M0MPC15array5Array2atGfE(_M0L1cS2211, _M0L1kS866);
        _M0L6_2atmpS2208 = _M0L6_2atmpS2209 / _M0L6_2atmpS2210;
        _M0L6_2atmpS2206 = _M0L6_2atmpS2207 + _M0L6_2atmpS2208;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2205, _M0L1kS866, _M0L6_2atmpS2206);
        _M0L5v__d2S2223 = _M0L1pS849->$29;
        _M0L5v__d2S2240 = _M0L1pS849->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2225
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2240, _M0L1kS866);
        _M0L4v__sS2239 = _M0L1pS849->$26;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2236
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2239, _M0L1kS866);
        _M0L5v__d2S2238 = _M0L1pS849->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2237
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2238, _M0L1kS866);
        _M0L6_2atmpS2235 = _M0L6_2atmpS2236 - _M0L6_2atmpS2237;
        _M0L6_2atmpS2231 = _M0L2dtS860 * _M0L6_2atmpS2235;
        _M0L2d2S2234 = _M0L1pS849->$3;
        _M0L3gaxS2233 = _M0L2d2S2234->$3;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2232
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2233, _M0L1kS866);
        _M0L6_2atmpS2227 = _M0L6_2atmpS2231 * _M0L6_2atmpS2232;
        _M0L2d2S2230 = _M0L1pS849->$3;
        _M0L1cS2229 = _M0L2d2S2230->$2;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2228 = _M0MPC15array5Array2atGfE(_M0L1cS2229, _M0L1kS866);
        _M0L6_2atmpS2226 = _M0L6_2atmpS2227 / _M0L6_2atmpS2228;
        _M0L6_2atmpS2224 = _M0L6_2atmpS2225 + _M0L6_2atmpS2226;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2223, _M0L1kS866, _M0L6_2atmpS2224);
        goto join_867;
      }
      _M0L4v__sS2322 = _M0L1pS849->$26;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2317
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2322, _M0L1kS866);
      _M0L2dvS2320 = _M0L1pS849->$33;
      _M0L6_2atmpS2321 = _M0L1kS866 * 4;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2319
      = _M0MPC15array5Array2atGfE(_M0L2dvS2320, _M0L6_2atmpS2321);
      _M0L6_2atmpS2318 = _M0L6_2atmpS2319 * _M0L2dtS860;
      _M0L10v__s__predS869 = _M0L6_2atmpS2317 + _M0L6_2atmpS2318;
      _M0L4fireS2241 = _M0L1pS849->$30;
      _M0L6_2atmpS2242 = _M0L10v__s__predS869 >= -0x1.4p+3f;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2241, _M0L1kS866, _M0L6_2atmpS2242);
      _M0L4fireS2243 = _M0L1pS849->$30;
      #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2243, _M0L1kS866)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2244 = _M0L1pS849->$33;
        int32_t _M0L6_2atmpS2245 = _M0L1kS866 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2248 = _M0L1pS849->$26;
        float _M0L6_2atmpS2247;
        float _M0L6_2atmpS2246;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2249;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2250;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2253;
        float _M0L6_2atmpS2252;
        float _M0L6_2atmpS2251;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2254;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2257;
        float _M0L6_2atmpS2256;
        float _M0L6_2atmpS2255;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2258;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2247
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2248, _M0L1kS866);
        _M0L6_2atmpS2246 = _M0L12ap__membraneS858 - _M0L6_2atmpS2247;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2244, _M0L6_2atmpS2245, _M0L6_2atmpS2246);
        _M0L4v__sS2249 = _M0L1pS849->$26;
        #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2249, _M0L1kS866, _M0L12ap__membraneS858);
        _M0L4w__sS2250 = _M0L1pS849->$27;
        _M0L4w__sS2253 = _M0L1pS849->$27;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2252
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2253, _M0L1kS866);
        _M0L6_2atmpS2251 = _M0L6_2atmpS2252 + _M0L1bS853;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2250, _M0L1kS866, _M0L6_2atmpS2251);
        _M0L9thresholdS2254 = _M0L1pS849->$31;
        _M0L9thresholdS2257 = _M0L1pS849->$31;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2256
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2257, _M0L1kS866);
        _M0L6_2atmpS2255 = _M0L6_2atmpS2256 + _M0L2atS854;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2254, _M0L1kS866, _M0L6_2atmpS2255);
        _M0L4tabsS2258 = _M0L1pS849->$32;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2258, _M0L1kS866, _M0L11tabs__stepsS859);
        goto join_867;
      }
      _M0L4v__sS2259 = _M0L1pS849->$26;
      _M0L4v__sS2271 = _M0L1pS849->$26;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2261
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2271, _M0L1kS866);
      _M0L6_2atmpS2263 = 0x1p-1f * _M0L2dtS860;
      _M0L2dvS2269 = _M0L1pS849->$33;
      _M0L6_2atmpS2270 = _M0L1kS866 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2265
      = _M0MPC15array5Array2atGfE(_M0L2dvS2269, _M0L6_2atmpS2270);
      _M0L8dv__tempS2267 = _M0L1pS849->$34;
      _M0L6_2atmpS2268 = _M0L1kS866 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2266
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2267, _M0L6_2atmpS2268);
      _M0L6_2atmpS2264 = _M0L6_2atmpS2265 + _M0L6_2atmpS2266;
      _M0L6_2atmpS2262 = _M0L6_2atmpS2263 * _M0L6_2atmpS2264;
      _M0L6_2atmpS2260 = _M0L6_2atmpS2261 + _M0L6_2atmpS2262;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS2259, _M0L1kS866, _M0L6_2atmpS2260);
      _M0L5v__d1S2272 = _M0L1pS849->$28;
      _M0L5v__d1S2286 = _M0L1pS849->$28;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2274
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2286, _M0L1kS866);
      _M0L6_2atmpS2276 = 0x1p-1f * _M0L2dtS860;
      _M0L2dvS2283 = _M0L1pS849->$33;
      _M0L6_2atmpS2285 = _M0L1kS866 * 4;
      _M0L6_2atmpS2284 = _M0L6_2atmpS2285 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2278
      = _M0MPC15array5Array2atGfE(_M0L2dvS2283, _M0L6_2atmpS2284);
      _M0L8dv__tempS2280 = _M0L1pS849->$34;
      _M0L6_2atmpS2282 = _M0L1kS866 * 4;
      _M0L6_2atmpS2281 = _M0L6_2atmpS2282 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2279
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2280, _M0L6_2atmpS2281);
      _M0L6_2atmpS2277 = _M0L6_2atmpS2278 + _M0L6_2atmpS2279;
      _M0L6_2atmpS2275 = _M0L6_2atmpS2276 * _M0L6_2atmpS2277;
      _M0L6_2atmpS2273 = _M0L6_2atmpS2274 + _M0L6_2atmpS2275;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S2272, _M0L1kS866, _M0L6_2atmpS2273);
      _M0L5v__d2S2287 = _M0L1pS849->$29;
      _M0L5v__d2S2301 = _M0L1pS849->$29;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2289
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2301, _M0L1kS866);
      _M0L6_2atmpS2291 = 0x1p-1f * _M0L2dtS860;
      _M0L2dvS2298 = _M0L1pS849->$33;
      _M0L6_2atmpS2300 = _M0L1kS866 * 4;
      _M0L6_2atmpS2299 = _M0L6_2atmpS2300 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2293
      = _M0MPC15array5Array2atGfE(_M0L2dvS2298, _M0L6_2atmpS2299);
      _M0L8dv__tempS2295 = _M0L1pS849->$34;
      _M0L6_2atmpS2297 = _M0L1kS866 * 4;
      _M0L6_2atmpS2296 = _M0L6_2atmpS2297 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2294
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2295, _M0L6_2atmpS2296);
      _M0L6_2atmpS2292 = _M0L6_2atmpS2293 + _M0L6_2atmpS2294;
      _M0L6_2atmpS2290 = _M0L6_2atmpS2291 * _M0L6_2atmpS2292;
      _M0L6_2atmpS2288 = _M0L6_2atmpS2289 + _M0L6_2atmpS2290;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S2287, _M0L1kS866, _M0L6_2atmpS2288);
      _M0L4w__sS2302 = _M0L1pS849->$27;
      _M0L4w__sS2316 = _M0L1pS849->$27;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2304
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2316, _M0L1kS866);
      _M0L6_2atmpS2306 = 0x1p-1f * _M0L2dtS860;
      _M0L2dvS2313 = _M0L1pS849->$33;
      _M0L6_2atmpS2315 = _M0L1kS866 * 4;
      _M0L6_2atmpS2314 = _M0L6_2atmpS2315 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2308
      = _M0MPC15array5Array2atGfE(_M0L2dvS2313, _M0L6_2atmpS2314);
      _M0L8dv__tempS2310 = _M0L1pS849->$34;
      _M0L6_2atmpS2312 = _M0L1kS866 * 4;
      _M0L6_2atmpS2311 = _M0L6_2atmpS2312 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2309
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2310, _M0L6_2atmpS2311);
      _M0L6_2atmpS2307 = _M0L6_2atmpS2308 + _M0L6_2atmpS2309;
      _M0L6_2atmpS2305 = _M0L6_2atmpS2306 * _M0L6_2atmpS2307;
      _M0L6_2atmpS2303 = _M0L6_2atmpS2304 + _M0L6_2atmpS2305;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS2302, _M0L1kS866, _M0L6_2atmpS2303);
      goto join_867;
      goto joinlet_2435;
      join_867:;
      _M0L6_2atmpS2188 = _M0L1kS866 + 1;
      _M0L1kS866 = _M0L6_2atmpS2188;
      continue;
      joinlet_2435:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18tripod__heun__step(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS825,
  float _M0L2dtS836,
  int32_t _M0L11store__tempS835
) {
  int32_t _M0L1nS824;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S826;
  float _M0L1cS827;
  float _M0L2glS828;
  float _M0L2elS829;
  float _M0L9dt__slopeS830;
  float _M0L2twS831;
  float _M0L1aS832;
  struct _M0TPB8MutLocalGiE* _M0L1kS833;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS824 = _M0L1pS825->$25;
  _M0L3p__S826 = _M0L1pS825->$0;
  _M0L1cS827 = _M0L3p__S826->$0;
  _M0L2glS828 = _M0L3p__S826->$1;
  _M0L2elS829 = _M0L3p__S826->$4;
  _M0L9dt__slopeS830 = _M0L3p__S826->$7;
  _M0L2twS831 = _M0L3p__S826->$8;
  _M0L1aS832 = _M0L3p__S826->$9;
  _M0L1kS833
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS833)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS833->$0 = 0;
  while (1) {
    int32_t _M0L3valS2016 = _M0L1kS833->$0;
    if (_M0L3valS2016 < _M0L1nS824) {
      float _M0L2dsS834;
      float _M0L3dd1S837;
      float _M0L3dd2S838;
      float _M0L2dwS839;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2164;
      int32_t _M0L3valS2165;
      float _M0L6_2atmpS2163;
      float _M0L6_2atmpS2159;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2161;
      int32_t _M0L3valS2162;
      float _M0L6_2atmpS2160;
      float _M0L6_2atmpS2158;
      float _M0L6_2atmpS2157;
      float _M0L6_2atmpS2152;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2156;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2154;
      int32_t _M0L3valS2155;
      float _M0L6_2atmpS2153;
      float _M0L3ic1S840;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2150;
      int32_t _M0L3valS2151;
      float _M0L6_2atmpS2149;
      float _M0L6_2atmpS2145;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2147;
      int32_t _M0L3valS2148;
      float _M0L6_2atmpS2146;
      float _M0L6_2atmpS2144;
      float _M0L6_2atmpS2143;
      float _M0L6_2atmpS2138;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2142;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2140;
      int32_t _M0L3valS2141;
      float _M0L6_2atmpS2139;
      float _M0L3ic2S841;
      float _M0L9exp__termS842;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2126;
      int32_t _M0L3valS2127;
      float _M0L6_2atmpS2125;
      float _M0L6_2atmpS2124;
      float _M0L6_2atmpS2123;
      float _M0L6_2atmpS2122;
      float _M0L6_2atmpS2118;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2120;
      int32_t _M0L3valS2121;
      float _M0L6_2atmpS2119;
      float _M0L6_2atmpS2117;
      float _M0L6_2atmpS2113;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2115;
      int32_t _M0L3valS2116;
      float _M0L6_2atmpS2114;
      float _M0L6_2atmpS2111;
      float _M0L6_2atmpS2112;
      float _M0L6_2atmpS2107;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2109;
      int32_t _M0L3valS2110;
      float _M0L6_2atmpS2108;
      float _M0L6_2atmpS2106;
      float _M0L10dv__s__valS843;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2104;
      int32_t _M0L3valS2105;
      float _M0L6_2atmpS2103;
      float _M0L6_2atmpS2102;
      float _M0L6_2atmpS2097;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2101;
      struct _M0TPB5ArrayGfE* _M0L2gmS2099;
      int32_t _M0L3valS2100;
      float _M0L6_2atmpS2098;
      float _M0L6_2atmpS2093;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2095;
      int32_t _M0L3valS2096;
      float _M0L6_2atmpS2094;
      float _M0L6_2atmpS2092;
      float _M0L6_2atmpS2088;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2090;
      int32_t _M0L3valS2091;
      float _M0L6_2atmpS2089;
      float _M0L6_2atmpS2083;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2087;
      struct _M0TPB5ArrayGfE* _M0L1cS2085;
      int32_t _M0L3valS2086;
      float _M0L6_2atmpS2084;
      float _M0L11dv__d1__valS844;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2081;
      int32_t _M0L3valS2082;
      float _M0L6_2atmpS2080;
      float _M0L6_2atmpS2079;
      float _M0L6_2atmpS2074;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2078;
      struct _M0TPB5ArrayGfE* _M0L2gmS2076;
      int32_t _M0L3valS2077;
      float _M0L6_2atmpS2075;
      float _M0L6_2atmpS2070;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2072;
      int32_t _M0L3valS2073;
      float _M0L6_2atmpS2071;
      float _M0L6_2atmpS2069;
      float _M0L6_2atmpS2065;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2067;
      int32_t _M0L3valS2068;
      float _M0L6_2atmpS2066;
      float _M0L6_2atmpS2060;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2064;
      struct _M0TPB5ArrayGfE* _M0L1cS2062;
      int32_t _M0L3valS2063;
      float _M0L6_2atmpS2061;
      float _M0L11dv__d2__valS845;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2058;
      int32_t _M0L3valS2059;
      float _M0L6_2atmpS2057;
      float _M0L6_2atmpS2056;
      float _M0L6_2atmpS2055;
      float _M0L6_2atmpS2050;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2053;
      int32_t _M0L3valS2054;
      float _M0L6_2atmpS2052;
      float _M0L6_2atmpS2051;
      float _M0L6_2atmpS2049;
      float _M0L7dw__valS846;
      int32_t _M0L3valS2048;
      int32_t _M0L6_2atmpS2047;
      if (_M0L11store__tempS835) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2181 = _M0L1pS825->$34;
        int32_t _M0L3valS2183 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2182 = _M0L3valS2183 * 4;
        float _M0L6_2atmpS2180;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2180
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2181, _M0L6_2atmpS2182);
        _M0L2dsS834 = _M0L6_2atmpS2180 * _M0L2dtS836;
      } else {
        _M0L2dsS834 = 0x0p+0f;
      }
      if (_M0L11store__tempS835) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2176 = _M0L1pS825->$34;
        int32_t _M0L3valS2179 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2178 = _M0L3valS2179 * 4;
        int32_t _M0L6_2atmpS2177 = _M0L6_2atmpS2178 + 1;
        float _M0L6_2atmpS2175;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2175
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2176, _M0L6_2atmpS2177);
        _M0L3dd1S837 = _M0L6_2atmpS2175 * _M0L2dtS836;
      } else {
        _M0L3dd1S837 = 0x0p+0f;
      }
      if (_M0L11store__tempS835) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2171 = _M0L1pS825->$34;
        int32_t _M0L3valS2174 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2173 = _M0L3valS2174 * 4;
        int32_t _M0L6_2atmpS2172 = _M0L6_2atmpS2173 + 2;
        float _M0L6_2atmpS2170;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2170
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2171, _M0L6_2atmpS2172);
        _M0L3dd2S838 = _M0L6_2atmpS2170 * _M0L2dtS836;
      } else {
        _M0L3dd2S838 = 0x0p+0f;
      }
      if (_M0L11store__tempS835) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2166 = _M0L1pS825->$34;
        int32_t _M0L3valS2169 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2168 = _M0L3valS2169 * 4;
        int32_t _M0L6_2atmpS2167 = _M0L6_2atmpS2168 + 3;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L2dwS839
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2166, _M0L6_2atmpS2167);
      } else {
        _M0L2dwS839 = 0x0p+0f;
      }
      _M0L5v__d1S2164 = _M0L1pS825->$28;
      _M0L3valS2165 = _M0L1kS833->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2163
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2164, _M0L3valS2165);
      _M0L6_2atmpS2159 = _M0L6_2atmpS2163 + _M0L3dd1S837;
      _M0L4v__sS2161 = _M0L1pS825->$26;
      _M0L3valS2162 = _M0L1kS833->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2160
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2161, _M0L3valS2162);
      _M0L6_2atmpS2158 = _M0L6_2atmpS2159 - _M0L6_2atmpS2160;
      _M0L6_2atmpS2157 = _M0L6_2atmpS2158 - _M0L2dsS834;
      _M0L6_2atmpS2152 = -_M0L6_2atmpS2157;
      _M0L2d1S2156 = _M0L1pS825->$2;
      _M0L3gaxS2154 = _M0L2d1S2156->$3;
      _M0L3valS2155 = _M0L1kS833->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2153
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2154, _M0L3valS2155);
      _M0L3ic1S840 = _M0L6_2atmpS2152 * _M0L6_2atmpS2153;
      _M0L5v__d2S2150 = _M0L1pS825->$29;
      _M0L3valS2151 = _M0L1kS833->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2149
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2150, _M0L3valS2151);
      _M0L6_2atmpS2145 = _M0L6_2atmpS2149 + _M0L3dd2S838;
      _M0L4v__sS2147 = _M0L1pS825->$26;
      _M0L3valS2148 = _M0L1kS833->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2146
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2147, _M0L3valS2148);
      _M0L6_2atmpS2144 = _M0L6_2atmpS2145 - _M0L6_2atmpS2146;
      _M0L6_2atmpS2143 = _M0L6_2atmpS2144 - _M0L2dsS834;
      _M0L6_2atmpS2138 = -_M0L6_2atmpS2143;
      _M0L2d2S2142 = _M0L1pS825->$3;
      _M0L3gaxS2140 = _M0L2d2S2142->$3;
      _M0L3valS2141 = _M0L1kS833->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2139
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2140, _M0L3valS2141);
      _M0L3ic2S841 = _M0L6_2atmpS2138 * _M0L6_2atmpS2139;
      if (_M0L9dt__slopeS830 < 0x0p+0f) {
        _M0L9exp__termS842 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2136 = _M0L1pS825->$26;
        int32_t _M0L3valS2137 = _M0L1kS833->$0;
        float _M0L6_2atmpS2135;
        float _M0L6_2atmpS2131;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2133;
        int32_t _M0L3valS2134;
        float _M0L6_2atmpS2132;
        float _M0L6_2atmpS2130;
        float _M0L6_2atmpS2129;
        float _M0L6_2atmpS2128;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2135
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2136, _M0L3valS2137);
        _M0L6_2atmpS2131 = _M0L6_2atmpS2135 + _M0L2dsS834;
        _M0L9thresholdS2133 = _M0L1pS825->$31;
        _M0L3valS2134 = _M0L1kS833->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2132
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2133, _M0L3valS2134);
        _M0L6_2atmpS2130 = _M0L6_2atmpS2131 - _M0L6_2atmpS2132;
        _M0L6_2atmpS2129 = _M0L6_2atmpS2130 / _M0L9dt__slopeS830;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2128 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2129);
        _M0L9exp__termS842 = _M0L9dt__slopeS830 * _M0L6_2atmpS2128;
      }
      _M0L4v__sS2126 = _M0L1pS825->$26;
      _M0L3valS2127 = _M0L1kS833->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2125
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2126, _M0L3valS2127);
      _M0L6_2atmpS2124 = _M0L2elS829 - _M0L6_2atmpS2125;
      _M0L6_2atmpS2123 = _M0L6_2atmpS2124 - _M0L2dsS834;
      _M0L6_2atmpS2122 = _M0L2glS828 * _M0L6_2atmpS2123;
      _M0L6_2atmpS2118 = _M0L6_2atmpS2122 + _M0L9exp__termS842;
      _M0L4w__sS2120 = _M0L1pS825->$27;
      _M0L3valS2121 = _M0L1kS833->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2119
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2120, _M0L3valS2121);
      _M0L6_2atmpS2117 = _M0L6_2atmpS2118 - _M0L6_2atmpS2119;
      _M0L6_2atmpS2113 = _M0L6_2atmpS2117 - _M0L2dwS839;
      _M0L12syn__curr__sS2115 = _M0L1pS825->$35;
      _M0L3valS2116 = _M0L1kS833->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2114
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2115, _M0L3valS2116);
      _M0L6_2atmpS2111 = _M0L6_2atmpS2113 - _M0L6_2atmpS2114;
      _M0L6_2atmpS2112 = _M0L3ic1S840 + _M0L3ic2S841;
      _M0L6_2atmpS2107 = _M0L6_2atmpS2111 - _M0L6_2atmpS2112;
      _M0L4i__sS2109 = _M0L1pS825->$4;
      _M0L3valS2110 = _M0L1kS833->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2108
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2109, _M0L3valS2110);
      _M0L6_2atmpS2106 = _M0L6_2atmpS2107 + _M0L6_2atmpS2108;
      _M0L10dv__s__valS843 = _M0L6_2atmpS2106 / _M0L1cS827;
      _M0L5v__d1S2104 = _M0L1pS825->$28;
      _M0L3valS2105 = _M0L1kS833->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2103
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2104, _M0L3valS2105);
      _M0L6_2atmpS2102 = _M0L2elS829 - _M0L6_2atmpS2103;
      _M0L6_2atmpS2097 = _M0L6_2atmpS2102 - _M0L3dd1S837;
      _M0L2d1S2101 = _M0L1pS825->$2;
      _M0L2gmS2099 = _M0L2d1S2101->$4;
      _M0L3valS2100 = _M0L1kS833->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2098
      = _M0MPC15array5Array2atGfE(_M0L2gmS2099, _M0L3valS2100);
      _M0L6_2atmpS2093 = _M0L6_2atmpS2097 * _M0L6_2atmpS2098;
      _M0L13syn__curr__d1S2095 = _M0L1pS825->$36;
      _M0L3valS2096 = _M0L1kS833->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2094
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2095, _M0L3valS2096);
      _M0L6_2atmpS2092 = _M0L6_2atmpS2093 - _M0L6_2atmpS2094;
      _M0L6_2atmpS2088 = _M0L6_2atmpS2092 + _M0L3ic1S840;
      _M0L5i__d1S2090 = _M0L1pS825->$5;
      _M0L3valS2091 = _M0L1kS833->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2089
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2090, _M0L3valS2091);
      _M0L6_2atmpS2083 = _M0L6_2atmpS2088 + _M0L6_2atmpS2089;
      _M0L2d1S2087 = _M0L1pS825->$2;
      _M0L1cS2085 = _M0L2d1S2087->$2;
      _M0L3valS2086 = _M0L1kS833->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2084
      = _M0MPC15array5Array2atGfE(_M0L1cS2085, _M0L3valS2086);
      _M0L11dv__d1__valS844 = _M0L6_2atmpS2083 / _M0L6_2atmpS2084;
      _M0L5v__d2S2081 = _M0L1pS825->$29;
      _M0L3valS2082 = _M0L1kS833->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2080
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2081, _M0L3valS2082);
      _M0L6_2atmpS2079 = _M0L2elS829 - _M0L6_2atmpS2080;
      _M0L6_2atmpS2074 = _M0L6_2atmpS2079 - _M0L3dd2S838;
      _M0L2d2S2078 = _M0L1pS825->$3;
      _M0L2gmS2076 = _M0L2d2S2078->$4;
      _M0L3valS2077 = _M0L1kS833->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2075
      = _M0MPC15array5Array2atGfE(_M0L2gmS2076, _M0L3valS2077);
      _M0L6_2atmpS2070 = _M0L6_2atmpS2074 * _M0L6_2atmpS2075;
      _M0L13syn__curr__d2S2072 = _M0L1pS825->$37;
      _M0L3valS2073 = _M0L1kS833->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2071
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2072, _M0L3valS2073);
      _M0L6_2atmpS2069 = _M0L6_2atmpS2070 - _M0L6_2atmpS2071;
      _M0L6_2atmpS2065 = _M0L6_2atmpS2069 + _M0L3ic2S841;
      _M0L5i__d2S2067 = _M0L1pS825->$6;
      _M0L3valS2068 = _M0L1kS833->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2066
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2067, _M0L3valS2068);
      _M0L6_2atmpS2060 = _M0L6_2atmpS2065 + _M0L6_2atmpS2066;
      _M0L2d2S2064 = _M0L1pS825->$3;
      _M0L1cS2062 = _M0L2d2S2064->$2;
      _M0L3valS2063 = _M0L1kS833->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2061
      = _M0MPC15array5Array2atGfE(_M0L1cS2062, _M0L3valS2063);
      _M0L11dv__d2__valS845 = _M0L6_2atmpS2060 / _M0L6_2atmpS2061;
      _M0L4v__sS2058 = _M0L1pS825->$26;
      _M0L3valS2059 = _M0L1kS833->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2057
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2058, _M0L3valS2059);
      _M0L6_2atmpS2056 = _M0L6_2atmpS2057 + _M0L2dsS834;
      _M0L6_2atmpS2055 = _M0L6_2atmpS2056 - _M0L2elS829;
      _M0L6_2atmpS2050 = _M0L1aS832 * _M0L6_2atmpS2055;
      _M0L4w__sS2053 = _M0L1pS825->$27;
      _M0L3valS2054 = _M0L1kS833->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2052
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2053, _M0L3valS2054);
      _M0L6_2atmpS2051 = _M0L6_2atmpS2052 + _M0L2dwS839;
      _M0L6_2atmpS2049 = _M0L6_2atmpS2050 - _M0L6_2atmpS2051;
      _M0L7dw__valS846 = _M0L6_2atmpS2049 / _M0L2twS831;
      if (_M0L11store__tempS835) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2017 = _M0L1pS825->$34;
        int32_t _M0L3valS2019 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2018 = _M0L3valS2019 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2020;
        int32_t _M0L3valS2023;
        int32_t _M0L6_2atmpS2022;
        int32_t _M0L6_2atmpS2021;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2024;
        int32_t _M0L3valS2027;
        int32_t _M0L6_2atmpS2026;
        int32_t _M0L6_2atmpS2025;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2028;
        int32_t _M0L3valS2031;
        int32_t _M0L6_2atmpS2030;
        int32_t _M0L6_2atmpS2029;
        #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2017, _M0L6_2atmpS2018, _M0L10dv__s__valS843);
        _M0L8dv__tempS2020 = _M0L1pS825->$34;
        _M0L3valS2023 = _M0L1kS833->$0;
        _M0L6_2atmpS2022 = _M0L3valS2023 * 4;
        _M0L6_2atmpS2021 = _M0L6_2atmpS2022 + 1;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2020, _M0L6_2atmpS2021, _M0L11dv__d1__valS844);
        _M0L8dv__tempS2024 = _M0L1pS825->$34;
        _M0L3valS2027 = _M0L1kS833->$0;
        _M0L6_2atmpS2026 = _M0L3valS2027 * 4;
        _M0L6_2atmpS2025 = _M0L6_2atmpS2026 + 2;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2024, _M0L6_2atmpS2025, _M0L11dv__d2__valS845);
        _M0L8dv__tempS2028 = _M0L1pS825->$34;
        _M0L3valS2031 = _M0L1kS833->$0;
        _M0L6_2atmpS2030 = _M0L3valS2031 * 4;
        _M0L6_2atmpS2029 = _M0L6_2atmpS2030 + 3;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2028, _M0L6_2atmpS2029, _M0L7dw__valS846);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2032 = _M0L1pS825->$33;
        int32_t _M0L3valS2034 = _M0L1kS833->$0;
        int32_t _M0L6_2atmpS2033 = _M0L3valS2034 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2035;
        int32_t _M0L3valS2038;
        int32_t _M0L6_2atmpS2037;
        int32_t _M0L6_2atmpS2036;
        struct _M0TPB5ArrayGfE* _M0L2dvS2039;
        int32_t _M0L3valS2042;
        int32_t _M0L6_2atmpS2041;
        int32_t _M0L6_2atmpS2040;
        struct _M0TPB5ArrayGfE* _M0L2dvS2043;
        int32_t _M0L3valS2046;
        int32_t _M0L6_2atmpS2045;
        int32_t _M0L6_2atmpS2044;
        #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2032, _M0L6_2atmpS2033, _M0L10dv__s__valS843);
        _M0L2dvS2035 = _M0L1pS825->$33;
        _M0L3valS2038 = _M0L1kS833->$0;
        _M0L6_2atmpS2037 = _M0L3valS2038 * 4;
        _M0L6_2atmpS2036 = _M0L6_2atmpS2037 + 1;
        #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2035, _M0L6_2atmpS2036, _M0L11dv__d1__valS844);
        _M0L2dvS2039 = _M0L1pS825->$33;
        _M0L3valS2042 = _M0L1kS833->$0;
        _M0L6_2atmpS2041 = _M0L3valS2042 * 4;
        _M0L6_2atmpS2040 = _M0L6_2atmpS2041 + 2;
        #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2039, _M0L6_2atmpS2040, _M0L11dv__d2__valS845);
        _M0L2dvS2043 = _M0L1pS825->$33;
        _M0L3valS2046 = _M0L1kS833->$0;
        _M0L6_2atmpS2045 = _M0L3valS2046 * 4;
        _M0L6_2atmpS2044 = _M0L6_2atmpS2045 + 3;
        #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2043, _M0L6_2atmpS2044, _M0L7dw__valS846);
      }
      _M0L3valS2048 = _M0L1kS833->$0;
      _M0L6_2atmpS2047 = _M0L3valS2048 + 1;
      _M0L1kS833->$0 = _M0L6_2atmpS2047;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS833);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS820
) {
  int32_t _M0L1nS819;
  int32_t _M0L7_2abindS821;
  int32_t _M0L1iS822;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS819 = _M0L1pS820->$25;
  _M0L7_2abindS821 = 0;
  _M0L1iS822 = _M0L7_2abindS821;
  while (1) {
    if (_M0L1iS822 < _M0L1nS819) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S1975 = _M0L1pS820->$36;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1994 = _M0L1pS820->$9;
      float _M0L6_2atmpS1989;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S1993;
      float _M0L6_2atmpS1991;
      float _M0L4e__eS1992;
      float _M0L6_2atmpS1990;
      float _M0L6_2atmpS1987;
      float _M0L7gsyn__eS1988;
      float _M0L6_2atmpS1977;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1986;
      float _M0L6_2atmpS1981;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S1985;
      float _M0L6_2atmpS1983;
      float _M0L4e__iS1984;
      float _M0L6_2atmpS1982;
      float _M0L6_2atmpS1979;
      float _M0L7gsyn__iS1980;
      float _M0L6_2atmpS1978;
      float _M0L6_2atmpS1976;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S1995;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2014;
      float _M0L6_2atmpS2009;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2013;
      float _M0L6_2atmpS2011;
      float _M0L4e__eS2012;
      float _M0L6_2atmpS2010;
      float _M0L6_2atmpS2007;
      float _M0L7gsyn__eS2008;
      float _M0L6_2atmpS1997;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2006;
      float _M0L6_2atmpS2001;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2005;
      float _M0L6_2atmpS2003;
      float _M0L4e__iS2004;
      float _M0L6_2atmpS2002;
      float _M0L6_2atmpS1999;
      float _M0L7gsyn__iS2000;
      float _M0L6_2atmpS1998;
      float _M0L6_2atmpS1996;
      int32_t _M0L6_2atmpS2015;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1989
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S1994, _M0L1iS822);
      _M0L5v__d1S1993 = _M0L1pS820->$28;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1991
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S1993, _M0L1iS822);
      _M0L4e__eS1992 = _M0L1pS820->$19;
      _M0L6_2atmpS1990 = _M0L6_2atmpS1991 - _M0L4e__eS1992;
      _M0L6_2atmpS1987 = _M0L6_2atmpS1989 * _M0L6_2atmpS1990;
      _M0L7gsyn__eS1988 = _M0L1pS820->$23;
      _M0L6_2atmpS1977 = _M0L6_2atmpS1987 * _M0L7gsyn__eS1988;
      _M0L6gi__d1S1986 = _M0L1pS820->$10;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1981
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S1986, _M0L1iS822);
      _M0L5v__d1S1985 = _M0L1pS820->$28;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1983
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S1985, _M0L1iS822);
      _M0L4e__iS1984 = _M0L1pS820->$20;
      _M0L6_2atmpS1982 = _M0L6_2atmpS1983 - _M0L4e__iS1984;
      _M0L6_2atmpS1979 = _M0L6_2atmpS1981 * _M0L6_2atmpS1982;
      _M0L7gsyn__iS1980 = _M0L1pS820->$24;
      _M0L6_2atmpS1978 = _M0L6_2atmpS1979 * _M0L7gsyn__iS1980;
      _M0L6_2atmpS1976 = _M0L6_2atmpS1977 + _M0L6_2atmpS1978;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S1975, _M0L1iS822, _M0L6_2atmpS1976);
      _M0L13syn__curr__d2S1995 = _M0L1pS820->$37;
      _M0L6ge__d2S2014 = _M0L1pS820->$11;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2009
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2014, _M0L1iS822);
      _M0L5v__d2S2013 = _M0L1pS820->$29;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2011
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2013, _M0L1iS822);
      _M0L4e__eS2012 = _M0L1pS820->$19;
      _M0L6_2atmpS2010 = _M0L6_2atmpS2011 - _M0L4e__eS2012;
      _M0L6_2atmpS2007 = _M0L6_2atmpS2009 * _M0L6_2atmpS2010;
      _M0L7gsyn__eS2008 = _M0L1pS820->$23;
      _M0L6_2atmpS1997 = _M0L6_2atmpS2007 * _M0L7gsyn__eS2008;
      _M0L6gi__d2S2006 = _M0L1pS820->$12;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2001
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2006, _M0L1iS822);
      _M0L5v__d2S2005 = _M0L1pS820->$29;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2003
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2005, _M0L1iS822);
      _M0L4e__iS2004 = _M0L1pS820->$20;
      _M0L6_2atmpS2002 = _M0L6_2atmpS2003 - _M0L4e__iS2004;
      _M0L6_2atmpS1999 = _M0L6_2atmpS2001 * _M0L6_2atmpS2002;
      _M0L7gsyn__iS2000 = _M0L1pS820->$24;
      _M0L6_2atmpS1998 = _M0L6_2atmpS1999 * _M0L7gsyn__iS2000;
      _M0L6_2atmpS1996 = _M0L6_2atmpS1997 + _M0L6_2atmpS1998;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S1995, _M0L1iS822, _M0L6_2atmpS1996);
      _M0L6_2atmpS2015 = _M0L1iS822 + 1;
      _M0L1iS822 = _M0L6_2atmpS2015;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS815
) {
  int32_t _M0L1nS814;
  int32_t _M0L7_2abindS816;
  int32_t _M0L1iS817;
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS814 = _M0L1pS815->$25;
  _M0L7_2abindS816 = 0;
  _M0L1iS817 = _M0L7_2abindS816;
  while (1) {
    if (_M0L1iS817 < _M0L1nS814) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1954 = _M0L1pS815->$35;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1973 = _M0L1pS815->$7;
      float _M0L6_2atmpS1968;
      struct _M0TPB5ArrayGfE* _M0L4v__sS1972;
      float _M0L6_2atmpS1970;
      float _M0L4e__eS1971;
      float _M0L6_2atmpS1969;
      float _M0L6_2atmpS1966;
      float _M0L7gsyn__eS1967;
      float _M0L6_2atmpS1956;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1965;
      float _M0L6_2atmpS1960;
      struct _M0TPB5ArrayGfE* _M0L4v__sS1964;
      float _M0L6_2atmpS1962;
      float _M0L4e__iS1963;
      float _M0L6_2atmpS1961;
      float _M0L6_2atmpS1958;
      float _M0L7gsyn__iS1959;
      float _M0L6_2atmpS1957;
      float _M0L6_2atmpS1955;
      int32_t _M0L6_2atmpS1974;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1968
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS1973, _M0L1iS817);
      _M0L4v__sS1972 = _M0L1pS815->$26;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1970
      = _M0MPC15array5Array2atGfE(_M0L4v__sS1972, _M0L1iS817);
      _M0L4e__eS1971 = _M0L1pS815->$19;
      _M0L6_2atmpS1969 = _M0L6_2atmpS1970 - _M0L4e__eS1971;
      _M0L6_2atmpS1966 = _M0L6_2atmpS1968 * _M0L6_2atmpS1969;
      _M0L7gsyn__eS1967 = _M0L1pS815->$23;
      _M0L6_2atmpS1956 = _M0L6_2atmpS1966 * _M0L7gsyn__eS1967;
      _M0L5gi__sS1965 = _M0L1pS815->$8;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1960
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS1965, _M0L1iS817);
      _M0L4v__sS1964 = _M0L1pS815->$26;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1962
      = _M0MPC15array5Array2atGfE(_M0L4v__sS1964, _M0L1iS817);
      _M0L4e__iS1963 = _M0L1pS815->$20;
      _M0L6_2atmpS1961 = _M0L6_2atmpS1962 - _M0L4e__iS1963;
      _M0L6_2atmpS1958 = _M0L6_2atmpS1960 * _M0L6_2atmpS1961;
      _M0L7gsyn__iS1959 = _M0L1pS815->$24;
      _M0L6_2atmpS1957 = _M0L6_2atmpS1958 * _M0L7gsyn__iS1959;
      _M0L6_2atmpS1955 = _M0L6_2atmpS1956 + _M0L6_2atmpS1957;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS1954, _M0L1iS817, _M0L6_2atmpS1955);
      _M0L6_2atmpS1974 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS1974;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS800,
  float _M0L2dtS803
) {
  int32_t _M0L1nS799;
  int32_t _M0L7_2abindS801;
  int32_t _M0L1iS802;
  int32_t _M0L7_2abindS805;
  int32_t _M0L1iS806;
  int32_t _M0L7_2abindS808;
  int32_t _M0L1iS809;
  int32_t _M0L7_2abindS811;
  int32_t _M0L1iS812;
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS799 = _M0L1pS800->$25;
  _M0L7_2abindS801 = 0;
  _M0L1iS802 = _M0L7_2abindS801;
  while (1) {
    if (_M0L1iS802 < _M0L1nS799) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1882 = _M0L1pS800->$9;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1887 = _M0L1pS800->$9;
      float _M0L6_2atmpS1884;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S1886;
      float _M0L6_2atmpS1885;
      float _M0L6_2atmpS1883;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1888;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1893;
      float _M0L6_2atmpS1890;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S1892;
      float _M0L6_2atmpS1891;
      float _M0L6_2atmpS1889;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1894;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1903;
      float _M0L6_2atmpS1896;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S1902;
      float _M0L6_2atmpS1901;
      float _M0L6_2atmpS1899;
      float _M0L6tau__eS1900;
      float _M0L6_2atmpS1898;
      float _M0L6_2atmpS1897;
      float _M0L6_2atmpS1895;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1904;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1913;
      float _M0L6_2atmpS1906;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S1912;
      float _M0L6_2atmpS1911;
      float _M0L6_2atmpS1909;
      float _M0L6tau__iS1910;
      float _M0L6_2atmpS1908;
      float _M0L6_2atmpS1907;
      float _M0L6_2atmpS1905;
      int32_t _M0L6_2atmpS1914;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1884
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S1887, _M0L1iS802);
      _M0L7glu__d1S1886 = _M0L1pS800->$15;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1885
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S1886, _M0L1iS802);
      _M0L6_2atmpS1883 = _M0L6_2atmpS1884 + _M0L6_2atmpS1885;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S1882, _M0L1iS802, _M0L6_2atmpS1883);
      _M0L6gi__d1S1888 = _M0L1pS800->$10;
      _M0L6gi__d1S1893 = _M0L1pS800->$10;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1890
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S1893, _M0L1iS802);
      _M0L8gaba__d1S1892 = _M0L1pS800->$16;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1891
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S1892, _M0L1iS802);
      _M0L6_2atmpS1889 = _M0L6_2atmpS1890 + _M0L6_2atmpS1891;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S1888, _M0L1iS802, _M0L6_2atmpS1889);
      _M0L6ge__d1S1894 = _M0L1pS800->$9;
      _M0L6ge__d1S1903 = _M0L1pS800->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1896
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S1903, _M0L1iS802);
      _M0L6ge__d1S1902 = _M0L1pS800->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1901
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S1902, _M0L1iS802);
      _M0L6_2atmpS1899 = -_M0L6_2atmpS1901;
      _M0L6tau__eS1900 = _M0L1pS800->$21;
      _M0L6_2atmpS1898 = _M0L6_2atmpS1899 / _M0L6tau__eS1900;
      _M0L6_2atmpS1897 = _M0L2dtS803 * _M0L6_2atmpS1898;
      _M0L6_2atmpS1895 = _M0L6_2atmpS1896 + _M0L6_2atmpS1897;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S1894, _M0L1iS802, _M0L6_2atmpS1895);
      _M0L6gi__d1S1904 = _M0L1pS800->$10;
      _M0L6gi__d1S1913 = _M0L1pS800->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1906
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S1913, _M0L1iS802);
      _M0L6gi__d1S1912 = _M0L1pS800->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1911
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S1912, _M0L1iS802);
      _M0L6_2atmpS1909 = -_M0L6_2atmpS1911;
      _M0L6tau__iS1910 = _M0L1pS800->$22;
      _M0L6_2atmpS1908 = _M0L6_2atmpS1909 / _M0L6tau__iS1910;
      _M0L6_2atmpS1907 = _M0L2dtS803 * _M0L6_2atmpS1908;
      _M0L6_2atmpS1905 = _M0L6_2atmpS1906 + _M0L6_2atmpS1907;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S1904, _M0L1iS802, _M0L6_2atmpS1905);
      _M0L6_2atmpS1914 = _M0L1iS802 + 1;
      _M0L1iS802 = _M0L6_2atmpS1914;
      continue;
    }
    break;
  }
  _M0L7_2abindS805 = 0;
  _M0L1iS806 = _M0L7_2abindS805;
  while (1) {
    if (_M0L1iS806 < _M0L1nS799) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S1915 = _M0L1pS800->$15;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S1916;
      int32_t _M0L6_2atmpS1917;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S1915, _M0L1iS806, 0x0p+0f);
      _M0L8gaba__d1S1916 = _M0L1pS800->$16;
      #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S1916, _M0L1iS806, 0x0p+0f);
      _M0L6_2atmpS1917 = _M0L1iS806 + 1;
      _M0L1iS806 = _M0L6_2atmpS1917;
      continue;
    }
    break;
  }
  _M0L7_2abindS808 = 0;
  _M0L1iS809 = _M0L7_2abindS808;
  while (1) {
    if (_M0L1iS809 < _M0L1nS799) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S1918 = _M0L1pS800->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S1923 = _M0L1pS800->$11;
      float _M0L6_2atmpS1920;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S1922;
      float _M0L6_2atmpS1921;
      float _M0L6_2atmpS1919;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S1924;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S1929;
      float _M0L6_2atmpS1926;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S1928;
      float _M0L6_2atmpS1927;
      float _M0L6_2atmpS1925;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S1930;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S1939;
      float _M0L6_2atmpS1932;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S1938;
      float _M0L6_2atmpS1937;
      float _M0L6_2atmpS1935;
      float _M0L6tau__eS1936;
      float _M0L6_2atmpS1934;
      float _M0L6_2atmpS1933;
      float _M0L6_2atmpS1931;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S1940;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S1949;
      float _M0L6_2atmpS1942;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S1948;
      float _M0L6_2atmpS1947;
      float _M0L6_2atmpS1945;
      float _M0L6tau__iS1946;
      float _M0L6_2atmpS1944;
      float _M0L6_2atmpS1943;
      float _M0L6_2atmpS1941;
      int32_t _M0L6_2atmpS1950;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1920
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S1923, _M0L1iS809);
      _M0L7glu__d2S1922 = _M0L1pS800->$17;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1921
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S1922, _M0L1iS809);
      _M0L6_2atmpS1919 = _M0L6_2atmpS1920 + _M0L6_2atmpS1921;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S1918, _M0L1iS809, _M0L6_2atmpS1919);
      _M0L6gi__d2S1924 = _M0L1pS800->$12;
      _M0L6gi__d2S1929 = _M0L1pS800->$12;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1926
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S1929, _M0L1iS809);
      _M0L8gaba__d2S1928 = _M0L1pS800->$18;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1927
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S1928, _M0L1iS809);
      _M0L6_2atmpS1925 = _M0L6_2atmpS1926 + _M0L6_2atmpS1927;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S1924, _M0L1iS809, _M0L6_2atmpS1925);
      _M0L6ge__d2S1930 = _M0L1pS800->$11;
      _M0L6ge__d2S1939 = _M0L1pS800->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1932
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S1939, _M0L1iS809);
      _M0L6ge__d2S1938 = _M0L1pS800->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1937
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S1938, _M0L1iS809);
      _M0L6_2atmpS1935 = -_M0L6_2atmpS1937;
      _M0L6tau__eS1936 = _M0L1pS800->$21;
      _M0L6_2atmpS1934 = _M0L6_2atmpS1935 / _M0L6tau__eS1936;
      _M0L6_2atmpS1933 = _M0L2dtS803 * _M0L6_2atmpS1934;
      _M0L6_2atmpS1931 = _M0L6_2atmpS1932 + _M0L6_2atmpS1933;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S1930, _M0L1iS809, _M0L6_2atmpS1931);
      _M0L6gi__d2S1940 = _M0L1pS800->$12;
      _M0L6gi__d2S1949 = _M0L1pS800->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1942
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S1949, _M0L1iS809);
      _M0L6gi__d2S1948 = _M0L1pS800->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1947
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S1948, _M0L1iS809);
      _M0L6_2atmpS1945 = -_M0L6_2atmpS1947;
      _M0L6tau__iS1946 = _M0L1pS800->$22;
      _M0L6_2atmpS1944 = _M0L6_2atmpS1945 / _M0L6tau__iS1946;
      _M0L6_2atmpS1943 = _M0L2dtS803 * _M0L6_2atmpS1944;
      _M0L6_2atmpS1941 = _M0L6_2atmpS1942 + _M0L6_2atmpS1943;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S1940, _M0L1iS809, _M0L6_2atmpS1941);
      _M0L6_2atmpS1950 = _M0L1iS809 + 1;
      _M0L1iS809 = _M0L6_2atmpS1950;
      continue;
    }
    break;
  }
  _M0L7_2abindS811 = 0;
  _M0L1iS812 = _M0L7_2abindS811;
  while (1) {
    if (_M0L1iS812 < _M0L1nS799) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S1951 = _M0L1pS800->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S1952;
      int32_t _M0L6_2atmpS1953;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S1951, _M0L1iS812, 0x0p+0f);
      _M0L8gaba__d2S1952 = _M0L1pS800->$18;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S1952, _M0L1iS812, 0x0p+0f);
      _M0L6_2atmpS1953 = _M0L1iS812 + 1;
      _M0L1iS812 = _M0L6_2atmpS1953;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS791,
  float _M0L2dtS794
) {
  int32_t _M0L1nS790;
  int32_t _M0L7_2abindS792;
  int32_t _M0L1iS793;
  int32_t _M0L7_2abindS796;
  int32_t _M0L1iS797;
  #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS790 = _M0L1pS791->$25;
  _M0L7_2abindS792 = 0;
  _M0L1iS793 = _M0L7_2abindS792;
  while (1) {
    if (_M0L1iS793 < _M0L1nS790) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1846 = _M0L1pS791->$7;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1851 = _M0L1pS791->$7;
      float _M0L6_2atmpS1848;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS1850;
      float _M0L6_2atmpS1849;
      float _M0L6_2atmpS1847;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1852;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1857;
      float _M0L6_2atmpS1854;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS1856;
      float _M0L6_2atmpS1855;
      float _M0L6_2atmpS1853;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1858;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1867;
      float _M0L6_2atmpS1860;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS1866;
      float _M0L6_2atmpS1865;
      float _M0L6_2atmpS1863;
      float _M0L6tau__eS1864;
      float _M0L6_2atmpS1862;
      float _M0L6_2atmpS1861;
      float _M0L6_2atmpS1859;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1868;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1877;
      float _M0L6_2atmpS1870;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS1876;
      float _M0L6_2atmpS1875;
      float _M0L6_2atmpS1873;
      float _M0L6tau__iS1874;
      float _M0L6_2atmpS1872;
      float _M0L6_2atmpS1871;
      float _M0L6_2atmpS1869;
      int32_t _M0L6_2atmpS1878;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1848
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS1851, _M0L1iS793);
      _M0L6glu__sS1850 = _M0L1pS791->$13;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1849
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS1850, _M0L1iS793);
      _M0L6_2atmpS1847 = _M0L6_2atmpS1848 + _M0L6_2atmpS1849;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS1846, _M0L1iS793, _M0L6_2atmpS1847);
      _M0L5gi__sS1852 = _M0L1pS791->$8;
      _M0L5gi__sS1857 = _M0L1pS791->$8;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1854
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS1857, _M0L1iS793);
      _M0L7gaba__sS1856 = _M0L1pS791->$14;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1855
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS1856, _M0L1iS793);
      _M0L6_2atmpS1853 = _M0L6_2atmpS1854 + _M0L6_2atmpS1855;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS1852, _M0L1iS793, _M0L6_2atmpS1853);
      _M0L5ge__sS1858 = _M0L1pS791->$7;
      _M0L5ge__sS1867 = _M0L1pS791->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1860
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS1867, _M0L1iS793);
      _M0L5ge__sS1866 = _M0L1pS791->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1865
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS1866, _M0L1iS793);
      _M0L6_2atmpS1863 = -_M0L6_2atmpS1865;
      _M0L6tau__eS1864 = _M0L1pS791->$21;
      _M0L6_2atmpS1862 = _M0L6_2atmpS1863 / _M0L6tau__eS1864;
      _M0L6_2atmpS1861 = _M0L2dtS794 * _M0L6_2atmpS1862;
      _M0L6_2atmpS1859 = _M0L6_2atmpS1860 + _M0L6_2atmpS1861;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS1858, _M0L1iS793, _M0L6_2atmpS1859);
      _M0L5gi__sS1868 = _M0L1pS791->$8;
      _M0L5gi__sS1877 = _M0L1pS791->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1870
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS1877, _M0L1iS793);
      _M0L5gi__sS1876 = _M0L1pS791->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1875
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS1876, _M0L1iS793);
      _M0L6_2atmpS1873 = -_M0L6_2atmpS1875;
      _M0L6tau__iS1874 = _M0L1pS791->$22;
      _M0L6_2atmpS1872 = _M0L6_2atmpS1873 / _M0L6tau__iS1874;
      _M0L6_2atmpS1871 = _M0L2dtS794 * _M0L6_2atmpS1872;
      _M0L6_2atmpS1869 = _M0L6_2atmpS1870 + _M0L6_2atmpS1871;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS1868, _M0L1iS793, _M0L6_2atmpS1869);
      _M0L6_2atmpS1878 = _M0L1iS793 + 1;
      _M0L1iS793 = _M0L6_2atmpS1878;
      continue;
    }
    break;
  }
  _M0L7_2abindS796 = 0;
  _M0L1iS797 = _M0L7_2abindS796;
  while (1) {
    if (_M0L1iS797 < _M0L1nS790) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS1879 = _M0L1pS791->$13;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS1880;
      int32_t _M0L6_2atmpS1881;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS1879, _M0L1iS797, 0x0p+0f);
      _M0L7gaba__sS1880 = _M0L1pS791->$14;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS1880, _M0L1iS797, 0x0p+0f);
      _M0L6_2atmpS1881 = _M0L1iS797 + 1;
      _M0L1iS797 = _M0L6_2atmpS1881;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t _M0L1nS751,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS753,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS756
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS750;
  float _M0L2vtS1844;
  float _M0L2vrS1845;
  float _M0L6spreadS752;
  int32_t _M0L7_2abindS754;
  int32_t _M0L1kS755;
  struct _M0TPB5ArrayGfE* _M0L4w__sS758;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S759;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S760;
  int32_t _M0L7_2abindS761;
  int32_t _M0L1kS762;
  struct _M0TPB5ArrayGbE* _M0L4fireS764;
  float _M0L2vtS1843;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS765;
  struct _M0TPB5ArrayGiE* _M0L4tabsS766;
  struct _M0TPB5ArrayGfE* _M0L4i__sS767;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S768;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S769;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS770;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS771;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S772;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S773;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S774;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S775;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS776;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS777;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S778;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S779;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S780;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S781;
  int32_t _M0L6total4S782;
  struct _M0TPB5ArrayGfE* _M0L2dvS783;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS784;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS785;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S786;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S787;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S788;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S789;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS1842;
  struct _M0TP26RiantR8snn__mbt6Tripod* _block_2447;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4v__sS750 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  _M0L2vtS1844 = _M0L11soma__paramS753->$2;
  _M0L2vrS1845 = _M0L11soma__paramS753->$3;
  _M0L6spreadS752 = _M0L2vtS1844 - _M0L2vrS1845;
  _M0L7_2abindS754 = 0;
  _M0L1kS755 = _M0L7_2abindS754;
  while (1) {
    if (_M0L1kS755 < _M0L1nS751) {
      float _M0L2vrS1829 = _M0L11soma__paramS753->$3;
      float _M0L6_2atmpS1831;
      float _M0L6_2atmpS1830;
      float _M0L6_2atmpS1828;
      int32_t _M0L6_2atmpS1832;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1831 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS756);
      _M0L6_2atmpS1830 = _M0L6_2atmpS1831 * _M0L6spreadS752;
      _M0L6_2atmpS1828 = _M0L2vrS1829 + _M0L6_2atmpS1830;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS750, _M0L1kS755, _M0L6_2atmpS1828);
      _M0L6_2atmpS1832 = _M0L1kS755 + 1;
      _M0L1kS755 = _M0L6_2atmpS1832;
      continue;
    }
    break;
  }
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4w__sS758 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d1S759 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d2S760 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  _M0L7_2abindS761 = 0;
  _M0L1kS762 = _M0L7_2abindS761;
  while (1) {
    if (_M0L1kS762 < _M0L1nS751) {
      float _M0L2vrS1834 = _M0L11soma__paramS753->$3;
      float _M0L6_2atmpS1836;
      float _M0L6_2atmpS1835;
      float _M0L6_2atmpS1833;
      float _M0L2vrS1838;
      float _M0L6_2atmpS1840;
      float _M0L6_2atmpS1839;
      float _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1841;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1836 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS756);
      _M0L6_2atmpS1835 = _M0L6_2atmpS1836 * _M0L6spreadS752;
      _M0L6_2atmpS1833 = _M0L2vrS1834 + _M0L6_2atmpS1835;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S759, _M0L1kS762, _M0L6_2atmpS1833);
      _M0L2vrS1838 = _M0L11soma__paramS753->$3;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS1840 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS756);
      _M0L6_2atmpS1839 = _M0L6_2atmpS1840 * _M0L6spreadS752;
      _M0L6_2atmpS1837 = _M0L2vrS1838 + _M0L6_2atmpS1839;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S760, _M0L1kS762, _M0L6_2atmpS1837);
      _M0L6_2atmpS1841 = _M0L1kS762 + 1;
      _M0L1kS762 = _M0L6_2atmpS1841;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4fireS764 = _M0MPC15array5Array4makeGbE(_M0L1nS751, 0);
  _M0L2vtS1843 = _M0L11soma__paramS753->$2;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L9thresholdS765 = _M0MPC15array5Array4makeGfE(_M0L1nS751, _M0L2vtS1843);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4tabsS766 = _M0MPC15array5Array4makeGiE(_M0L1nS751, 1);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4i__sS767 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d1S768 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d2S769 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5ge__sS770 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5gi__sS771 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d1S772 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d1S773 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d2S774 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d2S775 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6glu__sS776 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7gaba__sS777 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d1S778 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d1S779 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d2S780 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d2S781 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  _M0L6total4S782 = _M0L1nS751 * 4;
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2dvS783 = _M0MPC15array5Array4makeGfE(_M0L6total4S782, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8dv__tempS784 = _M0MPC15array5Array4makeGfE(_M0L6total4S782, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L12syn__curr__sS785 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d1S786 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d2S787 = _M0MPC15array5Array4makeGfE(_M0L1nS751, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d1S788 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS751);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d2S789 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS751);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6_2atmpS1842 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS753);
  _block_2447
  = (struct _M0TP26RiantR8snn__mbt6Tripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt6Tripod));
  Moonbit_object_header(_block_2447)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2447->$0 = _M0L11soma__paramS753;
  _block_2447->$1 = _M0L6_2atmpS1842;
  _block_2447->$2 = _M0L2d1S788;
  _block_2447->$3 = _M0L2d2S789;
  _block_2447->$4 = _M0L4i__sS767;
  _block_2447->$5 = _M0L5i__d1S768;
  _block_2447->$6 = _M0L5i__d2S769;
  _block_2447->$7 = _M0L5ge__sS770;
  _block_2447->$8 = _M0L5gi__sS771;
  _block_2447->$9 = _M0L6ge__d1S772;
  _block_2447->$10 = _M0L6gi__d1S773;
  _block_2447->$11 = _M0L6ge__d2S774;
  _block_2447->$12 = _M0L6gi__d2S775;
  _block_2447->$13 = _M0L6glu__sS776;
  _block_2447->$14 = _M0L7gaba__sS777;
  _block_2447->$15 = _M0L7glu__d1S778;
  _block_2447->$16 = _M0L8gaba__d1S779;
  _block_2447->$17 = _M0L7glu__d2S780;
  _block_2447->$18 = _M0L8gaba__d2S781;
  _block_2447->$19 = 0x0p+0f;
  _block_2447->$20 = -0x1.2cp+6f;
  _block_2447->$21 = 0x1.8p+2f;
  _block_2447->$22 = 0x1p+1f;
  _block_2447->$23 = 0x1p+0f;
  _block_2447->$24 = 0x1p+0f;
  _block_2447->$25 = _M0L1nS751;
  _block_2447->$26 = _M0L4v__sS750;
  _block_2447->$27 = _M0L4w__sS758;
  _block_2447->$28 = _M0L5v__d1S759;
  _block_2447->$29 = _M0L5v__d2S760;
  _block_2447->$30 = _M0L4fireS764;
  _block_2447->$31 = _M0L9thresholdS765;
  _block_2447->$32 = _M0L4tabsS766;
  _block_2447->$33 = _M0L2dvS783;
  _block_2447->$34 = _M0L8dv__tempS784;
  _block_2447->$35 = _M0L12syn__curr__sS785;
  _block_2447->$36 = _M0L13syn__curr__d1S786;
  _block_2447->$37 = _M0L13syn__curr__d2S787;
  return _block_2447;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS743
) {
  struct _M0TPB5ArrayGfE* _M0L2elS742;
  struct _M0TPB5ArrayGfE* _M0L1cS744;
  struct _M0TPB5ArrayGfE* _M0L3gaxS745;
  struct _M0TPB5ArrayGfE* _M0L2gmS746;
  struct _M0TPB5ArrayGfE* _M0L1lS747;
  struct _M0TPB5ArrayGfE* _M0L1dS748;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS749;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_2448;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS742
  = _M0MPC15array5Array4makeGfE(_M0L1nS743, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS744 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS745 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS746 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS747 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS748 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS749 = _M0MPC15array5Array4makeGfE(_M0L1nS743, 0x0p+0f);
  _block_2448
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_2448)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _block_2448->$0 = _M0L1nS743;
  _block_2448->$1 = _M0L2elS742;
  _block_2448->$2 = _M0L1cS744;
  _block_2448->$3 = _M0L3gaxS745;
  _block_2448->$4 = _M0L2gmS746;
  _block_2448->$5 = _M0L1lS747;
  _block_2448->$6 = _M0L1dS748;
  _block_2448->$7 = _M0L11gax__parentS749;
  return _block_2448;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2449;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2449
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2449)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2449->$0 = 0x0p+0f;
  _block_2449->$1 = 0x1.4p+3f;
  _block_2449->$2 = 0x1.4p+3f;
  _block_2449->$3 = 0x1p+0f;
  _block_2449->$4 = 0x1p+0f;
  return _block_2449;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS740
) {
  struct _M0TUmmmmE* _M0L1sS739;
  uint64_t _M0L6_2atmpS1827;
  struct _M0TUmmmmE* _M0L1tS741;
  uint64_t _M0L6_2atmpS1823;
  uint64_t _M0L6_2atmpS1824;
  uint64_t _M0L6_2atmpS1825;
  uint64_t _M0L6_2atmpS1826;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2450;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS739 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS740);
  _M0L6_2atmpS1827 = _M0L1sS739->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS741 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1827);
  _M0L6_2atmpS1823 = _M0L1sS739->$0;
  _M0L6_2atmpS1824 = _M0L1sS739->$1;
  _M0L6_2atmpS1825 = _M0L1sS739->$2;
  moonbit_decref_cycle_free(_M0L1sS739);
  _M0L6_2atmpS1826 = _M0L1tS741->$0;
  moonbit_decref_cycle_free(_M0L1tS741);
  _block_2450
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2450)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2450->$0 = _M0L6_2atmpS1823;
  _block_2450->$1 = _M0L6_2atmpS1824;
  _block_2450->$2 = _M0L6_2atmpS1825;
  _block_2450->$3 = _M0L6_2atmpS1826;
  return _block_2450;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS731) {
  uint64_t _M0L2s1S730;
  uint64_t _M0L2z1S732;
  uint64_t _M0L2s2S733;
  uint64_t _M0L2z2S734;
  uint64_t _M0L2s3S735;
  uint64_t _M0L2z3S736;
  uint64_t _M0L2s4S737;
  uint64_t _M0L2z4S738;
  struct _M0TUmmmmE* _block_2451;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S730 = _M0L4seedS731 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S732 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S730);
  _M0L2s2S733 = _M0L2s1S730 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S734 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S733);
  _M0L2s3S735 = _M0L2s2S733 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S736 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S735);
  _M0L2s4S737 = _M0L2s3S735 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S738 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S737);
  _block_2451 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2451)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2451->$0 = _M0L2z1S732;
  _block_2451->$1 = _M0L2z2S734;
  _block_2451->$2 = _M0L2z3S736;
  _block_2451->$3 = _M0L2z4S738;
  return _block_2451;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS728) {
  uint64_t _M0L6_2atmpS1822;
  uint64_t _M0L6_2atmpS1821;
  uint64_t _M0L1zS727;
  uint64_t _M0L6_2atmpS1820;
  uint64_t _M0L6_2atmpS1819;
  uint64_t _M0L1zS729;
  uint64_t _M0L6_2atmpS1818;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1822 = _M0L1zS728 >> 30;
  _M0L6_2atmpS1821 = _M0L1zS728 ^ _M0L6_2atmpS1822;
  _M0L1zS727 = _M0L6_2atmpS1821 * 13787848793156543929ull;
  _M0L6_2atmpS1820 = _M0L1zS727 >> 27;
  _M0L6_2atmpS1819 = _M0L1zS727 ^ _M0L6_2atmpS1820;
  _M0L1zS729 = _M0L6_2atmpS1819 * 10723151780598845931ull;
  _M0L6_2atmpS1818 = _M0L1zS729 >> 31;
  return _M0L1zS729 ^ _M0L6_2atmpS1818;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS725
) {
  uint32_t _M0L1uS724;
  uint32_t _M0L4bitsS726;
  double _M0L6_2atmpS1817;
  double _M0L6_2atmpS1816;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS724 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS725);
  _M0L4bitsS726 = _M0L1uS724 >> 8;
  _M0L6_2atmpS1817 = (double)_M0L4bitsS726;
  _M0L6_2atmpS1816 = _M0L6_2atmpS1817 * 0x1p-24;
  return (float)_M0L6_2atmpS1816;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS723
) {
  uint64_t _M0L1uS722;
  uint64_t _M0L6_2atmpS1815;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS722 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS723);
  _M0L6_2atmpS1815 = _M0L1uS722 >> 32;
  return (uint32_t)_M0L6_2atmpS1815;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS715
) {
  uint64_t _M0L2s0S714;
  uint64_t _M0L2s1S716;
  uint64_t _M0L2s2S717;
  uint64_t _M0L2s3S718;
  uint64_t _M0L3tmpS719;
  uint64_t _M0L6_2atmpS1814;
  uint64_t _M0L3resS720;
  uint64_t _M0L1tS721;
  uint64_t _M0L6_2atmpS1804;
  uint64_t _M0L6_2atmpS1805;
  uint64_t _M0L2s2S1807;
  uint64_t _M0L6_2atmpS1806;
  uint64_t _M0L2s3S1809;
  uint64_t _M0L6_2atmpS1808;
  uint64_t _M0L2s2S1811;
  uint64_t _M0L6_2atmpS1810;
  uint64_t _M0L2s3S1813;
  uint64_t _M0L6_2atmpS1812;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S714 = _M0L1rS715->$0;
  _M0L2s1S716 = _M0L1rS715->$1;
  _M0L2s2S717 = _M0L1rS715->$2;
  _M0L2s3S718 = _M0L1rS715->$3;
  _M0L3tmpS719 = _M0L2s0S714 + _M0L2s3S718;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1814 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS719, 23);
  _M0L3resS720 = _M0L6_2atmpS1814 + _M0L2s0S714;
  _M0L1tS721 = _M0L2s1S716 << 17;
  _M0L6_2atmpS1804 = _M0L2s2S717 ^ _M0L2s0S714;
  _M0L1rS715->$2 = _M0L6_2atmpS1804;
  _M0L6_2atmpS1805 = _M0L2s3S718 ^ _M0L2s1S716;
  _M0L1rS715->$3 = _M0L6_2atmpS1805;
  _M0L2s2S1807 = _M0L1rS715->$2;
  _M0L6_2atmpS1806 = _M0L2s1S716 ^ _M0L2s2S1807;
  _M0L1rS715->$1 = _M0L6_2atmpS1806;
  _M0L2s3S1809 = _M0L1rS715->$3;
  _M0L6_2atmpS1808 = _M0L2s0S714 ^ _M0L2s3S1809;
  _M0L1rS715->$0 = _M0L6_2atmpS1808;
  _M0L2s2S1811 = _M0L1rS715->$2;
  _M0L6_2atmpS1810 = _M0L2s2S1811 ^ _M0L1tS721;
  _M0L1rS715->$2 = _M0L6_2atmpS1810;
  _M0L2s3S1813 = _M0L1rS715->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1812 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1813, 45);
  _M0L1rS715->$3 = _M0L6_2atmpS1812;
  return _M0L3resS720;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS712, int32_t _M0L1kS713) {
  uint64_t _M0L6_2atmpS1801;
  int32_t _M0L6_2atmpS1803;
  uint64_t _M0L6_2atmpS1802;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1801 = _M0L1xS712 << (_M0L1kS713 & 63);
  _M0L6_2atmpS1803 = 64 - _M0L1kS713;
  _M0L6_2atmpS1802 = _M0L1xS712 >> (_M0L6_2atmpS1803 & 63);
  return _M0L6_2atmpS1801 | _M0L6_2atmpS1802;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS711) {
  double _M0L6_2atmpS1800;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1800 = (double)_M0L4selfS711;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1800);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS710) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS710 != _M0L4selfS710) {
    return 0;
  } else if (_M0L4selfS710 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS710 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS710;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS696,
  float _M0L4elemS698
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS695;
  int32_t _M0L1iS697;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS695 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS696);
  _M0L1iS697 = 0;
  while (1) {
    if (_M0L1iS697 < _M0L3lenS696) {
      float* _M0L3bufS1794 = _M0L3arrS695->$0;
      int32_t _M0L6_2atmpS1795;
      _M0L3bufS1794[_M0L1iS697] = _M0L4elemS698;
      _M0L6_2atmpS1795 = _M0L1iS697 + 1;
      _M0L1iS697 = _M0L6_2atmpS1795;
      continue;
    }
    break;
  }
  return _M0L3arrS695;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS701,
  int32_t _M0L4elemS703
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS700;
  int32_t _M0L1iS702;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS700 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS701);
  _M0L1iS702 = 0;
  while (1) {
    if (_M0L1iS702 < _M0L3lenS701) {
      uint8_t* _M0L3bufS1796 = _M0L3arrS700->$0;
      int32_t _M0L6_2atmpS1797;
      _M0L3bufS1796[_M0L1iS702] = _M0L4elemS703;
      _M0L6_2atmpS1797 = _M0L1iS702 + 1;
      _M0L1iS702 = _M0L6_2atmpS1797;
      continue;
    }
    break;
  }
  return _M0L3arrS700;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS706,
  int32_t _M0L4elemS708
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS705;
  int32_t _M0L1iS707;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS705 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS706);
  _M0L1iS707 = 0;
  while (1) {
    if (_M0L1iS707 < _M0L3lenS706) {
      int32_t* _M0L3bufS1798 = _M0L3arrS705->$0;
      int32_t _M0L6_2atmpS1799;
      _M0L3bufS1798[_M0L1iS707] = _M0L4elemS708;
      _M0L6_2atmpS1799 = _M0L1iS707 + 1;
      _M0L1iS707 = _M0L6_2atmpS1799;
      continue;
    }
    break;
  }
  return _M0L3arrS705;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS684,
  int32_t _M0L5indexS685,
  float _M0L5valueS686
) {
  int32_t _M0L3lenS683;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS683 = _M0L4selfS684->$1;
  if (_M0L5indexS685 >= 0 && _M0L5indexS685 < _M0L3lenS683) {
    float* _M0L6_2atmpS1791;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1791 = _M0MPC15array5Array6bufferGfE(_M0L4selfS684);
    _M0L6_2atmpS1791[_M0L5indexS685] = _M0L5valueS686;
    moonbit_decref_cycle_free(_M0L6_2atmpS1791);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS688,
  int32_t _M0L5indexS689,
  int32_t _M0L5valueS690
) {
  int32_t _M0L3lenS687;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS687 = _M0L4selfS688->$1;
  if (_M0L5indexS689 >= 0 && _M0L5indexS689 < _M0L3lenS687) {
    int32_t* _M0L6_2atmpS1792;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1792 = _M0MPC15array5Array6bufferGiE(_M0L4selfS688);
    _M0L6_2atmpS1792[_M0L5indexS689] = _M0L5valueS690;
    moonbit_decref_cycle_free(_M0L6_2atmpS1792);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS692,
  int32_t _M0L5indexS693,
  int32_t _M0L5valueS694
) {
  int32_t _M0L3lenS691;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS691 = _M0L4selfS692->$1;
  if (_M0L5indexS693 >= 0 && _M0L5indexS693 < _M0L3lenS691) {
    uint8_t* _M0L6_2atmpS1793;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1793 = _M0MPC15array5Array6bufferGbE(_M0L4selfS692);
    _M0L6_2atmpS1793[_M0L5indexS693] = _M0L5valueS694;
    moonbit_decref_cycle_free(_M0L6_2atmpS1793);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS672,
  int32_t _M0L5indexS673
) {
  int32_t _M0L3lenS671;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS671 = _M0L4selfS672->$1;
  if (_M0L5indexS673 >= 0 && _M0L5indexS673 < _M0L3lenS671) {
    uint8_t* _M0L6_2atmpS1787;
    int32_t _result_2455;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1787 = _M0MPC15array5Array6bufferGbE(_M0L4selfS672);
    _result_2455 = (int32_t)_M0L6_2atmpS1787[_M0L5indexS673];
    moonbit_decref_cycle_free(_M0L6_2atmpS1787);
    return _result_2455;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS675,
  int32_t _M0L5indexS676
) {
  int32_t _M0L3lenS674;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS674 = _M0L4selfS675->$1;
  if (_M0L5indexS676 >= 0 && _M0L5indexS676 < _M0L3lenS674) {
    float* _M0L6_2atmpS1788;
    float _result_2456;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1788 = _M0MPC15array5Array6bufferGfE(_M0L4selfS675);
    _result_2456 = (float)_M0L6_2atmpS1788[_M0L5indexS676];
    moonbit_decref_cycle_free(_M0L6_2atmpS1788);
    return _result_2456;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS678,
  int32_t _M0L5indexS679
) {
  int32_t _M0L3lenS677;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS677 = _M0L4selfS678->$1;
  if (_M0L5indexS679 >= 0 && _M0L5indexS679 < _M0L3lenS677) {
    int32_t* _M0L6_2atmpS1789;
    int32_t _result_2457;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1789 = _M0MPC15array5Array6bufferGiE(_M0L4selfS678);
    _result_2457 = (int32_t)_M0L6_2atmpS1789[_M0L5indexS679];
    moonbit_decref_cycle_free(_M0L6_2atmpS1789);
    return _result_2457;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS681,
  int32_t _M0L5indexS682
) {
  int32_t _M0L3lenS680;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS680 = _M0L4selfS681->$1;
  if (_M0L5indexS682 >= 0 && _M0L5indexS682 < _M0L3lenS680) {
    moonbit_string_t* _M0L6_2atmpS1790;
    moonbit_string_t _M0L6_2atmpS2384;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1790 = _M0MPC15array5Array6bufferGsE(_M0L4selfS681);
    _M0L6_2atmpS2384 = (moonbit_string_t)_M0L6_2atmpS1790[_M0L5indexS682];
    moonbit_incref_cycle_free(_M0L6_2atmpS2384);
    moonbit_decref_cycle_free(_M0L6_2atmpS1790);
    return _M0L6_2atmpS2384;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS670) {
  moonbit_string_t _M0L6_2atmpS1786;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1786 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS670);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1786);
  moonbit_decref_cycle_free(_M0L6_2atmpS1786);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS669) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS669);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS654) {
  uint64_t _M0L4bitsS657;
  uint64_t _M0L6_2atmpS1785;
  uint64_t _M0L6_2atmpS1784;
  int32_t _M0L8ieeeSignS658;
  uint64_t _M0L12ieeeMantissaS659;
  uint64_t _M0L6_2atmpS1783;
  uint64_t _M0L6_2atmpS1782;
  int32_t _M0L12ieeeExponentS660;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS661;
  struct _M0TPB17FloatingDecimal64* _M0L1vS662;
  moonbit_string_t _result_2459;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS654 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS654 >= -0x1p+53 && _M0L3valS654 <= 0x1p+53) {
    if (_M0L3valS654 >= -0x1p+31 && _M0L3valS654 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS655;
      double _M0L6_2atmpS1771;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS655 = _M0MPC16double6Double7to__int(_M0L3valS654);
      _M0L6_2atmpS1771 = (double)_M0L1iS655;
      if (_M0L6_2atmpS1771 == _M0L3valS654) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS655, 10);
      }
    } else {
      int64_t _M0L1iS656;
      double _M0L6_2atmpS1772;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS656 = _M0MPC16double6Double9to__int64(_M0L3valS654);
      _M0L6_2atmpS1772 = (double)_M0L1iS656;
      if (_M0L6_2atmpS1772 == _M0L3valS654) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS656, 10);
      }
    }
  }
  _M0L4bitsS657 = *(int64_t*)&_M0L3valS654;
  _M0L6_2atmpS1785 = _M0L4bitsS657 >> 63;
  _M0L6_2atmpS1784 = _M0L6_2atmpS1785 & 1ull;
  _M0L8ieeeSignS658 = _M0L6_2atmpS1784 != 0ull;
  _M0L12ieeeMantissaS659 = _M0L4bitsS657 & 4503599627370495ull;
  _M0L6_2atmpS1783 = _M0L4bitsS657 >> 52;
  _M0L6_2atmpS1782 = _M0L6_2atmpS1783 & 2047ull;
  _M0L12ieeeExponentS660 = (int32_t)_M0L6_2atmpS1782;
  if (
    _M0L12ieeeExponentS660 == 2047
    || _M0L12ieeeExponentS660 == 0 && _M0L12ieeeMantissaS659 == 0ull
  ) {
    int32_t _M0L6_2atmpS1773 = _M0L12ieeeExponentS660 != 0;
    int32_t _M0L6_2atmpS1774 = _M0L12ieeeMantissaS659 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS658, _M0L6_2atmpS1773, _M0L6_2atmpS1774);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS661
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS659, _M0L12ieeeExponentS660);
  if (_M0L7_2abindS661 == 0) {
    uint32_t _M0L6_2atmpS1775;
    if (_M0L7_2abindS661) {
      moonbit_decref_cycle_free(_M0L7_2abindS661);
    }
    _M0L6_2atmpS1775 = *(uint32_t*)&_M0L12ieeeExponentS660;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS662 = _M0FPB3d2d(_M0L12ieeeMantissaS659, _M0L6_2atmpS1775);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS663 = _M0L7_2abindS661;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS664 = _M0L7_2aSomeS663;
    struct _M0TPB17FloatingDecimal64* _M0L1xS665 = _M0L4_2afS664;
    while (1) {
      uint64_t _M0L8mantissaS1781 = _M0L1xS665->$0;
      uint64_t _M0L1qS666 = _M0L8mantissaS1781 / 10ull;
      uint64_t _M0L8mantissaS1779 = _M0L1xS665->$0;
      uint64_t _M0L6_2atmpS1780 = 10ull * _M0L1qS666;
      uint64_t _M0L1rS667 = _M0L8mantissaS1779 - _M0L6_2atmpS1780;
      int32_t _M0L8exponentS1778;
      int32_t _M0L6_2atmpS1777;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1776;
      if (_M0L1rS667 != 0ull) {
        _M0L1vS662 = _M0L1xS665;
        break;
      }
      _M0L8exponentS1778 = _M0L1xS665->$1;
      moonbit_decref_cycle_free(_M0L1xS665);
      _M0L6_2atmpS1777 = _M0L8exponentS1778 + 1;
      _M0L6_2atmpS1776
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1776)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1776->$0 = _M0L1qS666;
      _M0L6_2atmpS1776->$1 = _M0L6_2atmpS1777;
      _M0L1xS665 = _M0L6_2atmpS1776;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2459 = _M0FPB9to__chars(_M0L1vS662, _M0L8ieeeSignS658);
  moonbit_decref_cycle_free(_M0L1vS662);
  return _result_2459;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS649,
  int32_t _M0L12ieeeExponentS651
) {
  uint64_t _M0L2m2S648;
  int32_t _M0L6_2atmpS1770;
  int32_t _M0L2e2S650;
  int32_t _M0L6_2atmpS1769;
  uint64_t _M0L6_2atmpS1768;
  uint64_t _M0L4maskS652;
  uint64_t _M0L8fractionS653;
  int32_t _M0L6_2atmpS1767;
  uint64_t _M0L6_2atmpS1766;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1765;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S648 = 4503599627370496ull | _M0L12ieeeMantissaS649;
  _M0L6_2atmpS1770 = _M0L12ieeeExponentS651 - 1023;
  _M0L2e2S650 = _M0L6_2atmpS1770 - 52;
  if (_M0L2e2S650 > 0) {
    return 0;
  }
  if (_M0L2e2S650 < -52) {
    return 0;
  }
  _M0L6_2atmpS1769 = -_M0L2e2S650;
  _M0L6_2atmpS1768 = 1ull << (_M0L6_2atmpS1769 & 63);
  _M0L4maskS652 = _M0L6_2atmpS1768 - 1ull;
  _M0L8fractionS653 = _M0L2m2S648 & _M0L4maskS652;
  if (_M0L8fractionS653 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1767 = -_M0L2e2S650;
  _M0L6_2atmpS1766 = _M0L2m2S648 >> (_M0L6_2atmpS1767 & 63);
  _M0L6_2atmpS1765
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1765)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1765->$0 = _M0L6_2atmpS1766;
  _M0L6_2atmpS1765->$1 = 0;
  return _M0L6_2atmpS1765;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS616,
  int32_t _M0L4signS614
) {
  moonbit_bytes_t _M0L6resultS612;
  int32_t _M0Lm5indexS613;
  uint64_t _M0L6outputS615;
  int32_t _M0L7olengthS617;
  int32_t _M0L8exponentS1764;
  int32_t _M0L6_2atmpS1763;
  int32_t _M0Lm3expS618;
  int32_t _M0L6_2atmpS1762;
  int32_t _M0L6_2atmpS1760;
  int32_t _M0L18scientificNotationS619;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS612 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS613 = 0;
  if (_M0L4signS614) {
    int32_t _M0L6_2atmpS1634 = _M0Lm5indexS613;
    int32_t _M0L6_2atmpS1635;
    if (
      _M0L6_2atmpS1634 < 0
      || _M0L6_2atmpS1634 >= Moonbit_array_length(_M0L6resultS612)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS612[_M0L6_2atmpS1634] = 45;
    _M0L6_2atmpS1635 = _M0Lm5indexS613;
    _M0Lm5indexS613 = _M0L6_2atmpS1635 + 1;
  }
  _M0L6outputS615 = _M0L1vS616->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS617 = _M0FPB17decimal__length17(_M0L6outputS615);
  _M0L8exponentS1764 = _M0L1vS616->$1;
  _M0L6_2atmpS1763 = _M0L8exponentS1764 + _M0L7olengthS617;
  _M0Lm3expS618 = _M0L6_2atmpS1763 - 1;
  _M0L6_2atmpS1762 = _M0Lm3expS618;
  if (_M0L6_2atmpS1762 >= -6) {
    int32_t _M0L6_2atmpS1761 = _M0Lm3expS618;
    _M0L6_2atmpS1760 = _M0L6_2atmpS1761 < 21;
  } else {
    _M0L6_2atmpS1760 = 0;
  }
  _M0L18scientificNotationS619 = !_M0L6_2atmpS1760;
  if (_M0L18scientificNotationS619) {
    int32_t _M0L7_2abindS620 = _M0L7olengthS617 - 1;
    uint64_t _M0L6outputS621;
    int32_t _M0L1iS622 = 0;
    uint64_t _M0L6outputS623 = _M0L6outputS615;
    int32_t _M0L6_2atmpS1636;
    int32_t _M0L6_2atmpS1640;
    int32_t _M0L6_2atmpS1639;
    int32_t _M0L6_2atmpS1638;
    int32_t _M0L6_2atmpS1637;
    int32_t _M0L6_2atmpS1644;
    int32_t _M0L6_2atmpS1645;
    int32_t _M0L6_2atmpS1646;
    int32_t _M0L6_2atmpS1647;
    int32_t _M0L6_2atmpS1648;
    int32_t _M0L6_2atmpS1654;
    int32_t _M0L6_2atmpS1687;
    moonbit_string_t _result_2461;
    while (1) {
      if (_M0L1iS622 < _M0L7_2abindS620) {
        uint64_t _M0L1cS624 = _M0L6outputS623 % 10ull;
        int32_t _M0L6_2atmpS1693 = _M0Lm5indexS613;
        int32_t _M0L6_2atmpS1692 = _M0L6_2atmpS1693 + _M0L7olengthS617;
        int32_t _M0L6_2atmpS1688 = _M0L6_2atmpS1692 - _M0L1iS622;
        int32_t _M0L6_2atmpS1691 = (int32_t)_M0L1cS624;
        int32_t _M0L6_2atmpS1690 = 48 + _M0L6_2atmpS1691;
        int32_t _M0L6_2atmpS1689 = _M0L6_2atmpS1690 & 0xff;
        int32_t _M0L6_2atmpS1694;
        uint64_t _M0L6_2atmpS1695;
        if (
          _M0L6_2atmpS1688 < 0
          || _M0L6_2atmpS1688 >= Moonbit_array_length(_M0L6resultS612)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS612[_M0L6_2atmpS1688] = _M0L6_2atmpS1689;
        _M0L6_2atmpS1694 = _M0L1iS622 + 1;
        _M0L6_2atmpS1695 = _M0L6outputS623 / 10ull;
        _M0L1iS622 = _M0L6_2atmpS1694;
        _M0L6outputS623 = _M0L6_2atmpS1695;
        continue;
      } else {
        _M0L6outputS621 = _M0L6outputS623;
      }
      break;
    }
    _M0L6_2atmpS1636 = _M0Lm5indexS613;
    _M0L6_2atmpS1640 = (int32_t)_M0L6outputS621;
    _M0L6_2atmpS1639 = _M0L6_2atmpS1640 % 10;
    _M0L6_2atmpS1638 = 48 + _M0L6_2atmpS1639;
    _M0L6_2atmpS1637 = _M0L6_2atmpS1638 & 0xff;
    if (
      _M0L6_2atmpS1636 < 0
      || _M0L6_2atmpS1636 >= Moonbit_array_length(_M0L6resultS612)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS612[_M0L6_2atmpS1636] = _M0L6_2atmpS1637;
    if (_M0L7olengthS617 > 1) {
      int32_t _M0L6_2atmpS1642 = _M0Lm5indexS613;
      int32_t _M0L6_2atmpS1641 = _M0L6_2atmpS1642 + 1;
      if (
        _M0L6_2atmpS1641 < 0
        || _M0L6_2atmpS1641 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1641] = 46;
    } else {
      int32_t _M0L6_2atmpS1643 = _M0Lm5indexS613;
      _M0Lm5indexS613 = _M0L6_2atmpS1643 - 1;
    }
    _M0L6_2atmpS1644 = _M0Lm5indexS613;
    _M0L6_2atmpS1645 = _M0L7olengthS617 + 1;
    _M0Lm5indexS613 = _M0L6_2atmpS1644 + _M0L6_2atmpS1645;
    _M0L6_2atmpS1646 = _M0Lm5indexS613;
    if (
      _M0L6_2atmpS1646 < 0
      || _M0L6_2atmpS1646 >= Moonbit_array_length(_M0L6resultS612)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS612[_M0L6_2atmpS1646] = 101;
    _M0L6_2atmpS1647 = _M0Lm5indexS613;
    _M0Lm5indexS613 = _M0L6_2atmpS1647 + 1;
    _M0L6_2atmpS1648 = _M0Lm3expS618;
    if (_M0L6_2atmpS1648 < 0) {
      int32_t _M0L6_2atmpS1649 = _M0Lm5indexS613;
      int32_t _M0L6_2atmpS1650;
      int32_t _M0L6_2atmpS1651;
      if (
        _M0L6_2atmpS1649 < 0
        || _M0L6_2atmpS1649 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1649] = 45;
      _M0L6_2atmpS1650 = _M0Lm5indexS613;
      _M0Lm5indexS613 = _M0L6_2atmpS1650 + 1;
      _M0L6_2atmpS1651 = _M0Lm3expS618;
      _M0Lm3expS618 = -_M0L6_2atmpS1651;
    } else {
      int32_t _M0L6_2atmpS1652 = _M0Lm5indexS613;
      int32_t _M0L6_2atmpS1653;
      if (
        _M0L6_2atmpS1652 < 0
        || _M0L6_2atmpS1652 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1652] = 43;
      _M0L6_2atmpS1653 = _M0Lm5indexS613;
      _M0Lm5indexS613 = _M0L6_2atmpS1653 + 1;
    }
    _M0L6_2atmpS1654 = _M0Lm3expS618;
    if (_M0L6_2atmpS1654 >= 100) {
      int32_t _M0L6_2atmpS1670 = _M0Lm3expS618;
      int32_t _M0L1aS626 = _M0L6_2atmpS1670 / 100;
      int32_t _M0L6_2atmpS1669 = _M0Lm3expS618;
      int32_t _M0L6_2atmpS1668 = _M0L6_2atmpS1669 / 10;
      int32_t _M0L1bS627 = _M0L6_2atmpS1668 % 10;
      int32_t _M0L6_2atmpS1667 = _M0Lm3expS618;
      int32_t _M0L1cS628 = _M0L6_2atmpS1667 % 10;
      int32_t _M0L6_2atmpS1655 = _M0Lm5indexS613;
      int32_t _M0L6_2atmpS1657 = 48 + _M0L1aS626;
      int32_t _M0L6_2atmpS1656 = _M0L6_2atmpS1657 & 0xff;
      int32_t _M0L6_2atmpS1661;
      int32_t _M0L6_2atmpS1658;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1659;
      int32_t _M0L6_2atmpS1665;
      int32_t _M0L6_2atmpS1662;
      int32_t _M0L6_2atmpS1664;
      int32_t _M0L6_2atmpS1663;
      int32_t _M0L6_2atmpS1666;
      if (
        _M0L6_2atmpS1655 < 0
        || _M0L6_2atmpS1655 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1655] = _M0L6_2atmpS1656;
      _M0L6_2atmpS1661 = _M0Lm5indexS613;
      _M0L6_2atmpS1658 = _M0L6_2atmpS1661 + 1;
      _M0L6_2atmpS1660 = 48 + _M0L1bS627;
      _M0L6_2atmpS1659 = _M0L6_2atmpS1660 & 0xff;
      if (
        _M0L6_2atmpS1658 < 0
        || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1658] = _M0L6_2atmpS1659;
      _M0L6_2atmpS1665 = _M0Lm5indexS613;
      _M0L6_2atmpS1662 = _M0L6_2atmpS1665 + 2;
      _M0L6_2atmpS1664 = 48 + _M0L1cS628;
      _M0L6_2atmpS1663 = _M0L6_2atmpS1664 & 0xff;
      if (
        _M0L6_2atmpS1662 < 0
        || _M0L6_2atmpS1662 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1662] = _M0L6_2atmpS1663;
      _M0L6_2atmpS1666 = _M0Lm5indexS613;
      _M0Lm5indexS613 = _M0L6_2atmpS1666 + 3;
    } else {
      int32_t _M0L6_2atmpS1671 = _M0Lm3expS618;
      if (_M0L6_2atmpS1671 >= 10) {
        int32_t _M0L6_2atmpS1681 = _M0Lm3expS618;
        int32_t _M0L1aS629 = _M0L6_2atmpS1681 / 10;
        int32_t _M0L6_2atmpS1680 = _M0Lm3expS618;
        int32_t _M0L1bS630 = _M0L6_2atmpS1680 % 10;
        int32_t _M0L6_2atmpS1672 = _M0Lm5indexS613;
        int32_t _M0L6_2atmpS1674 = 48 + _M0L1aS629;
        int32_t _M0L6_2atmpS1673 = _M0L6_2atmpS1674 & 0xff;
        int32_t _M0L6_2atmpS1678;
        int32_t _M0L6_2atmpS1675;
        int32_t _M0L6_2atmpS1677;
        int32_t _M0L6_2atmpS1676;
        int32_t _M0L6_2atmpS1679;
        if (
          _M0L6_2atmpS1672 < 0
          || _M0L6_2atmpS1672 >= Moonbit_array_length(_M0L6resultS612)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS612[_M0L6_2atmpS1672] = _M0L6_2atmpS1673;
        _M0L6_2atmpS1678 = _M0Lm5indexS613;
        _M0L6_2atmpS1675 = _M0L6_2atmpS1678 + 1;
        _M0L6_2atmpS1677 = 48 + _M0L1bS630;
        _M0L6_2atmpS1676 = _M0L6_2atmpS1677 & 0xff;
        if (
          _M0L6_2atmpS1675 < 0
          || _M0L6_2atmpS1675 >= Moonbit_array_length(_M0L6resultS612)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS612[_M0L6_2atmpS1675] = _M0L6_2atmpS1676;
        _M0L6_2atmpS1679 = _M0Lm5indexS613;
        _M0Lm5indexS613 = _M0L6_2atmpS1679 + 2;
      } else {
        int32_t _M0L6_2atmpS1682 = _M0Lm5indexS613;
        int32_t _M0L6_2atmpS1685 = _M0Lm3expS618;
        int32_t _M0L6_2atmpS1684 = 48 + _M0L6_2atmpS1685;
        int32_t _M0L6_2atmpS1683 = _M0L6_2atmpS1684 & 0xff;
        int32_t _M0L6_2atmpS1686;
        if (
          _M0L6_2atmpS1682 < 0
          || _M0L6_2atmpS1682 >= Moonbit_array_length(_M0L6resultS612)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS612[_M0L6_2atmpS1682] = _M0L6_2atmpS1683;
        _M0L6_2atmpS1686 = _M0Lm5indexS613;
        _M0Lm5indexS613 = _M0L6_2atmpS1686 + 1;
      }
    }
    _M0L6_2atmpS1687 = _M0Lm5indexS613;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2461
    = _M0FPB19string__from__bytes(_M0L6resultS612, 0, _M0L6_2atmpS1687);
    moonbit_decref_cycle_free(_M0L6resultS612);
    return _result_2461;
  } else {
    int32_t _M0L6_2atmpS1696 = _M0Lm3expS618;
    int32_t _M0L6_2atmpS1759;
    moonbit_string_t _result_2467;
    if (_M0L6_2atmpS1696 < 0) {
      int32_t _M0L6_2atmpS1697 = _M0Lm5indexS613;
      int32_t _M0L6_2atmpS1699;
      int32_t _M0L6_2atmpS1698;
      int32_t _M0L6_2atmpS1700;
      int32_t _M0L1iS631;
      int32_t _M0L6_2atmpS1715;
      int32_t _M0L6_2atmpS1717;
      int32_t _M0L6_2atmpS1716;
      int32_t _M0L7currentS633;
      int32_t _M0L1iS634;
      uint64_t _M0L6outputS635;
      if (
        _M0L6_2atmpS1697 < 0
        || _M0L6_2atmpS1697 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1697] = 48;
      _M0L6_2atmpS1699 = _M0Lm5indexS613;
      _M0L6_2atmpS1698 = _M0L6_2atmpS1699 + 1;
      if (
        _M0L6_2atmpS1698 < 0
        || _M0L6_2atmpS1698 >= Moonbit_array_length(_M0L6resultS612)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS612[_M0L6_2atmpS1698] = 46;
      _M0L6_2atmpS1700 = _M0Lm5indexS613;
      _M0Lm5indexS613 = _M0L6_2atmpS1700 + 2;
      _M0L1iS631 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1701 = _M0Lm3expS618;
        if (_M0L1iS631 > _M0L6_2atmpS1701) {
          int32_t _M0L6_2atmpS1704 = _M0Lm5indexS613;
          int32_t _M0L6_2atmpS1703 = _M0L6_2atmpS1704 - _M0L1iS631;
          int32_t _M0L6_2atmpS1702 = _M0L6_2atmpS1703 - 1;
          int32_t _M0L6_2atmpS1705;
          if (
            _M0L6_2atmpS1702 < 0
            || _M0L6_2atmpS1702 >= Moonbit_array_length(_M0L6resultS612)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS612[_M0L6_2atmpS1702] = 48;
          _M0L6_2atmpS1705 = _M0L1iS631 - 1;
          _M0L1iS631 = _M0L6_2atmpS1705;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1715 = _M0Lm5indexS613;
      _M0L6_2atmpS1717 = _M0Lm3expS618;
      _M0L6_2atmpS1716 = -1 - _M0L6_2atmpS1717;
      _M0L7currentS633 = _M0L6_2atmpS1715 + _M0L6_2atmpS1716;
      _M0L1iS634 = 0;
      _M0L6outputS635 = _M0L6outputS615;
      while (1) {
        if (_M0L1iS634 < _M0L7olengthS617) {
          int32_t _M0L6_2atmpS1712 = _M0L7currentS633 + _M0L7olengthS617;
          int32_t _M0L6_2atmpS1711 = _M0L6_2atmpS1712 - _M0L1iS634;
          int32_t _M0L6_2atmpS1706 = _M0L6_2atmpS1711 - 1;
          uint64_t _M0L6_2atmpS1710 = _M0L6outputS635 % 10ull;
          int32_t _M0L6_2atmpS1709 = (int32_t)_M0L6_2atmpS1710;
          int32_t _M0L6_2atmpS1708 = 48 + _M0L6_2atmpS1709;
          int32_t _M0L6_2atmpS1707 = _M0L6_2atmpS1708 & 0xff;
          int32_t _M0L6_2atmpS1713;
          uint64_t _M0L6_2atmpS1714;
          if (
            _M0L6_2atmpS1706 < 0
            || _M0L6_2atmpS1706 >= Moonbit_array_length(_M0L6resultS612)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS612[_M0L6_2atmpS1706] = _M0L6_2atmpS1707;
          _M0L6_2atmpS1713 = _M0L1iS634 + 1;
          _M0L6_2atmpS1714 = _M0L6outputS635 / 10ull;
          _M0L1iS634 = _M0L6_2atmpS1713;
          _M0L6outputS635 = _M0L6_2atmpS1714;
          continue;
        }
        break;
      }
      _M0Lm5indexS613 = _M0L7currentS633 + _M0L7olengthS617;
    } else {
      int32_t _M0L6_2atmpS1719 = _M0Lm3expS618;
      int32_t _M0L6_2atmpS1718 = _M0L6_2atmpS1719 + 1;
      if (_M0L6_2atmpS1718 >= _M0L7olengthS617) {
        int32_t _M0L1iS637 = 0;
        uint64_t _M0L6outputS638 = _M0L6outputS615;
        int32_t _M0L6_2atmpS1730;
        int32_t _M0L6_2atmpS1735;
        int32_t _M0L7_2abindS640;
        int32_t _M0L1iS641;
        int32_t _M0L6_2atmpS1736;
        int32_t _M0L6_2atmpS1739;
        int32_t _M0L6_2atmpS1738;
        int32_t _M0L6_2atmpS1737;
        while (1) {
          if (_M0L1iS637 < _M0L7olengthS617) {
            int32_t _M0L6_2atmpS1727 = _M0Lm5indexS613;
            int32_t _M0L6_2atmpS1726 = _M0L6_2atmpS1727 + _M0L7olengthS617;
            int32_t _M0L6_2atmpS1725 = _M0L6_2atmpS1726 - _M0L1iS637;
            int32_t _M0L6_2atmpS1720 = _M0L6_2atmpS1725 - 1;
            uint64_t _M0L6_2atmpS1724 = _M0L6outputS638 % 10ull;
            int32_t _M0L6_2atmpS1723 = (int32_t)_M0L6_2atmpS1724;
            int32_t _M0L6_2atmpS1722 = 48 + _M0L6_2atmpS1723;
            int32_t _M0L6_2atmpS1721 = _M0L6_2atmpS1722 & 0xff;
            int32_t _M0L6_2atmpS1728;
            uint64_t _M0L6_2atmpS1729;
            if (
              _M0L6_2atmpS1720 < 0
              || _M0L6_2atmpS1720 >= Moonbit_array_length(_M0L6resultS612)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS612[_M0L6_2atmpS1720] = _M0L6_2atmpS1721;
            _M0L6_2atmpS1728 = _M0L1iS637 + 1;
            _M0L6_2atmpS1729 = _M0L6outputS638 / 10ull;
            _M0L1iS637 = _M0L6_2atmpS1728;
            _M0L6outputS638 = _M0L6_2atmpS1729;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1730 = _M0Lm5indexS613;
        _M0Lm5indexS613 = _M0L6_2atmpS1730 + _M0L7olengthS617;
        _M0L6_2atmpS1735 = _M0Lm3expS618;
        _M0L7_2abindS640 = _M0L6_2atmpS1735 + 1;
        _M0L1iS641 = _M0L7olengthS617;
        while (1) {
          if (_M0L1iS641 < _M0L7_2abindS640) {
            int32_t _M0L6_2atmpS1733 = _M0Lm5indexS613;
            int32_t _M0L6_2atmpS1732 = _M0L6_2atmpS1733 + _M0L1iS641;
            int32_t _M0L6_2atmpS1731 = _M0L6_2atmpS1732 - _M0L7olengthS617;
            int32_t _M0L6_2atmpS1734;
            if (
              _M0L6_2atmpS1731 < 0
              || _M0L6_2atmpS1731 >= Moonbit_array_length(_M0L6resultS612)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS612[_M0L6_2atmpS1731] = 48;
            _M0L6_2atmpS1734 = _M0L1iS641 + 1;
            _M0L1iS641 = _M0L6_2atmpS1734;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1736 = _M0Lm5indexS613;
        _M0L6_2atmpS1739 = _M0Lm3expS618;
        _M0L6_2atmpS1738 = _M0L6_2atmpS1739 + 1;
        _M0L6_2atmpS1737 = _M0L6_2atmpS1738 - _M0L7olengthS617;
        _M0Lm5indexS613 = _M0L6_2atmpS1736 + _M0L6_2atmpS1737;
      } else {
        int32_t _M0L6_2atmpS1756 = _M0Lm5indexS613;
        int32_t _M0L6_2atmpS1755 = _M0L6_2atmpS1756 + 1;
        int32_t _M0L1iS643 = 0;
        int32_t _M0L7currentS644 = _M0L6_2atmpS1755;
        uint64_t _M0L6outputS645 = _M0L6outputS615;
        int32_t _M0L6_2atmpS1757;
        int32_t _M0L6_2atmpS1758;
        while (1) {
          if (_M0L1iS643 < _M0L7olengthS617) {
            int32_t _M0L6_2atmpS1751 = _M0L7olengthS617 - _M0L1iS643;
            int32_t _M0L6_2atmpS1749 = _M0L6_2atmpS1751 - 1;
            int32_t _M0L6_2atmpS1750 = _M0Lm3expS618;
            int32_t _M0L7currentS646;
            int32_t _M0L6_2atmpS1746;
            int32_t _M0L6_2atmpS1745;
            int32_t _M0L6_2atmpS1740;
            uint64_t _M0L6_2atmpS1744;
            int32_t _M0L6_2atmpS1743;
            int32_t _M0L6_2atmpS1742;
            int32_t _M0L6_2atmpS1741;
            int32_t _M0L6_2atmpS1747;
            uint64_t _M0L6_2atmpS1748;
            if (_M0L6_2atmpS1749 == _M0L6_2atmpS1750) {
              int32_t _M0L6_2atmpS1754 = _M0L7currentS644 + _M0L7olengthS617;
              int32_t _M0L6_2atmpS1753 = _M0L6_2atmpS1754 - _M0L1iS643;
              int32_t _M0L6_2atmpS1752 = _M0L6_2atmpS1753 - 1;
              if (
                _M0L6_2atmpS1752 < 0
                || _M0L6_2atmpS1752 >= Moonbit_array_length(_M0L6resultS612)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS612[_M0L6_2atmpS1752] = 46;
              _M0L7currentS646 = _M0L7currentS644 - 1;
            } else {
              _M0L7currentS646 = _M0L7currentS644;
            }
            _M0L6_2atmpS1746 = _M0L7currentS646 + _M0L7olengthS617;
            _M0L6_2atmpS1745 = _M0L6_2atmpS1746 - _M0L1iS643;
            _M0L6_2atmpS1740 = _M0L6_2atmpS1745 - 1;
            _M0L6_2atmpS1744 = _M0L6outputS645 % 10ull;
            _M0L6_2atmpS1743 = (int32_t)_M0L6_2atmpS1744;
            _M0L6_2atmpS1742 = 48 + _M0L6_2atmpS1743;
            _M0L6_2atmpS1741 = _M0L6_2atmpS1742 & 0xff;
            if (
              _M0L6_2atmpS1740 < 0
              || _M0L6_2atmpS1740 >= Moonbit_array_length(_M0L6resultS612)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS612[_M0L6_2atmpS1740] = _M0L6_2atmpS1741;
            _M0L6_2atmpS1747 = _M0L1iS643 + 1;
            _M0L6_2atmpS1748 = _M0L6outputS645 / 10ull;
            _M0L1iS643 = _M0L6_2atmpS1747;
            _M0L7currentS644 = _M0L7currentS646;
            _M0L6outputS645 = _M0L6_2atmpS1748;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1757 = _M0Lm5indexS613;
        _M0L6_2atmpS1758 = _M0L7olengthS617 + 1;
        _M0Lm5indexS613 = _M0L6_2atmpS1757 + _M0L6_2atmpS1758;
      }
    }
    _M0L6_2atmpS1759 = _M0Lm5indexS613;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2467
    = _M0FPB19string__from__bytes(_M0L6resultS612, 0, _M0L6_2atmpS1759);
    moonbit_decref_cycle_free(_M0L6resultS612);
    return _result_2467;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS558,
  uint32_t _M0L12ieeeExponentS557
) {
  int32_t _M0Lm2e2S555;
  uint64_t _M0Lm2m2S556;
  uint64_t _M0L6_2atmpS1633;
  uint64_t _M0L6_2atmpS1632;
  int32_t _M0L4evenS559;
  uint64_t _M0L6_2atmpS1631;
  uint64_t _M0L2mvS560;
  int32_t _M0L7mmShiftS561;
  uint64_t _M0Lm2vrS562;
  uint64_t _M0Lm2vpS563;
  uint64_t _M0Lm2vmS564;
  int32_t _M0Lm3e10S565;
  int32_t _M0Lm17vmIsTrailingZerosS566;
  int32_t _M0Lm17vrIsTrailingZerosS567;
  int32_t _M0L6_2atmpS1533;
  int32_t _M0Lm7removedS586;
  int32_t _M0Lm16lastRemovedDigitS587;
  uint64_t _M0Lm6outputS588;
  int32_t _M0L6_2atmpS1629;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L3expS611;
  uint64_t _M0L6_2atmpS1628;
  struct _M0TPB17FloatingDecimal64* _block_2473;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S555 = 0;
  _M0Lm2m2S556 = 0ull;
  if (_M0L12ieeeExponentS557 == 0u) {
    _M0Lm2e2S555 = -1076;
    _M0Lm2m2S556 = _M0L12ieeeMantissaS558;
  } else {
    int32_t _M0L6_2atmpS1532 = *(int32_t*)&_M0L12ieeeExponentS557;
    int32_t _M0L6_2atmpS1531 = _M0L6_2atmpS1532 - 1023;
    int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1531 - 52;
    _M0Lm2e2S555 = _M0L6_2atmpS1530 - 2;
    _M0Lm2m2S556 = 4503599627370496ull | _M0L12ieeeMantissaS558;
  }
  _M0L6_2atmpS1633 = _M0Lm2m2S556;
  _M0L6_2atmpS1632 = _M0L6_2atmpS1633 & 1ull;
  _M0L4evenS559 = _M0L6_2atmpS1632 == 0ull;
  _M0L6_2atmpS1631 = _M0Lm2m2S556;
  _M0L2mvS560 = 4ull * _M0L6_2atmpS1631;
  _M0L7mmShiftS561
  = _M0L12ieeeMantissaS558 != 0ull || _M0L12ieeeExponentS557 <= 1u;
  _M0Lm2vrS562 = 0ull;
  _M0Lm2vpS563 = 0ull;
  _M0Lm2vmS564 = 0ull;
  _M0Lm3e10S565 = 0;
  _M0Lm17vmIsTrailingZerosS566 = 0;
  _M0Lm17vrIsTrailingZerosS567 = 0;
  _M0L6_2atmpS1533 = _M0Lm2e2S555;
  if (_M0L6_2atmpS1533 >= 0) {
    int32_t _M0L6_2atmpS1555 = _M0Lm2e2S555;
    int32_t _M0L6_2atmpS1551;
    int32_t _M0L6_2atmpS1554;
    int32_t _M0L6_2atmpS1553;
    int32_t _M0L6_2atmpS1552;
    int32_t _M0L1qS568;
    int32_t _M0L6_2atmpS1550;
    int32_t _M0L6_2atmpS1549;
    int32_t _M0L1kS569;
    int32_t _M0L6_2atmpS1548;
    int32_t _M0L6_2atmpS1547;
    int32_t _M0L6_2atmpS1546;
    int32_t _M0L1iS570;
    struct _M0TPB8Pow5Pair _M0L4pow5S571;
    uint64_t _M0L6_2atmpS1545;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS572;
    uint64_t _M0L8_2avrOutS573;
    uint64_t _M0L8_2avpOutS574;
    uint64_t _M0L8_2avmOutS575;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1551 = _M0FPB9log10Pow2(_M0L6_2atmpS1555);
    _M0L6_2atmpS1554 = _M0Lm2e2S555;
    _M0L6_2atmpS1553 = _M0L6_2atmpS1554 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1552 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1553);
    _M0L1qS568 = _M0L6_2atmpS1551 - _M0L6_2atmpS1552;
    _M0Lm3e10S565 = _M0L1qS568;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1550 = _M0FPB8pow5bits(_M0L1qS568);
    _M0L6_2atmpS1549 = 125 + _M0L6_2atmpS1550;
    _M0L1kS569 = _M0L6_2atmpS1549 - 1;
    _M0L6_2atmpS1548 = _M0Lm2e2S555;
    _M0L6_2atmpS1547 = -_M0L6_2atmpS1548;
    _M0L6_2atmpS1546 = _M0L6_2atmpS1547 + _M0L1qS568;
    _M0L1iS570 = _M0L6_2atmpS1546 + _M0L1kS569;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S571 = _M0FPB22double__computeInvPow5(_M0L1qS568);
    _M0L6_2atmpS1545 = _M0Lm2m2S556;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS572
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1545, _M0L4pow5S571, _M0L1iS570, _M0L7mmShiftS561);
    _M0L8_2avrOutS573 = _M0L7_2abindS572.$0;
    _M0L8_2avpOutS574 = _M0L7_2abindS572.$1;
    _M0L8_2avmOutS575 = _M0L7_2abindS572.$2;
    _M0Lm2vrS562 = _M0L8_2avrOutS573;
    _M0Lm2vpS563 = _M0L8_2avpOutS574;
    _M0Lm2vmS564 = _M0L8_2avmOutS575;
    if (_M0L1qS568 <= 21) {
      int32_t _M0L6_2atmpS1541 = (int32_t)_M0L2mvS560;
      uint64_t _M0L6_2atmpS1544 = _M0L2mvS560 / 5ull;
      int32_t _M0L6_2atmpS1543 = (int32_t)_M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1542 = 5 * _M0L6_2atmpS1543;
      int32_t _M0L6mvMod5S576 = _M0L6_2atmpS1541 - _M0L6_2atmpS1542;
      if (_M0L6mvMod5S576 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS567
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS560, _M0L1qS568);
      } else if (_M0L4evenS559) {
        uint64_t _M0L6_2atmpS1535 = _M0L2mvS560 - 1ull;
        uint64_t _M0L6_2atmpS1536;
        uint64_t _M0L6_2atmpS1534;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1536 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS561);
        _M0L6_2atmpS1534 = _M0L6_2atmpS1535 - _M0L6_2atmpS1536;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS566
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1534, _M0L1qS568);
      } else {
        uint64_t _M0L6_2atmpS1537 = _M0Lm2vpS563;
        uint64_t _M0L6_2atmpS1540 = _M0L2mvS560 + 2ull;
        int32_t _M0L6_2atmpS1539;
        uint64_t _M0L6_2atmpS1538;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1539
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1540, _M0L1qS568);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1538 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1539);
        _M0Lm2vpS563 = _M0L6_2atmpS1537 - _M0L6_2atmpS1538;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1569 = _M0Lm2e2S555;
    int32_t _M0L6_2atmpS1568 = -_M0L6_2atmpS1569;
    int32_t _M0L6_2atmpS1563;
    int32_t _M0L6_2atmpS1567;
    int32_t _M0L6_2atmpS1566;
    int32_t _M0L6_2atmpS1565;
    int32_t _M0L6_2atmpS1564;
    int32_t _M0L1qS577;
    int32_t _M0L6_2atmpS1556;
    int32_t _M0L6_2atmpS1562;
    int32_t _M0L6_2atmpS1561;
    int32_t _M0L1iS578;
    int32_t _M0L6_2atmpS1560;
    int32_t _M0L1kS579;
    int32_t _M0L1jS580;
    struct _M0TPB8Pow5Pair _M0L4pow5S581;
    uint64_t _M0L6_2atmpS1559;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS582;
    uint64_t _M0L8_2avrOutS583;
    uint64_t _M0L8_2avpOutS584;
    uint64_t _M0L8_2avmOutS585;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1563 = _M0FPB9log10Pow5(_M0L6_2atmpS1568);
    _M0L6_2atmpS1567 = _M0Lm2e2S555;
    _M0L6_2atmpS1566 = -_M0L6_2atmpS1567;
    _M0L6_2atmpS1565 = _M0L6_2atmpS1566 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1564 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1565);
    _M0L1qS577 = _M0L6_2atmpS1563 - _M0L6_2atmpS1564;
    _M0L6_2atmpS1556 = _M0Lm2e2S555;
    _M0Lm3e10S565 = _M0L1qS577 + _M0L6_2atmpS1556;
    _M0L6_2atmpS1562 = _M0Lm2e2S555;
    _M0L6_2atmpS1561 = -_M0L6_2atmpS1562;
    _M0L1iS578 = _M0L6_2atmpS1561 - _M0L1qS577;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1560 = _M0FPB8pow5bits(_M0L1iS578);
    _M0L1kS579 = _M0L6_2atmpS1560 - 125;
    _M0L1jS580 = _M0L1qS577 - _M0L1kS579;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S581 = _M0FPB19double__computePow5(_M0L1iS578);
    _M0L6_2atmpS1559 = _M0Lm2m2S556;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS582
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1559, _M0L4pow5S581, _M0L1jS580, _M0L7mmShiftS561);
    _M0L8_2avrOutS583 = _M0L7_2abindS582.$0;
    _M0L8_2avpOutS584 = _M0L7_2abindS582.$1;
    _M0L8_2avmOutS585 = _M0L7_2abindS582.$2;
    _M0Lm2vrS562 = _M0L8_2avrOutS583;
    _M0Lm2vpS563 = _M0L8_2avpOutS584;
    _M0Lm2vmS564 = _M0L8_2avmOutS585;
    if (_M0L1qS577 <= 1) {
      _M0Lm17vrIsTrailingZerosS567 = 1;
      if (_M0L4evenS559) {
        int32_t _M0L6_2atmpS1557;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1557 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS561);
        _M0Lm17vmIsTrailingZerosS566 = _M0L6_2atmpS1557 == 1;
      } else {
        uint64_t _M0L6_2atmpS1558 = _M0Lm2vpS563;
        _M0Lm2vpS563 = _M0L6_2atmpS1558 - 1ull;
      }
    } else if (_M0L1qS577 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS567
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS560, _M0L1qS577);
    }
  }
  _M0Lm7removedS586 = 0;
  _M0Lm16lastRemovedDigitS587 = 0;
  _M0Lm6outputS588 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS566 || _M0Lm17vrIsTrailingZerosS567) {
    int32_t _if__result_2470;
    uint64_t _M0L6_2atmpS1599;
    uint64_t _M0L6_2atmpS1605;
    uint64_t _M0L6_2atmpS1606;
    int32_t _if__result_2471;
    int32_t _M0L6_2atmpS1602;
    int64_t _M0L6_2atmpS1601;
    uint64_t _M0L6_2atmpS1600;
    while (1) {
      uint64_t _M0L6_2atmpS1582 = _M0Lm2vpS563;
      uint64_t _M0L7vpDiv10S589 = _M0L6_2atmpS1582 / 10ull;
      uint64_t _M0L6_2atmpS1581 = _M0Lm2vmS564;
      uint64_t _M0L7vmDiv10S590 = _M0L6_2atmpS1581 / 10ull;
      uint64_t _M0L6_2atmpS1580;
      int32_t _M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1578;
      int32_t _M0L7vmMod10S592;
      uint64_t _M0L6_2atmpS1576;
      uint64_t _M0L7vrDiv10S593;
      uint64_t _M0L6_2atmpS1575;
      int32_t _M0L6_2atmpS1572;
      int32_t _M0L6_2atmpS1574;
      int32_t _M0L6_2atmpS1573;
      int32_t _M0L7vrMod10S594;
      int32_t _M0L6_2atmpS1571;
      if (_M0L7vpDiv10S589 <= _M0L7vmDiv10S590) {
        break;
      }
      _M0L6_2atmpS1580 = _M0Lm2vmS564;
      _M0L6_2atmpS1577 = (int32_t)_M0L6_2atmpS1580;
      _M0L6_2atmpS1579 = (int32_t)_M0L7vmDiv10S590;
      _M0L6_2atmpS1578 = 10 * _M0L6_2atmpS1579;
      _M0L7vmMod10S592 = _M0L6_2atmpS1577 - _M0L6_2atmpS1578;
      _M0L6_2atmpS1576 = _M0Lm2vrS562;
      _M0L7vrDiv10S593 = _M0L6_2atmpS1576 / 10ull;
      _M0L6_2atmpS1575 = _M0Lm2vrS562;
      _M0L6_2atmpS1572 = (int32_t)_M0L6_2atmpS1575;
      _M0L6_2atmpS1574 = (int32_t)_M0L7vrDiv10S593;
      _M0L6_2atmpS1573 = 10 * _M0L6_2atmpS1574;
      _M0L7vrMod10S594 = _M0L6_2atmpS1572 - _M0L6_2atmpS1573;
      _M0Lm17vmIsTrailingZerosS566
      = _M0Lm17vmIsTrailingZerosS566 && _M0L7vmMod10S592 == 0;
      if (_M0Lm17vrIsTrailingZerosS567) {
        int32_t _M0L6_2atmpS1570 = _M0Lm16lastRemovedDigitS587;
        _M0Lm17vrIsTrailingZerosS567 = _M0L6_2atmpS1570 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS567 = 0;
      }
      _M0Lm16lastRemovedDigitS587 = _M0L7vrMod10S594;
      _M0Lm2vrS562 = _M0L7vrDiv10S593;
      _M0Lm2vpS563 = _M0L7vpDiv10S589;
      _M0Lm2vmS564 = _M0L7vmDiv10S590;
      _M0L6_2atmpS1571 = _M0Lm7removedS586;
      _M0Lm7removedS586 = _M0L6_2atmpS1571 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS566) {
      while (1) {
        uint64_t _M0L6_2atmpS1595 = _M0Lm2vmS564;
        uint64_t _M0L7vmDiv10S595 = _M0L6_2atmpS1595 / 10ull;
        uint64_t _M0L6_2atmpS1594 = _M0Lm2vmS564;
        int32_t _M0L6_2atmpS1591 = (int32_t)_M0L6_2atmpS1594;
        int32_t _M0L6_2atmpS1593 = (int32_t)_M0L7vmDiv10S595;
        int32_t _M0L6_2atmpS1592 = 10 * _M0L6_2atmpS1593;
        int32_t _M0L7vmMod10S596 = _M0L6_2atmpS1591 - _M0L6_2atmpS1592;
        uint64_t _M0L6_2atmpS1590;
        uint64_t _M0L7vpDiv10S598;
        uint64_t _M0L6_2atmpS1589;
        uint64_t _M0L7vrDiv10S599;
        uint64_t _M0L6_2atmpS1588;
        int32_t _M0L6_2atmpS1585;
        int32_t _M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1586;
        int32_t _M0L7vrMod10S600;
        int32_t _M0L6_2atmpS1584;
        if (_M0L7vmMod10S596 != 0) {
          break;
        }
        _M0L6_2atmpS1590 = _M0Lm2vpS563;
        _M0L7vpDiv10S598 = _M0L6_2atmpS1590 / 10ull;
        _M0L6_2atmpS1589 = _M0Lm2vrS562;
        _M0L7vrDiv10S599 = _M0L6_2atmpS1589 / 10ull;
        _M0L6_2atmpS1588 = _M0Lm2vrS562;
        _M0L6_2atmpS1585 = (int32_t)_M0L6_2atmpS1588;
        _M0L6_2atmpS1587 = (int32_t)_M0L7vrDiv10S599;
        _M0L6_2atmpS1586 = 10 * _M0L6_2atmpS1587;
        _M0L7vrMod10S600 = _M0L6_2atmpS1585 - _M0L6_2atmpS1586;
        if (_M0Lm17vrIsTrailingZerosS567) {
          int32_t _M0L6_2atmpS1583 = _M0Lm16lastRemovedDigitS587;
          _M0Lm17vrIsTrailingZerosS567 = _M0L6_2atmpS1583 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS567 = 0;
        }
        _M0Lm16lastRemovedDigitS587 = _M0L7vrMod10S600;
        _M0Lm2vrS562 = _M0L7vrDiv10S599;
        _M0Lm2vpS563 = _M0L7vpDiv10S598;
        _M0Lm2vmS564 = _M0L7vmDiv10S595;
        _M0L6_2atmpS1584 = _M0Lm7removedS586;
        _M0Lm7removedS586 = _M0L6_2atmpS1584 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS567) {
      int32_t _M0L6_2atmpS1598 = _M0Lm16lastRemovedDigitS587;
      if (_M0L6_2atmpS1598 == 5) {
        uint64_t _M0L6_2atmpS1597 = _M0Lm2vrS562;
        uint64_t _M0L6_2atmpS1596 = _M0L6_2atmpS1597 % 2ull;
        _if__result_2470 = _M0L6_2atmpS1596 == 0ull;
      } else {
        _if__result_2470 = 0;
      }
    } else {
      _if__result_2470 = 0;
    }
    if (_if__result_2470) {
      _M0Lm16lastRemovedDigitS587 = 4;
    }
    _M0L6_2atmpS1599 = _M0Lm2vrS562;
    _M0L6_2atmpS1605 = _M0Lm2vrS562;
    _M0L6_2atmpS1606 = _M0Lm2vmS564;
    if (_M0L6_2atmpS1605 == _M0L6_2atmpS1606) {
      if (!_M0L4evenS559) {
        _if__result_2471 = 1;
      } else {
        int32_t _M0L6_2atmpS1604 = _M0Lm17vmIsTrailingZerosS566;
        _if__result_2471 = !_M0L6_2atmpS1604;
      }
    } else {
      _if__result_2471 = 0;
    }
    if (_if__result_2471) {
      _M0L6_2atmpS1602 = 1;
    } else {
      int32_t _M0L6_2atmpS1603 = _M0Lm16lastRemovedDigitS587;
      _M0L6_2atmpS1602 = _M0L6_2atmpS1603 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1601 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1602);
    _M0L6_2atmpS1600 = *(uint64_t*)&_M0L6_2atmpS1601;
    _M0Lm6outputS588 = _M0L6_2atmpS1599 + _M0L6_2atmpS1600;
  } else {
    int32_t _M0Lm7roundUpS601 = 0;
    uint64_t _M0L6_2atmpS1627 = _M0Lm2vpS563;
    uint64_t _M0L8vpDiv100S602 = _M0L6_2atmpS1627 / 100ull;
    uint64_t _M0L6_2atmpS1626 = _M0Lm2vmS564;
    uint64_t _M0L8vmDiv100S603 = _M0L6_2atmpS1626 / 100ull;
    uint64_t _M0L6_2atmpS1621;
    uint64_t _M0L6_2atmpS1624;
    uint64_t _M0L6_2atmpS1625;
    int32_t _M0L6_2atmpS1623;
    uint64_t _M0L6_2atmpS1622;
    if (_M0L8vpDiv100S602 > _M0L8vmDiv100S603) {
      uint64_t _M0L6_2atmpS1612 = _M0Lm2vrS562;
      uint64_t _M0L8vrDiv100S604 = _M0L6_2atmpS1612 / 100ull;
      uint64_t _M0L6_2atmpS1611 = _M0Lm2vrS562;
      int32_t _M0L6_2atmpS1608 = (int32_t)_M0L6_2atmpS1611;
      int32_t _M0L6_2atmpS1610 = (int32_t)_M0L8vrDiv100S604;
      int32_t _M0L6_2atmpS1609 = 100 * _M0L6_2atmpS1610;
      int32_t _M0L8vrMod100S605 = _M0L6_2atmpS1608 - _M0L6_2atmpS1609;
      int32_t _M0L6_2atmpS1607;
      _M0Lm7roundUpS601 = _M0L8vrMod100S605 >= 50;
      _M0Lm2vrS562 = _M0L8vrDiv100S604;
      _M0Lm2vpS563 = _M0L8vpDiv100S602;
      _M0Lm2vmS564 = _M0L8vmDiv100S603;
      _M0L6_2atmpS1607 = _M0Lm7removedS586;
      _M0Lm7removedS586 = _M0L6_2atmpS1607 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1620 = _M0Lm2vpS563;
      uint64_t _M0L7vpDiv10S606 = _M0L6_2atmpS1620 / 10ull;
      uint64_t _M0L6_2atmpS1619 = _M0Lm2vmS564;
      uint64_t _M0L7vmDiv10S607 = _M0L6_2atmpS1619 / 10ull;
      uint64_t _M0L6_2atmpS1618;
      uint64_t _M0L7vrDiv10S609;
      uint64_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L7vrMod10S610;
      int32_t _M0L6_2atmpS1613;
      if (_M0L7vpDiv10S606 <= _M0L7vmDiv10S607) {
        break;
      }
      _M0L6_2atmpS1618 = _M0Lm2vrS562;
      _M0L7vrDiv10S609 = _M0L6_2atmpS1618 / 10ull;
      _M0L6_2atmpS1617 = _M0Lm2vrS562;
      _M0L6_2atmpS1614 = (int32_t)_M0L6_2atmpS1617;
      _M0L6_2atmpS1616 = (int32_t)_M0L7vrDiv10S609;
      _M0L6_2atmpS1615 = 10 * _M0L6_2atmpS1616;
      _M0L7vrMod10S610 = _M0L6_2atmpS1614 - _M0L6_2atmpS1615;
      _M0Lm7roundUpS601 = _M0L7vrMod10S610 >= 5;
      _M0Lm2vrS562 = _M0L7vrDiv10S609;
      _M0Lm2vpS563 = _M0L7vpDiv10S606;
      _M0Lm2vmS564 = _M0L7vmDiv10S607;
      _M0L6_2atmpS1613 = _M0Lm7removedS586;
      _M0Lm7removedS586 = _M0L6_2atmpS1613 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1621 = _M0Lm2vrS562;
    _M0L6_2atmpS1624 = _M0Lm2vrS562;
    _M0L6_2atmpS1625 = _M0Lm2vmS564;
    _M0L6_2atmpS1623
    = _M0L6_2atmpS1624 == _M0L6_2atmpS1625 || _M0Lm7roundUpS601;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1622 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1623);
    _M0Lm6outputS588 = _M0L6_2atmpS1621 + _M0L6_2atmpS1622;
  }
  _M0L6_2atmpS1629 = _M0Lm3e10S565;
  _M0L6_2atmpS1630 = _M0Lm7removedS586;
  _M0L3expS611 = _M0L6_2atmpS1629 + _M0L6_2atmpS1630;
  _M0L6_2atmpS1628 = _M0Lm6outputS588;
  _block_2473
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2473)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2473->$0 = _M0L6_2atmpS1628;
  _block_2473->$1 = _M0L3expS611;
  return _block_2473;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS554) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS554) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS553) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS553) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS552) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS552) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS551) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS551 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS551 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS551 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS551 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS551 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS551 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS551 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS551 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS551 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS551 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS551 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS551 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS551 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS551 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS551 >= 100ull) {
    return 3;
  }
  if (_M0L1vS551 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS534) {
  int32_t _M0L6_2atmpS1529;
  int32_t _M0L6_2atmpS1528;
  int32_t _M0L4baseS533;
  int32_t _M0L5base2S535;
  int32_t _M0L6offsetS536;
  int32_t _M0L6_2atmpS1527;
  uint64_t _M0L4mul0S537;
  int32_t _M0L6_2atmpS1526;
  int32_t _M0L6_2atmpS1525;
  uint64_t _M0L4mul1S538;
  uint64_t _M0L1mS539;
  struct _M0TPB7Umul128 _M0L7_2abindS540;
  uint64_t _M0L7_2alow1S541;
  uint64_t _M0L8_2ahigh1S542;
  struct _M0TPB7Umul128 _M0L7_2abindS543;
  uint64_t _M0L7_2alow0S544;
  uint64_t _M0L8_2ahigh0S545;
  uint64_t _M0L3sumS546;
  uint64_t _M0Lm5high1S547;
  int32_t _M0L6_2atmpS1523;
  int32_t _M0L6_2atmpS1524;
  int32_t _M0L5deltaS548;
  uint64_t _M0L6_2atmpS1522;
  uint64_t _M0L6_2atmpS1514;
  int32_t _M0L6_2atmpS1521;
  uint32_t _M0L6_2atmpS1518;
  int32_t _M0L6_2atmpS1520;
  int32_t _M0L6_2atmpS1519;
  uint32_t _M0L6_2atmpS1517;
  uint32_t _M0L6_2atmpS1516;
  uint64_t _M0L6_2atmpS1515;
  uint64_t _M0L1aS549;
  uint64_t _M0L6_2atmpS1513;
  uint64_t _M0L1bS550;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1529 = _M0L1iS534 + 26;
  _M0L6_2atmpS1528 = _M0L6_2atmpS1529 - 1;
  _M0L4baseS533 = _M0L6_2atmpS1528 / 26;
  _M0L5base2S535 = _M0L4baseS533 * 26;
  _M0L6offsetS536 = _M0L5base2S535 - _M0L1iS534;
  _M0L6_2atmpS1527 = _M0L4baseS533 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S537
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1527);
  _M0L6_2atmpS1526 = _M0L4baseS533 * 2;
  _M0L6_2atmpS1525 = _M0L6_2atmpS1526 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S538
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1525);
  if (_M0L6offsetS536 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S537, .$1 = _M0L4mul1S538};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS539
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS536);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS540 = _M0FPB7umul128(_M0L1mS539, _M0L4mul1S538);
  _M0L7_2alow1S541 = _M0L7_2abindS540.$0;
  _M0L8_2ahigh1S542 = _M0L7_2abindS540.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS543 = _M0FPB7umul128(_M0L1mS539, _M0L4mul0S537);
  _M0L7_2alow0S544 = _M0L7_2abindS543.$0;
  _M0L8_2ahigh0S545 = _M0L7_2abindS543.$1;
  _M0L3sumS546 = _M0L8_2ahigh0S545 + _M0L7_2alow1S541;
  _M0Lm5high1S547 = _M0L8_2ahigh1S542;
  if (_M0L3sumS546 < _M0L8_2ahigh0S545) {
    uint64_t _M0L6_2atmpS1512 = _M0Lm5high1S547;
    _M0Lm5high1S547 = _M0L6_2atmpS1512 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1523 = _M0FPB8pow5bits(_M0L5base2S535);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1524 = _M0FPB8pow5bits(_M0L1iS534);
  _M0L5deltaS548 = _M0L6_2atmpS1523 - _M0L6_2atmpS1524;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1522
  = _M0FPB13shiftright128(_M0L7_2alow0S544, _M0L3sumS546, _M0L5deltaS548);
  _M0L6_2atmpS1514 = _M0L6_2atmpS1522 + 1ull;
  _M0L6_2atmpS1521 = _M0L1iS534 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1518
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1521);
  _M0L6_2atmpS1520 = _M0L1iS534 % 16;
  _M0L6_2atmpS1519 = _M0L6_2atmpS1520 << 1;
  _M0L6_2atmpS1517 = _M0L6_2atmpS1518 >> (_M0L6_2atmpS1519 & 31);
  _M0L6_2atmpS1516 = _M0L6_2atmpS1517 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1515 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1516);
  _M0L1aS549 = _M0L6_2atmpS1514 + _M0L6_2atmpS1515;
  _M0L6_2atmpS1513 = _M0Lm5high1S547;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS550
  = _M0FPB13shiftright128(_M0L3sumS546, _M0L6_2atmpS1513, _M0L5deltaS548);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS549, .$1 = _M0L1bS550};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS516) {
  int32_t _M0L4baseS515;
  int32_t _M0L5base2S517;
  int32_t _M0L6offsetS518;
  int32_t _M0L6_2atmpS1511;
  uint64_t _M0L4mul0S519;
  int32_t _M0L6_2atmpS1510;
  int32_t _M0L6_2atmpS1509;
  uint64_t _M0L4mul1S520;
  uint64_t _M0L1mS521;
  struct _M0TPB7Umul128 _M0L7_2abindS522;
  uint64_t _M0L7_2alow1S523;
  uint64_t _M0L8_2ahigh1S524;
  struct _M0TPB7Umul128 _M0L7_2abindS525;
  uint64_t _M0L7_2alow0S526;
  uint64_t _M0L8_2ahigh0S527;
  uint64_t _M0L3sumS528;
  uint64_t _M0Lm5high1S529;
  int32_t _M0L6_2atmpS1507;
  int32_t _M0L6_2atmpS1508;
  int32_t _M0L5deltaS530;
  uint64_t _M0L6_2atmpS1499;
  int32_t _M0L6_2atmpS1506;
  uint32_t _M0L6_2atmpS1503;
  int32_t _M0L6_2atmpS1505;
  int32_t _M0L6_2atmpS1504;
  uint32_t _M0L6_2atmpS1502;
  uint32_t _M0L6_2atmpS1501;
  uint64_t _M0L6_2atmpS1500;
  uint64_t _M0L1aS531;
  uint64_t _M0L6_2atmpS1498;
  uint64_t _M0L1bS532;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS515 = _M0L1iS516 / 26;
  _M0L5base2S517 = _M0L4baseS515 * 26;
  _M0L6offsetS518 = _M0L1iS516 - _M0L5base2S517;
  _M0L6_2atmpS1511 = _M0L4baseS515 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S519
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1511);
  _M0L6_2atmpS1510 = _M0L4baseS515 * 2;
  _M0L6_2atmpS1509 = _M0L6_2atmpS1510 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S520
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1509);
  if (_M0L6offsetS518 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S519, .$1 = _M0L4mul1S520};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS521
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS518);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS522 = _M0FPB7umul128(_M0L1mS521, _M0L4mul1S520);
  _M0L7_2alow1S523 = _M0L7_2abindS522.$0;
  _M0L8_2ahigh1S524 = _M0L7_2abindS522.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS525 = _M0FPB7umul128(_M0L1mS521, _M0L4mul0S519);
  _M0L7_2alow0S526 = _M0L7_2abindS525.$0;
  _M0L8_2ahigh0S527 = _M0L7_2abindS525.$1;
  _M0L3sumS528 = _M0L8_2ahigh0S527 + _M0L7_2alow1S523;
  _M0Lm5high1S529 = _M0L8_2ahigh1S524;
  if (_M0L3sumS528 < _M0L8_2ahigh0S527) {
    uint64_t _M0L6_2atmpS1497 = _M0Lm5high1S529;
    _M0Lm5high1S529 = _M0L6_2atmpS1497 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1507 = _M0FPB8pow5bits(_M0L1iS516);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1508 = _M0FPB8pow5bits(_M0L5base2S517);
  _M0L5deltaS530 = _M0L6_2atmpS1507 - _M0L6_2atmpS1508;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1499
  = _M0FPB13shiftright128(_M0L7_2alow0S526, _M0L3sumS528, _M0L5deltaS530);
  _M0L6_2atmpS1506 = _M0L1iS516 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1503
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1506);
  _M0L6_2atmpS1505 = _M0L1iS516 % 16;
  _M0L6_2atmpS1504 = _M0L6_2atmpS1505 << 1;
  _M0L6_2atmpS1502 = _M0L6_2atmpS1503 >> (_M0L6_2atmpS1504 & 31);
  _M0L6_2atmpS1501 = _M0L6_2atmpS1502 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1500 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1501);
  _M0L1aS531 = _M0L6_2atmpS1499 + _M0L6_2atmpS1500;
  _M0L6_2atmpS1498 = _M0Lm5high1S529;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS532
  = _M0FPB13shiftright128(_M0L3sumS528, _M0L6_2atmpS1498, _M0L5deltaS530);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS531, .$1 = _M0L1bS532};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS489,
  struct _M0TPB8Pow5Pair _M0L3mulS486,
  int32_t _M0L1jS502,
  int32_t _M0L7mmShiftS504
) {
  uint64_t _M0L7_2amul0S485;
  uint64_t _M0L7_2amul1S487;
  uint64_t _M0L1mS488;
  struct _M0TPB7Umul128 _M0L7_2abindS490;
  uint64_t _M0L5_2aloS491;
  uint64_t _M0L6_2atmpS492;
  struct _M0TPB7Umul128 _M0L7_2abindS493;
  uint64_t _M0L6_2alo2S494;
  uint64_t _M0L6_2ahi2S495;
  uint64_t _M0L3midS496;
  uint64_t _M0L6_2atmpS1496;
  uint64_t _M0L2hiS497;
  uint64_t _M0L3lo2S498;
  uint64_t _M0L6_2atmpS1494;
  uint64_t _M0L6_2atmpS1495;
  uint64_t _M0L4mid2S499;
  uint64_t _M0L6_2atmpS1493;
  uint64_t _M0L3hi2S500;
  int32_t _M0L6_2atmpS1492;
  int32_t _M0L6_2atmpS1491;
  uint64_t _M0L2vpS501;
  uint64_t _M0Lm2vmS503;
  int32_t _M0L6_2atmpS1490;
  int32_t _M0L6_2atmpS1489;
  uint64_t _M0L2vrS514;
  uint64_t _M0L6_2atmpS1488;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S485 = _M0L3mulS486.$0;
  _M0L7_2amul1S487 = _M0L3mulS486.$1;
  _M0L1mS488 = _M0L1mS489 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS490 = _M0FPB7umul128(_M0L1mS488, _M0L7_2amul0S485);
  _M0L5_2aloS491 = _M0L7_2abindS490.$0;
  _M0L6_2atmpS492 = _M0L7_2abindS490.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS493 = _M0FPB7umul128(_M0L1mS488, _M0L7_2amul1S487);
  _M0L6_2alo2S494 = _M0L7_2abindS493.$0;
  _M0L6_2ahi2S495 = _M0L7_2abindS493.$1;
  _M0L3midS496 = _M0L6_2atmpS492 + _M0L6_2alo2S494;
  if (_M0L3midS496 < _M0L6_2atmpS492) {
    _M0L6_2atmpS1496 = 1ull;
  } else {
    _M0L6_2atmpS1496 = 0ull;
  }
  _M0L2hiS497 = _M0L6_2ahi2S495 + _M0L6_2atmpS1496;
  _M0L3lo2S498 = _M0L5_2aloS491 + _M0L7_2amul0S485;
  _M0L6_2atmpS1494 = _M0L3midS496 + _M0L7_2amul1S487;
  if (_M0L3lo2S498 < _M0L5_2aloS491) {
    _M0L6_2atmpS1495 = 1ull;
  } else {
    _M0L6_2atmpS1495 = 0ull;
  }
  _M0L4mid2S499 = _M0L6_2atmpS1494 + _M0L6_2atmpS1495;
  if (_M0L4mid2S499 < _M0L3midS496) {
    _M0L6_2atmpS1493 = 1ull;
  } else {
    _M0L6_2atmpS1493 = 0ull;
  }
  _M0L3hi2S500 = _M0L2hiS497 + _M0L6_2atmpS1493;
  _M0L6_2atmpS1492 = _M0L1jS502 - 64;
  _M0L6_2atmpS1491 = _M0L6_2atmpS1492 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS501
  = _M0FPB13shiftright128(_M0L4mid2S499, _M0L3hi2S500, _M0L6_2atmpS1491);
  _M0Lm2vmS503 = 0ull;
  if (_M0L7mmShiftS504) {
    uint64_t _M0L3lo3S505 = _M0L5_2aloS491 - _M0L7_2amul0S485;
    uint64_t _M0L6_2atmpS1478 = _M0L3midS496 - _M0L7_2amul1S487;
    uint64_t _M0L6_2atmpS1479;
    uint64_t _M0L4mid3S506;
    uint64_t _M0L6_2atmpS1477;
    uint64_t _M0L3hi3S507;
    int32_t _M0L6_2atmpS1476;
    int32_t _M0L6_2atmpS1475;
    if (_M0L5_2aloS491 < _M0L3lo3S505) {
      _M0L6_2atmpS1479 = 1ull;
    } else {
      _M0L6_2atmpS1479 = 0ull;
    }
    _M0L4mid3S506 = _M0L6_2atmpS1478 - _M0L6_2atmpS1479;
    if (_M0L3midS496 < _M0L4mid3S506) {
      _M0L6_2atmpS1477 = 1ull;
    } else {
      _M0L6_2atmpS1477 = 0ull;
    }
    _M0L3hi3S507 = _M0L2hiS497 - _M0L6_2atmpS1477;
    _M0L6_2atmpS1476 = _M0L1jS502 - 64;
    _M0L6_2atmpS1475 = _M0L6_2atmpS1476 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS503
    = _M0FPB13shiftright128(_M0L4mid3S506, _M0L3hi3S507, _M0L6_2atmpS1475);
  } else {
    uint64_t _M0L3lo3S508 = _M0L5_2aloS491 + _M0L5_2aloS491;
    uint64_t _M0L6_2atmpS1486 = _M0L3midS496 + _M0L3midS496;
    uint64_t _M0L6_2atmpS1487;
    uint64_t _M0L4mid3S509;
    uint64_t _M0L6_2atmpS1484;
    uint64_t _M0L6_2atmpS1485;
    uint64_t _M0L3hi3S510;
    uint64_t _M0L3lo4S511;
    uint64_t _M0L6_2atmpS1482;
    uint64_t _M0L6_2atmpS1483;
    uint64_t _M0L4mid4S512;
    uint64_t _M0L6_2atmpS1481;
    uint64_t _M0L3hi4S513;
    int32_t _M0L6_2atmpS1480;
    if (_M0L3lo3S508 < _M0L5_2aloS491) {
      _M0L6_2atmpS1487 = 1ull;
    } else {
      _M0L6_2atmpS1487 = 0ull;
    }
    _M0L4mid3S509 = _M0L6_2atmpS1486 + _M0L6_2atmpS1487;
    _M0L6_2atmpS1484 = _M0L2hiS497 + _M0L2hiS497;
    if (_M0L4mid3S509 < _M0L3midS496) {
      _M0L6_2atmpS1485 = 1ull;
    } else {
      _M0L6_2atmpS1485 = 0ull;
    }
    _M0L3hi3S510 = _M0L6_2atmpS1484 + _M0L6_2atmpS1485;
    _M0L3lo4S511 = _M0L3lo3S508 - _M0L7_2amul0S485;
    _M0L6_2atmpS1482 = _M0L4mid3S509 - _M0L7_2amul1S487;
    if (_M0L3lo3S508 < _M0L3lo4S511) {
      _M0L6_2atmpS1483 = 1ull;
    } else {
      _M0L6_2atmpS1483 = 0ull;
    }
    _M0L4mid4S512 = _M0L6_2atmpS1482 - _M0L6_2atmpS1483;
    if (_M0L4mid3S509 < _M0L4mid4S512) {
      _M0L6_2atmpS1481 = 1ull;
    } else {
      _M0L6_2atmpS1481 = 0ull;
    }
    _M0L3hi4S513 = _M0L3hi3S510 - _M0L6_2atmpS1481;
    _M0L6_2atmpS1480 = _M0L1jS502 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS503
    = _M0FPB13shiftright128(_M0L4mid4S512, _M0L3hi4S513, _M0L6_2atmpS1480);
  }
  _M0L6_2atmpS1490 = _M0L1jS502 - 64;
  _M0L6_2atmpS1489 = _M0L6_2atmpS1490 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS514
  = _M0FPB13shiftright128(_M0L3midS496, _M0L2hiS497, _M0L6_2atmpS1489);
  _M0L6_2atmpS1488 = _M0Lm2vmS503;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS514,
                                                .$1 = _M0L2vpS501,
                                                .$2 = _M0L6_2atmpS1488};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS483,
  int32_t _M0L1pS484
) {
  uint64_t _M0L6_2atmpS1474;
  uint64_t _M0L6_2atmpS1473;
  uint64_t _M0L6_2atmpS1472;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1474 = 1ull << (_M0L1pS484 & 63);
  _M0L6_2atmpS1473 = _M0L6_2atmpS1474 - 1ull;
  _M0L6_2atmpS1472 = _M0L5valueS483 & _M0L6_2atmpS1473;
  return _M0L6_2atmpS1472 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS481,
  int32_t _M0L1pS482
) {
  int32_t _M0L6_2atmpS1471;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1471 = _M0FPB10pow5Factor(_M0L5valueS481);
  return _M0L6_2atmpS1471 >= _M0L1pS482;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS476) {
  uint64_t _M0L6_2atmpS1462;
  uint64_t _M0L6_2atmpS1463;
  uint64_t _M0L6_2atmpS1464;
  uint64_t _M0L6_2atmpS1465;
  uint64_t _M0L6_2atmpS1470;
  int32_t _M0L5countS477;
  uint64_t _M0L1vS478;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1462 = _M0L5valueS476 % 5ull;
  if (_M0L6_2atmpS1462 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1463 = _M0L5valueS476 % 25ull;
  if (_M0L6_2atmpS1463 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1464 = _M0L5valueS476 % 125ull;
  if (_M0L6_2atmpS1464 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1465 = _M0L5valueS476 % 625ull;
  if (_M0L6_2atmpS1465 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1470 = _M0L5valueS476 / 625ull;
  _M0L5countS477 = 4;
  _M0L1vS478 = _M0L6_2atmpS1470;
  while (1) {
    if (_M0L1vS478 > 0ull) {
      uint64_t _M0L6_2atmpS1466 = _M0L1vS478 % 5ull;
      int32_t _M0L6_2atmpS1467;
      uint64_t _M0L6_2atmpS1468;
      if (_M0L6_2atmpS1466 != 0ull) {
        return _M0L5countS477;
      }
      _M0L6_2atmpS1467 = _M0L5countS477 + 1;
      _M0L6_2atmpS1468 = _M0L1vS478 / 5ull;
      _M0L5countS477 = _M0L6_2atmpS1467;
      _M0L1vS478 = _M0L6_2atmpS1468;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS480;
      moonbit_string_t _M0L6_2atmpS1469;
      int32_t _result_2475;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS480
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS480, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS480, _M0L5valueS476);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1469
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS480);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS480);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2475 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1469);
      moonbit_decref_cycle_free(_M0L6_2atmpS1469);
      return _result_2475;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS475,
  uint64_t _M0L2hiS473,
  int32_t _M0L4distS474
) {
  int32_t _M0L6_2atmpS1461;
  uint64_t _M0L6_2atmpS1459;
  uint64_t _M0L6_2atmpS1460;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1461 = 64 - _M0L4distS474;
  _M0L6_2atmpS1459 = _M0L2hiS473 << (_M0L6_2atmpS1461 & 63);
  _M0L6_2atmpS1460 = _M0L2loS475 >> (_M0L4distS474 & 63);
  return _M0L6_2atmpS1459 | _M0L6_2atmpS1460;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS463,
  uint64_t _M0L1bS466
) {
  uint64_t _M0L3aLoS462;
  uint64_t _M0L3aHiS464;
  uint64_t _M0L3bLoS465;
  uint64_t _M0L3bHiS467;
  uint64_t _M0L1xS468;
  uint64_t _M0L6_2atmpS1457;
  uint64_t _M0L6_2atmpS1458;
  uint64_t _M0L1yS469;
  uint64_t _M0L6_2atmpS1455;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L1zS470;
  uint64_t _M0L6_2atmpS1453;
  uint64_t _M0L6_2atmpS1454;
  uint64_t _M0L6_2atmpS1451;
  uint64_t _M0L6_2atmpS1452;
  uint64_t _M0L1wS471;
  uint64_t _M0L2loS472;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS462 = _M0L1aS463 & 4294967295ull;
  _M0L3aHiS464 = _M0L1aS463 >> 32;
  _M0L3bLoS465 = _M0L1bS466 & 4294967295ull;
  _M0L3bHiS467 = _M0L1bS466 >> 32;
  _M0L1xS468 = _M0L3aLoS462 * _M0L3bLoS465;
  _M0L6_2atmpS1457 = _M0L3aHiS464 * _M0L3bLoS465;
  _M0L6_2atmpS1458 = _M0L1xS468 >> 32;
  _M0L1yS469 = _M0L6_2atmpS1457 + _M0L6_2atmpS1458;
  _M0L6_2atmpS1455 = _M0L3aLoS462 * _M0L3bHiS467;
  _M0L6_2atmpS1456 = _M0L1yS469 & 4294967295ull;
  _M0L1zS470 = _M0L6_2atmpS1455 + _M0L6_2atmpS1456;
  _M0L6_2atmpS1453 = _M0L3aHiS464 * _M0L3bHiS467;
  _M0L6_2atmpS1454 = _M0L1yS469 >> 32;
  _M0L6_2atmpS1451 = _M0L6_2atmpS1453 + _M0L6_2atmpS1454;
  _M0L6_2atmpS1452 = _M0L1zS470 >> 32;
  _M0L1wS471 = _M0L6_2atmpS1451 + _M0L6_2atmpS1452;
  _M0L2loS472 = _M0L1aS463 * _M0L1bS466;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS472, .$1 = _M0L1wS471};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS460,
  int32_t _M0L4fromS457,
  int32_t _M0L2toS456
) {
  int32_t _M0L3lenS455;
  int32_t _M0L6_2atmpS1450;
  uint16_t* _M0L6bufferS458;
  int32_t _M0L1iS459;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS455 = _M0L2toS456 - _M0L4fromS457;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1450 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS458
  = (uint16_t*)moonbit_make_string(_M0L3lenS455, _M0L6_2atmpS1450);
  _M0L1iS459 = 0;
  while (1) {
    if (_M0L1iS459 < _M0L3lenS455) {
      int32_t _M0L6_2atmpS1448 = _M0L4fromS457 + _M0L1iS459;
      int32_t _M0L6_2atmpS1447;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1449;
      if (
        _M0L6_2atmpS1448 < 0
        || _M0L6_2atmpS1448 >= Moonbit_array_length(_M0L5bytesS460)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1447 = (int32_t)_M0L5bytesS460[_M0L6_2atmpS1448];
      _M0L6_2atmpS1446 = (uint16_t)_M0L6_2atmpS1447;
      if (
        _M0L1iS459 < 0 || _M0L1iS459 >= Moonbit_array_length(_M0L6bufferS458)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS458[_M0L1iS459] = _M0L6_2atmpS1446;
      _M0L6_2atmpS1449 = _M0L1iS459 + 1;
      _M0L1iS459 = _M0L6_2atmpS1449;
      continue;
    }
    break;
  }
  return _M0L6bufferS458;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS454) {
  int32_t _M0L6_2atmpS1445;
  uint32_t _M0L6_2atmpS1444;
  uint32_t _M0L6_2atmpS1443;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1445 = _M0L1eS454 * 78913;
  _M0L6_2atmpS1444 = *(uint32_t*)&_M0L6_2atmpS1445;
  _M0L6_2atmpS1443 = _M0L6_2atmpS1444 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1443;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS453) {
  int32_t _M0L6_2atmpS1442;
  uint32_t _M0L6_2atmpS1441;
  uint32_t _M0L6_2atmpS1440;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1442 = _M0L1eS453 * 732923;
  _M0L6_2atmpS1441 = *(uint32_t*)&_M0L6_2atmpS1442;
  _M0L6_2atmpS1440 = _M0L6_2atmpS1441 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1440;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS451,
  int32_t _M0L8exponentS452,
  int32_t _M0L8mantissaS449
) {
  moonbit_string_t _M0L1sS450;
  moonbit_string_t _result_2478;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS449) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS451) {
    _M0L1sS450 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS450 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS452) {
    moonbit_string_t _result_2477;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2477
    = moonbit_add_string(_M0L1sS450, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS450);
    return _result_2477;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2478
  = moonbit_add_string(_M0L1sS450, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS450);
  return _result_2478;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS448) {
  int32_t _M0L6_2atmpS1439;
  uint32_t _M0L6_2atmpS1438;
  uint32_t _M0L6_2atmpS1437;
  int32_t _M0L6_2atmpS1436;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1439 = _M0L1eS448 * 1217359;
  _M0L6_2atmpS1438 = *(uint32_t*)&_M0L6_2atmpS1439;
  _M0L6_2atmpS1437 = _M0L6_2atmpS1438 >> 19;
  _M0L6_2atmpS1436 = *(int32_t*)&_M0L6_2atmpS1437;
  return _M0L6_2atmpS1436 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS447) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS447 != _M0L4selfS447) {
    return 0;
  } else if (_M0L4selfS447 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS447 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS447;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS446) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS446 != _M0L4selfS446) {
    return 0ll;
  } else if (_M0L4selfS446 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS446 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS446;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS443
) {
  float* _M0L6_2atmpS1433;
  struct _M0TPB5ArrayGfE* _block_2479;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1433 = (float*)moonbit_make_float_array_raw(_M0L3lenS443);
  _block_2479
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2479)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2479->$0 = _M0L6_2atmpS1433;
  _block_2479->$1 = _M0L3lenS443;
  return _block_2479;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS444
) {
  uint8_t* _M0L6_2atmpS1434;
  struct _M0TPB5ArrayGbE* _block_2480;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1434 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS444);
  _block_2480
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2480)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _block_2480->$0 = _M0L6_2atmpS1434;
  _block_2480->$1 = _M0L3lenS444;
  return _block_2480;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS445
) {
  int32_t* _M0L6_2atmpS1435;
  struct _M0TPB5ArrayGiE* _block_2481;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1435 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS445);
  _block_2481
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2481)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _block_2481->$0 = _M0L6_2atmpS1435;
  _block_2481->$1 = _M0L3lenS445;
  return _block_2481;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS439,
  int32_t _M0L5indexS440
) {
  uint64_t* _M0L6_2atmpS1431;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1431 = _M0L4selfS439;
  if (
    _M0L5indexS440 < 0
    || _M0L5indexS440 >= Moonbit_array_length(_M0L6_2atmpS1431)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1431[_M0L5indexS440];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS441,
  int32_t _M0L5indexS442
) {
  uint32_t* _M0L6_2atmpS1432;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1432 = _M0L4selfS441;
  if (
    _M0L5indexS442 < 0
    || _M0L5indexS442 >= Moonbit_array_length(_M0L6_2atmpS1432)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1432[_M0L5indexS442];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS438
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS438, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS437) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS437, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS436) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS436;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS430,
  moonbit_string_t _M0L5valueS432
) {
  int32_t _M0L3lenS1417;
  moonbit_string_t* _M0L6_2atmpS1419;
  int32_t _M0L6_2atmpS1418;
  int32_t _M0L6lengthS431;
  moonbit_string_t* _M0L3bufS1422;
  moonbit_string_t _M0L6_2aoldS2385;
  int32_t _M0L6_2atmpS1423;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1417 = _M0L4selfS430->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1419 = _M0MPC15array5Array6bufferGsE(_M0L4selfS430);
  _M0L6_2atmpS1418 = Moonbit_array_length(_M0L6_2atmpS1419);
  moonbit_decref_cycle_free(_M0L6_2atmpS1419);
  if (_M0L3lenS1417 == _M0L6_2atmpS1418) {
    int32_t _M0L3lenS1421 = _M0L4selfS430->$1;
    int32_t _M0L6_2atmpS1420 = _M0L3lenS1421 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS430, _M0L6_2atmpS1420);
  }
  _M0L6lengthS431 = _M0L4selfS430->$1;
  _M0L3bufS1422 = _M0L4selfS430->$0;
  _M0L6_2aoldS2385 = (moonbit_string_t)_M0L3bufS1422[_M0L6lengthS431];
  moonbit_decref_cycle_free(_M0L6_2aoldS2385);
  _M0L3bufS1422[_M0L6lengthS431] = _M0L5valueS432;
  _M0L6_2atmpS1423 = _M0L6lengthS431 + 1;
  _M0L4selfS430->$1 = _M0L6_2atmpS1423;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS433,
  struct _M0TUsiE* _M0L5valueS435
) {
  int32_t _M0L3lenS1424;
  struct _M0TUsiE** _M0L6_2atmpS1426;
  int32_t _M0L6_2atmpS1425;
  int32_t _M0L6lengthS434;
  struct _M0TUsiE** _M0L3bufS1429;
  struct _M0TUsiE* _M0L6_2aoldS2386;
  int32_t _M0L6_2atmpS1430;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1424 = _M0L4selfS433->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1426 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS433);
  _M0L6_2atmpS1425 = Moonbit_array_length(_M0L6_2atmpS1426);
  moonbit_decref_cycle_free(_M0L6_2atmpS1426);
  if (_M0L3lenS1424 == _M0L6_2atmpS1425) {
    int32_t _M0L3lenS1428 = _M0L4selfS433->$1;
    int32_t _M0L6_2atmpS1427 = _M0L3lenS1428 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS433, _M0L6_2atmpS1427);
  }
  _M0L6lengthS434 = _M0L4selfS433->$1;
  _M0L3bufS1429 = _M0L4selfS433->$0;
  _M0L6_2aoldS2386 = (struct _M0TUsiE*)_M0L3bufS1429[_M0L6lengthS434];
  if (_M0L6_2aoldS2386) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2386);
  }
  _M0L3bufS1429[_M0L6lengthS434] = _M0L5valueS435;
  _M0L6_2atmpS1430 = _M0L6lengthS434 + 1;
  _M0L4selfS433->$1 = _M0L6_2atmpS1430;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS423,
  int32_t _M0L8requiredS425
) {
  int32_t _M0L8old__capS422;
  int32_t _M0L3lenS1415;
  int32_t _M0L8new__capS424;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS422 = _M0MPC15array5Array8capacityGsE(_M0L4selfS423);
  _M0L3lenS1415 = _M0L4selfS423->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS424
  = _M0FPB23array__growth__capacity(_M0L8old__capS422, _M0L3lenS1415, _M0L8requiredS425);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS423, _M0L8new__capS424);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS427,
  int32_t _M0L8requiredS429
) {
  int32_t _M0L8old__capS426;
  int32_t _M0L3lenS1416;
  int32_t _M0L8new__capS428;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS426 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS427);
  _M0L3lenS1416 = _M0L4selfS427->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS428
  = _M0FPB23array__growth__capacity(_M0L8old__capS426, _M0L3lenS1416, _M0L8requiredS429);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS427, _M0L8new__capS428);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS411,
  int32_t _M0L13new__capacityS414
) {
  moonbit_string_t* _M0L8old__bufS410;
  int32_t _M0L3lenS412;
  int32_t _M0L9copy__lenS413;
  moonbit_string_t* _M0L8new__bufS415;
  moonbit_string_t* _M0L6_2aoldS2387;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS410 = _M0L4selfS411->$0;
  _M0L3lenS412 = _M0L4selfS411->$1;
  if (_M0L3lenS412 < _M0L13new__capacityS414) {
    _M0L9copy__lenS413 = _M0L3lenS412;
  } else {
    _M0L9copy__lenS413 = _M0L13new__capacityS414;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS410);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS415
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS410, _M0L13new__capacityS414, _M0L9copy__lenS413, 0, 0);
  _M0L6_2aoldS2387 = _M0L4selfS411->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2387);
  _M0L4selfS411->$0 = _M0L8new__bufS415;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS417,
  int32_t _M0L13new__capacityS420
) {
  struct _M0TUsiE** _M0L8old__bufS416;
  int32_t _M0L3lenS418;
  int32_t _M0L9copy__lenS419;
  struct _M0TUsiE** _M0L8new__bufS421;
  struct _M0TUsiE** _M0L6_2aoldS2388;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS416 = _M0L4selfS417->$0;
  _M0L3lenS418 = _M0L4selfS417->$1;
  if (_M0L3lenS418 < _M0L13new__capacityS420) {
    _M0L9copy__lenS419 = _M0L3lenS418;
  } else {
    _M0L9copy__lenS419 = _M0L13new__capacityS420;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS416);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS421
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS416, _M0L13new__capacityS420, _M0L9copy__lenS419, 0, 0);
  _M0L6_2aoldS2388 = _M0L4selfS417->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2388);
  _M0L4selfS417->$0 = _M0L8new__bufS421;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS408
) {
  moonbit_string_t* _M0L6_2atmpS1413;
  int32_t _result_2482;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1413 = _M0MPC15array5Array6bufferGsE(_M0L4selfS408);
  _result_2482 = Moonbit_array_length(_M0L6_2atmpS1413);
  moonbit_decref_cycle_free(_M0L6_2atmpS1413);
  return _result_2482;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS409
) {
  struct _M0TUsiE** _M0L6_2atmpS1414;
  int32_t _result_2483;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1414 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS409);
  _result_2483 = Moonbit_array_length(_M0L6_2atmpS1414);
  moonbit_decref_cycle_free(_M0L6_2atmpS1414);
  return _result_2483;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS404,
  int32_t _M0L3lenS402,
  int32_t _M0L8requiredS401
) {
  int32_t _M0L5startS403;
  int32_t _M0L5spaceS405;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS401 < _M0L3lenS402) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS404 == 0) {
    _M0L5startS403 = 8;
  } else {
    _M0L5startS403 = _M0L7currentS404;
  }
  _M0L5spaceS405 = _M0L5startS403;
  while (1) {
    if (_M0L5spaceS405 < _M0L8requiredS401) {
      int32_t _M0L4nextS406 = _M0L5spaceS405 * 2;
      if (_M0L4nextS406 <= _M0L5spaceS405) {
        return _M0L8requiredS401;
      }
      _M0L5spaceS405 = _M0L4nextS406;
      continue;
    } else {
      return _M0L5spaceS405;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS396) {
  float* _M0L8_2afieldS2389;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2389 = _M0L4selfS396->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2389);
  return _M0L8_2afieldS2389;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS397) {
  uint8_t* _M0L8_2afieldS2390;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2390 = _M0L4selfS397->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2390);
  return _M0L8_2afieldS2390;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS398) {
  int32_t* _M0L8_2afieldS2391;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2391 = _M0L4selfS398->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2391);
  return _M0L8_2afieldS2391;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS399
) {
  moonbit_string_t* _M0L8_2afieldS2392;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2392 = _M0L4selfS399->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2392);
  return _M0L8_2afieldS2392;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS400
) {
  struct _M0TUsiE** _M0L8_2afieldS2393;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2393 = _M0L4selfS400->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2393);
  return _M0L8_2afieldS2393;
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
  int32_t _M0L3endS1411;
  int32_t _M0L5startS1412;
  int32_t _M0L8str__lenS391;
  int32_t _M0L3lenS1410;
  int32_t _M0L8requiredS393;
  uint16_t* _M0L4dataS1403;
  int32_t _M0L6_2atmpS1402;
  int32_t _if__result_2485;
  uint16_t* _M0L4dataS1404;
  int32_t _M0L3lenS1405;
  moonbit_string_t _M0L6_2atmpS1406;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L3lenS1409;
  int32_t _M0L6_2atmpS1408;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1411 = _M0L3strS392.$2;
  _M0L5startS1412 = _M0L3strS392.$1;
  _M0L8str__lenS391 = _M0L3endS1411 - _M0L5startS1412;
  if (_M0L8str__lenS391 == 0) {
    return 0;
  }
  _M0L3lenS1410 = _M0L4selfS394->$1;
  _M0L8requiredS393 = _M0L3lenS1410 + _M0L8str__lenS391;
  _M0L4dataS1403 = _M0L4selfS394->$0;
  _M0L6_2atmpS1402 = Moonbit_array_length(_M0L4dataS1403);
  if (_M0L8requiredS393 > _M0L6_2atmpS1402) {
    _if__result_2485 = 1;
  } else {
    int32_t _M0L3lenS1401 = _M0L4selfS394->$1;
    _if__result_2485 = _M0L8requiredS393 < _M0L3lenS1401;
  }
  if (_if__result_2485) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS394, _M0L8requiredS393);
  }
  _M0L4dataS1404 = _M0L4selfS394->$0;
  _M0L3lenS1405 = _M0L4selfS394->$1;
  moonbit_incref_cycle_free(_M0L4dataS1404);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1406 = _M0MPC16string10StringView4data(_M0L3strS392);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1407 = _M0MPC16string10StringView13start__offset(_M0L3strS392);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1404, _M0L3lenS1405, _M0L6_2atmpS1406, _M0L6_2atmpS1407, _M0L8str__lenS391);
  moonbit_decref_cycle_free(_M0L4dataS1404);
  moonbit_decref_cycle_free(_M0L6_2atmpS1406);
  _M0L3lenS1409 = _M0L4selfS394->$1;
  _M0L6_2atmpS1408 = _M0L3lenS1409 + _M0L8str__lenS391;
  _M0L4selfS394->$1 = _M0L6_2atmpS1408;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS388,
  int32_t _M0L5startS386,
  int32_t _M0L3endS387
) {
  int32_t _if__result_2486;
  int32_t _M0L3lenS389;
  int32_t _M0L6_2atmpS1400;
  moonbit_bytes_t _M0L5bytesS390;
  moonbit_bytes_t _M0L6_2atmpS1399;
  moonbit_string_t _result_2487;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS386 == 0) {
    int32_t _M0L6_2atmpS1398 = Moonbit_array_length(_M0L3strS388);
    _if__result_2486 = _M0L3endS387 == _M0L6_2atmpS1398;
  } else {
    _if__result_2486 = 0;
  }
  if (_if__result_2486) {
    moonbit_incref_cycle_free(_M0L3strS388);
    return _M0L3strS388;
  }
  _M0L3lenS389 = _M0L3endS387 - _M0L5startS386;
  _M0L6_2atmpS1400 = _M0L3lenS389 * 2;
  _M0L5bytesS390 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1400, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS390, 0, _M0L3strS388, _M0L5startS386, _M0L3lenS389);
  _M0L6_2atmpS1399 = _M0L5bytesS390;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2487
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1399, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1399);
  return _result_2487;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS381,
  int32_t _M0L6offsetS385,
  int64_t _M0L6lengthS383
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6lengthS382;
  int32_t _if__result_2488;
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
      int32_t _M0L6_2atmpS1397 = _M0L6offsetS385 + _M0L6lengthS382;
      _if__result_2488 = _M0L6_2atmpS1397 <= _M0L3lenS380;
    } else {
      _if__result_2488 = 0;
    }
  } else {
    _if__result_2488 = 0;
  }
  if (_if__result_2488) {
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
  int32_t _M0L6_2atmpS1396;
  int32_t _M0L6_2atmpS1395;
  int32_t _M0L2e1S366;
  int32_t _M0L6_2atmpS1394;
  int32_t _M0L2e2S369;
  int32_t _M0L4len1S371;
  int32_t _M0L4len2S373;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1396 = _M0L6lengthS368 * 2;
  _M0L6_2atmpS1395 = _M0L13bytes__offsetS367 + _M0L6_2atmpS1396;
  _M0L2e1S366 = _M0L6_2atmpS1395 - 1;
  _M0L6_2atmpS1394 = _M0L11str__offsetS370 + _M0L6lengthS368;
  _M0L2e2S369 = _M0L6_2atmpS1394 - 1;
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
        int32_t _M0L6_2atmpS1391 = _M0L3strS374[_M0L1iS376];
        int32_t _M0L6_2atmpS1390 = (int32_t)_M0L6_2atmpS1391;
        uint32_t _M0L1cS378 = *(uint32_t*)&_M0L6_2atmpS1390;
        uint32_t _M0L6_2atmpS1386 = _M0L1cS378 & 255u;
        int32_t _M0L6_2atmpS1385;
        int32_t _M0L6_2atmpS1387;
        uint32_t _M0L6_2atmpS1389;
        int32_t _M0L6_2atmpS1388;
        int32_t _M0L6_2atmpS1392;
        int32_t _M0L6_2atmpS1393;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1385 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1386);
        if (
          _M0L1jS377 < 0 || _M0L1jS377 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L1jS377] = _M0L6_2atmpS1385;
        _M0L6_2atmpS1387 = _M0L1jS377 + 1;
        _M0L6_2atmpS1389 = _M0L1cS378 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1388 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1389);
        if (
          _M0L6_2atmpS1387 < 0
          || _M0L6_2atmpS1387 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L6_2atmpS1387] = _M0L6_2atmpS1388;
        _M0L6_2atmpS1392 = _M0L1iS376 + 1;
        _M0L6_2atmpS1393 = _M0L1jS377 + 2;
        _M0L1iS376 = _M0L6_2atmpS1392;
        _M0L1jS377 = _M0L6_2atmpS1393;
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
  int32_t _M0L6_2atmpS1384;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1384 = *(int32_t*)&_M0L4selfS365;
  return _M0L6_2atmpS1384 & 0xff;
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
    int64_t _M0L6_2atmpS1383 = -_M0L4selfS340;
    _M0L3numS342 = *(uint64_t*)&_M0L6_2atmpS1383;
  } else {
    _M0L3numS342 = *(uint64_t*)&_M0L4selfS340;
  }
  switch (_M0L5radixS339) {
    case 10: {
      int32_t _M0L10digit__lenS344;
      int32_t _M0L6_2atmpS1380;
      int32_t _M0L10total__lenS345;
      uint16_t* _M0L6bufferS346;
      int32_t _M0L12digit__startS347;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS344 = _M0FPB12dec__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1380 = 1;
      } else {
        _M0L6_2atmpS1380 = 0;
      }
      _M0L10total__lenS345 = _M0L10digit__lenS344 + _M0L6_2atmpS1380;
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
      int32_t _M0L6_2atmpS1381;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12hex__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1381 = 1;
      } else {
        _M0L6_2atmpS1381 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1381;
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
      int32_t _M0L6_2atmpS1382;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352
      = _M0FPB14radix__count64(_M0L3numS342, _M0L5radixS339);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1382 = 1;
      } else {
        _M0L6_2atmpS1382 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1382;
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
  int32_t _M0L6_2atmpS1379;
  uint64_t _M0L3numS315;
  int32_t _M0L6offsetS316;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1379 = _M0L10total__lenS338 - _M0L12digit__startS326;
  _M0L3numS315 = _M0L3numS337;
  _M0L6offsetS316 = _M0L6_2atmpS1379;
  while (1) {
    if (_M0L3numS315 >= 10000ull) {
      uint64_t _M0L1tS317 = _M0L3numS315 / 10000ull;
      uint64_t _M0L6_2atmpS1356 = _M0L3numS315 % 10000ull;
      int32_t _M0L1rS318 = (int32_t)_M0L6_2atmpS1356;
      int32_t _M0L2d1S319 = _M0L1rS318 / 100;
      int32_t _M0L2d2S320 = _M0L1rS318 % 100;
      int32_t _M0L6_2atmpS1355 = _M0L2d1S319 / 10;
      int32_t _M0L6_2atmpS1354 = 48 + _M0L6_2atmpS1355;
      int32_t _M0L6d1__hiS321 = (uint16_t)_M0L6_2atmpS1354;
      int32_t _M0L6_2atmpS1353 = _M0L2d1S319 % 10;
      int32_t _M0L6_2atmpS1352 = 48 + _M0L6_2atmpS1353;
      int32_t _M0L6d1__loS322 = (uint16_t)_M0L6_2atmpS1352;
      int32_t _M0L6_2atmpS1351 = _M0L2d2S320 / 10;
      int32_t _M0L6_2atmpS1350 = 48 + _M0L6_2atmpS1351;
      int32_t _M0L6d2__hiS323 = (uint16_t)_M0L6_2atmpS1350;
      int32_t _M0L6_2atmpS1349 = _M0L2d2S320 % 10;
      int32_t _M0L6_2atmpS1348 = 48 + _M0L6_2atmpS1349;
      int32_t _M0L6d2__loS324 = (uint16_t)_M0L6_2atmpS1348;
      int32_t _M0L6_2atmpS1340 = _M0L12digit__startS326 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1339 = _M0L6_2atmpS1340 - 4;
      int32_t _M0L6_2atmpS1342;
      int32_t _M0L6_2atmpS1341;
      int32_t _M0L6_2atmpS1344;
      int32_t _M0L6_2atmpS1343;
      int32_t _M0L6_2atmpS1346;
      int32_t _M0L6_2atmpS1345;
      int32_t _M0L6_2atmpS1347;
      _M0L6bufferS325[_M0L6_2atmpS1339] = _M0L6d1__hiS321;
      _M0L6_2atmpS1342 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1341 = _M0L6_2atmpS1342 - 3;
      _M0L6bufferS325[_M0L6_2atmpS1341] = _M0L6d1__loS322;
      _M0L6_2atmpS1344 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1343 = _M0L6_2atmpS1344 - 2;
      _M0L6bufferS325[_M0L6_2atmpS1343] = _M0L6d2__hiS323;
      _M0L6_2atmpS1346 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1345 = _M0L6_2atmpS1346 - 1;
      _M0L6bufferS325[_M0L6_2atmpS1345] = _M0L6d2__loS324;
      _M0L6_2atmpS1347 = _M0L6offsetS316 - 4;
      _M0L3numS315 = _M0L1tS317;
      _M0L6offsetS316 = _M0L6_2atmpS1347;
      continue;
    } else {
      int32_t _M0L6_2atmpS1378 = (int32_t)_M0L3numS315;
      int32_t _M0L9remainingS328 = _M0L6_2atmpS1378;
      int32_t _M0L6offsetS329 = _M0L6offsetS316;
      while (1) {
        if (_M0L9remainingS328 >= 100) {
          int32_t _M0L1tS330 = _M0L9remainingS328 / 100;
          int32_t _M0L1dS331 = _M0L9remainingS328 % 100;
          int32_t _M0L6_2atmpS1365 = _M0L1dS331 / 10;
          int32_t _M0L6_2atmpS1364 = 48 + _M0L6_2atmpS1365;
          int32_t _M0L5d__hiS332 = (uint16_t)_M0L6_2atmpS1364;
          int32_t _M0L6_2atmpS1363 = _M0L1dS331 % 10;
          int32_t _M0L6_2atmpS1362 = 48 + _M0L6_2atmpS1363;
          int32_t _M0L5d__loS333 = (uint16_t)_M0L6_2atmpS1362;
          int32_t _M0L6_2atmpS1358 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1357 = _M0L6_2atmpS1358 - 2;
          int32_t _M0L6_2atmpS1360;
          int32_t _M0L6_2atmpS1359;
          int32_t _M0L6_2atmpS1361;
          _M0L6bufferS325[_M0L6_2atmpS1357] = _M0L5d__hiS332;
          _M0L6_2atmpS1360 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1359 = _M0L6_2atmpS1360 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1359] = _M0L5d__loS333;
          _M0L6_2atmpS1361 = _M0L6offsetS329 - 2;
          _M0L9remainingS328 = _M0L1tS330;
          _M0L6offsetS329 = _M0L6_2atmpS1361;
          continue;
        } else if (_M0L9remainingS328 >= 10) {
          int32_t _M0L6_2atmpS1373 = _M0L9remainingS328 / 10;
          int32_t _M0L6_2atmpS1372 = 48 + _M0L6_2atmpS1373;
          int32_t _M0L5d__hiS335 = (uint16_t)_M0L6_2atmpS1372;
          int32_t _M0L6_2atmpS1371 = _M0L9remainingS328 % 10;
          int32_t _M0L6_2atmpS1370 = 48 + _M0L6_2atmpS1371;
          int32_t _M0L5d__loS336 = (uint16_t)_M0L6_2atmpS1370;
          int32_t _M0L6_2atmpS1367 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1366 = _M0L6_2atmpS1367 - 2;
          int32_t _M0L6_2atmpS1369;
          int32_t _M0L6_2atmpS1368;
          _M0L6bufferS325[_M0L6_2atmpS1366] = _M0L5d__hiS335;
          _M0L6_2atmpS1369 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1368 = _M0L6_2atmpS1369 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1368] = _M0L5d__loS336;
        } else {
          int32_t _M0L6_2atmpS1377 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1374 = _M0L6_2atmpS1377 - 1;
          int32_t _M0L6_2atmpS1376 = 48 + _M0L9remainingS328;
          int32_t _M0L6_2atmpS1375 = (uint16_t)_M0L6_2atmpS1376;
          _M0L6bufferS325[_M0L6_2atmpS1374] = _M0L6_2atmpS1375;
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
  int32_t _M0L6_2atmpS1324;
  int32_t _M0L6_2atmpS1323;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS298 = _M0MPC13int3Int10to__uint64(_M0L5radixS299);
  _M0L6_2atmpS1324 = _M0L5radixS299 - 1;
  _M0L6_2atmpS1323 = _M0L5radixS299 & _M0L6_2atmpS1324;
  if (_M0L6_2atmpS1323 == 0) {
    int32_t _M0L5shiftS300;
    uint64_t _M0L4maskS301;
    int32_t _M0L6_2atmpS1331;
    int32_t _M0L6offsetS302;
    uint64_t _M0L1nS303;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS300 = moonbit_ctz32(_M0L5radixS299);
    _M0L4maskS301 = _M0L4baseS298 - 1ull;
    _M0L6_2atmpS1331 = _M0L10total__lenS308 - _M0L12digit__startS306;
    _M0L6offsetS302 = _M0L6_2atmpS1331;
    _M0L1nS303 = _M0L3numS309;
    while (1) {
      if (_M0L1nS303 > 0ull) {
        uint64_t _M0L6_2atmpS1330 = _M0L1nS303 & _M0L4maskS301;
        int32_t _M0L5digitS304 = (int32_t)_M0L6_2atmpS1330;
        int32_t _M0L6_2atmpS1327 = _M0L12digit__startS306 + _M0L6offsetS302;
        int32_t _M0L6_2atmpS1325 = _M0L6_2atmpS1327 - 1;
        int32_t _M0L6_2atmpS1326 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS304];
        int32_t _M0L6_2atmpS1328;
        uint64_t _M0L6_2atmpS1329;
        _M0L6bufferS305[_M0L6_2atmpS1325] = _M0L6_2atmpS1326;
        _M0L6_2atmpS1328 = _M0L6offsetS302 - 1;
        _M0L6_2atmpS1329 = _M0L1nS303 >> (_M0L5shiftS300 & 63);
        _M0L6offsetS302 = _M0L6_2atmpS1328;
        _M0L1nS303 = _M0L6_2atmpS1329;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1338 = _M0L10total__lenS308 - _M0L12digit__startS306;
    int32_t _M0L6offsetS310 = _M0L6_2atmpS1338;
    uint64_t _M0L1nS311 = _M0L3numS309;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L1qS312 = _M0L1nS311 / _M0L4baseS298;
        uint64_t _M0L6_2atmpS1337 = _M0L1qS312 * _M0L4baseS298;
        uint64_t _M0L6_2atmpS1336 = _M0L1nS311 - _M0L6_2atmpS1337;
        int32_t _M0L5digitS313 = (int32_t)_M0L6_2atmpS1336;
        int32_t _M0L6_2atmpS1334 = _M0L12digit__startS306 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1332 = _M0L6_2atmpS1334 - 1;
        int32_t _M0L6_2atmpS1333 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS313];
        int32_t _M0L6_2atmpS1335;
        _M0L6bufferS305[_M0L6_2atmpS1332] = _M0L6_2atmpS1333;
        _M0L6_2atmpS1335 = _M0L6offsetS310 - 1;
        _M0L6offsetS310 = _M0L6_2atmpS1335;
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
  int32_t _M0L6_2atmpS1322;
  int32_t _M0L6offsetS287;
  uint64_t _M0L1nS288;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1322 = _M0L10total__lenS296 - _M0L12digit__startS293;
  _M0L6offsetS287 = _M0L6_2atmpS1322;
  _M0L1nS288 = _M0L3numS297;
  while (1) {
    if (_M0L6offsetS287 >= 2) {
      uint64_t _M0L6_2atmpS1319 = _M0L1nS288 & 255ull;
      int32_t _M0L9byte__valS289 = (int32_t)_M0L6_2atmpS1319;
      int32_t _M0L2hiS290 = _M0L9byte__valS289 / 16;
      int32_t _M0L2loS291 = _M0L9byte__valS289 % 16;
      int32_t _M0L6_2atmpS1313 = _M0L12digit__startS293 + _M0L6offsetS287;
      int32_t _M0L6_2atmpS1311 = _M0L6_2atmpS1313 - 2;
      int32_t _M0L6_2atmpS1312 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS290];
      int32_t _M0L6_2atmpS1316;
      int32_t _M0L6_2atmpS1314;
      int32_t _M0L6_2atmpS1315;
      int32_t _M0L6_2atmpS1317;
      uint64_t _M0L6_2atmpS1318;
      _M0L6bufferS292[_M0L6_2atmpS1311] = _M0L6_2atmpS1312;
      _M0L6_2atmpS1316 = _M0L12digit__startS293 + _M0L6offsetS287;
      _M0L6_2atmpS1314 = _M0L6_2atmpS1316 - 1;
      _M0L6_2atmpS1315
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS291
      ];
      _M0L6bufferS292[_M0L6_2atmpS1314] = _M0L6_2atmpS1315;
      _M0L6_2atmpS1317 = _M0L6offsetS287 - 2;
      _M0L6_2atmpS1318 = _M0L1nS288 >> 8;
      _M0L6offsetS287 = _M0L6_2atmpS1317;
      _M0L1nS288 = _M0L6_2atmpS1318;
      continue;
    } else if (_M0L6offsetS287 == 1) {
      uint64_t _M0L6_2atmpS1321 = _M0L1nS288 & 15ull;
      int32_t _M0L6nibbleS295 = (int32_t)_M0L6_2atmpS1321;
      int32_t _M0L6_2atmpS1320 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS295];
      _M0L6bufferS292[_M0L12digit__startS293] = _M0L6_2atmpS1320;
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
      uint64_t _M0L6_2atmpS1309 = _M0L3numS284 / _M0L4baseS282;
      int32_t _M0L6_2atmpS1310 = _M0L5countS285 + 1;
      _M0L3numS284 = _M0L6_2atmpS1309;
      _M0L5countS285 = _M0L6_2atmpS1310;
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
    int32_t _M0L6_2atmpS1308;
    int32_t _M0L6_2atmpS1307;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS280 = moonbit_clz64(_M0L5valueS279);
    _M0L6_2atmpS1308 = 63 - _M0L14leading__zerosS280;
    _M0L6_2atmpS1307 = _M0L6_2atmpS1308 / 4;
    return _M0L6_2atmpS1307 + 1;
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
    int32_t _M0L6_2atmpS1306 = -_M0L4selfS262;
    _M0L3numS264 = *(uint32_t*)&_M0L6_2atmpS1306;
  } else {
    _M0L3numS264 = *(uint32_t*)&_M0L4selfS262;
  }
  switch (_M0L5radixS261) {
    case 10: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12dec__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1303 = 1;
      } else {
        _M0L6_2atmpS1303 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1303;
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
      int32_t _M0L6_2atmpS1304;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12hex__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1304 = 1;
      } else {
        _M0L6_2atmpS1304 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1304;
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
      int32_t _M0L6_2atmpS1305;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274
      = _M0FPB14radix__count32(_M0L3numS264, _M0L5radixS261);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1305 = 1;
      } else {
        _M0L6_2atmpS1305 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1305;
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
      uint32_t _M0L6_2atmpS1301 = _M0L3numS258 / _M0L4baseS256;
      int32_t _M0L6_2atmpS1302 = _M0L5countS259 + 1;
      _M0L3numS258 = _M0L6_2atmpS1301;
      _M0L5countS259 = _M0L6_2atmpS1302;
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
    int32_t _M0L6_2atmpS1300;
    int32_t _M0L6_2atmpS1299;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS254 = moonbit_clz32(_M0L5valueS253);
    _M0L6_2atmpS1300 = 31 - _M0L14leading__zerosS254;
    _M0L6_2atmpS1299 = _M0L6_2atmpS1300 / 4;
    return _M0L6_2atmpS1299 + 1;
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
  int32_t _M0L6_2atmpS1298;
  uint32_t _M0L3numS228;
  int32_t _M0L6offsetS229;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1298 = _M0L10total__lenS251 - _M0L12digit__startS239;
  _M0L3numS228 = _M0L3numS250;
  _M0L6offsetS229 = _M0L6_2atmpS1298;
  while (1) {
    if (_M0L3numS228 >= 10000u) {
      uint32_t _M0L1tS230 = _M0L3numS228 / 10000u;
      uint32_t _M0L6_2atmpS1275 = _M0L3numS228 % 10000u;
      int32_t _M0L1rS231 = *(int32_t*)&_M0L6_2atmpS1275;
      int32_t _M0L2d1S232 = _M0L1rS231 / 100;
      int32_t _M0L2d2S233 = _M0L1rS231 % 100;
      int32_t _M0L6_2atmpS1274 = _M0L2d1S232 / 10;
      int32_t _M0L6_2atmpS1273 = 48 + _M0L6_2atmpS1274;
      int32_t _M0L6d1__hiS234 = (uint16_t)_M0L6_2atmpS1273;
      int32_t _M0L6_2atmpS1272 = _M0L2d1S232 % 10;
      int32_t _M0L6_2atmpS1271 = 48 + _M0L6_2atmpS1272;
      int32_t _M0L6d1__loS235 = (uint16_t)_M0L6_2atmpS1271;
      int32_t _M0L6_2atmpS1270 = _M0L2d2S233 / 10;
      int32_t _M0L6_2atmpS1269 = 48 + _M0L6_2atmpS1270;
      int32_t _M0L6d2__hiS236 = (uint16_t)_M0L6_2atmpS1269;
      int32_t _M0L6_2atmpS1268 = _M0L2d2S233 % 10;
      int32_t _M0L6_2atmpS1267 = 48 + _M0L6_2atmpS1268;
      int32_t _M0L6d2__loS237 = (uint16_t)_M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1259 = _M0L12digit__startS239 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1258 = _M0L6_2atmpS1259 - 4;
      int32_t _M0L6_2atmpS1261;
      int32_t _M0L6_2atmpS1260;
      int32_t _M0L6_2atmpS1263;
      int32_t _M0L6_2atmpS1262;
      int32_t _M0L6_2atmpS1265;
      int32_t _M0L6_2atmpS1264;
      int32_t _M0L6_2atmpS1266;
      _M0L6bufferS238[_M0L6_2atmpS1258] = _M0L6d1__hiS234;
      _M0L6_2atmpS1261 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1260 = _M0L6_2atmpS1261 - 3;
      _M0L6bufferS238[_M0L6_2atmpS1260] = _M0L6d1__loS235;
      _M0L6_2atmpS1263 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1262 = _M0L6_2atmpS1263 - 2;
      _M0L6bufferS238[_M0L6_2atmpS1262] = _M0L6d2__hiS236;
      _M0L6_2atmpS1265 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1264 = _M0L6_2atmpS1265 - 1;
      _M0L6bufferS238[_M0L6_2atmpS1264] = _M0L6d2__loS237;
      _M0L6_2atmpS1266 = _M0L6offsetS229 - 4;
      _M0L3numS228 = _M0L1tS230;
      _M0L6offsetS229 = _M0L6_2atmpS1266;
      continue;
    } else {
      int32_t _M0L6_2atmpS1297 = *(int32_t*)&_M0L3numS228;
      int32_t _M0L9remainingS241 = _M0L6_2atmpS1297;
      int32_t _M0L6offsetS242 = _M0L6offsetS229;
      while (1) {
        if (_M0L9remainingS241 >= 100) {
          int32_t _M0L1tS243 = _M0L9remainingS241 / 100;
          int32_t _M0L1dS244 = _M0L9remainingS241 % 100;
          int32_t _M0L6_2atmpS1284 = _M0L1dS244 / 10;
          int32_t _M0L6_2atmpS1283 = 48 + _M0L6_2atmpS1284;
          int32_t _M0L5d__hiS245 = (uint16_t)_M0L6_2atmpS1283;
          int32_t _M0L6_2atmpS1282 = _M0L1dS244 % 10;
          int32_t _M0L6_2atmpS1281 = 48 + _M0L6_2atmpS1282;
          int32_t _M0L5d__loS246 = (uint16_t)_M0L6_2atmpS1281;
          int32_t _M0L6_2atmpS1277 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1277 - 2;
          int32_t _M0L6_2atmpS1279;
          int32_t _M0L6_2atmpS1278;
          int32_t _M0L6_2atmpS1280;
          _M0L6bufferS238[_M0L6_2atmpS1276] = _M0L5d__hiS245;
          _M0L6_2atmpS1279 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1278 = _M0L6_2atmpS1279 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1278] = _M0L5d__loS246;
          _M0L6_2atmpS1280 = _M0L6offsetS242 - 2;
          _M0L9remainingS241 = _M0L1tS243;
          _M0L6offsetS242 = _M0L6_2atmpS1280;
          continue;
        } else if (_M0L9remainingS241 >= 10) {
          int32_t _M0L6_2atmpS1292 = _M0L9remainingS241 / 10;
          int32_t _M0L6_2atmpS1291 = 48 + _M0L6_2atmpS1292;
          int32_t _M0L5d__hiS248 = (uint16_t)_M0L6_2atmpS1291;
          int32_t _M0L6_2atmpS1290 = _M0L9remainingS241 % 10;
          int32_t _M0L6_2atmpS1289 = 48 + _M0L6_2atmpS1290;
          int32_t _M0L5d__loS249 = (uint16_t)_M0L6_2atmpS1289;
          int32_t _M0L6_2atmpS1286 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1285 = _M0L6_2atmpS1286 - 2;
          int32_t _M0L6_2atmpS1288;
          int32_t _M0L6_2atmpS1287;
          _M0L6bufferS238[_M0L6_2atmpS1285] = _M0L5d__hiS248;
          _M0L6_2atmpS1288 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1287 = _M0L6_2atmpS1288 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1287] = _M0L5d__loS249;
        } else {
          int32_t _M0L6_2atmpS1296 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1293 = _M0L6_2atmpS1296 - 1;
          int32_t _M0L6_2atmpS1295 = 48 + _M0L9remainingS241;
          int32_t _M0L6_2atmpS1294 = (uint16_t)_M0L6_2atmpS1295;
          _M0L6bufferS238[_M0L6_2atmpS1293] = _M0L6_2atmpS1294;
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
  int32_t _M0L6_2atmpS1243;
  int32_t _M0L6_2atmpS1242;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS211 = *(uint32_t*)&_M0L5radixS212;
  _M0L6_2atmpS1243 = _M0L5radixS212 - 1;
  _M0L6_2atmpS1242 = _M0L5radixS212 & _M0L6_2atmpS1243;
  if (_M0L6_2atmpS1242 == 0) {
    int32_t _M0L5shiftS213;
    uint32_t _M0L4maskS214;
    int32_t _M0L6_2atmpS1250;
    int32_t _M0L6offsetS215;
    uint32_t _M0L1nS216;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS213 = moonbit_ctz32(_M0L5radixS212);
    _M0L4maskS214 = _M0L4baseS211 - 1u;
    _M0L6_2atmpS1250 = _M0L10total__lenS221 - _M0L12digit__startS219;
    _M0L6offsetS215 = _M0L6_2atmpS1250;
    _M0L1nS216 = _M0L3numS222;
    while (1) {
      if (_M0L1nS216 > 0u) {
        uint32_t _M0L6_2atmpS1249 = _M0L1nS216 & _M0L4maskS214;
        int32_t _M0L5digitS217 = *(int32_t*)&_M0L6_2atmpS1249;
        int32_t _M0L6_2atmpS1246 = _M0L12digit__startS219 + _M0L6offsetS215;
        int32_t _M0L6_2atmpS1244 = _M0L6_2atmpS1246 - 1;
        int32_t _M0L6_2atmpS1245 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS217];
        int32_t _M0L6_2atmpS1247;
        uint32_t _M0L6_2atmpS1248;
        _M0L6bufferS218[_M0L6_2atmpS1244] = _M0L6_2atmpS1245;
        _M0L6_2atmpS1247 = _M0L6offsetS215 - 1;
        _M0L6_2atmpS1248 = _M0L1nS216 >> (_M0L5shiftS213 & 31);
        _M0L6offsetS215 = _M0L6_2atmpS1247;
        _M0L1nS216 = _M0L6_2atmpS1248;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1257 = _M0L10total__lenS221 - _M0L12digit__startS219;
    int32_t _M0L6offsetS223 = _M0L6_2atmpS1257;
    uint32_t _M0L1nS224 = _M0L3numS222;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L1qS225 = _M0L1nS224 / _M0L4baseS211;
        uint32_t _M0L6_2atmpS1256 = _M0L1qS225 * _M0L4baseS211;
        uint32_t _M0L6_2atmpS1255 = _M0L1nS224 - _M0L6_2atmpS1256;
        int32_t _M0L5digitS226 = *(int32_t*)&_M0L6_2atmpS1255;
        int32_t _M0L6_2atmpS1253 = _M0L12digit__startS219 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1251 = _M0L6_2atmpS1253 - 1;
        int32_t _M0L6_2atmpS1252 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS226];
        int32_t _M0L6_2atmpS1254;
        _M0L6bufferS218[_M0L6_2atmpS1251] = _M0L6_2atmpS1252;
        _M0L6_2atmpS1254 = _M0L6offsetS223 - 1;
        _M0L6offsetS223 = _M0L6_2atmpS1254;
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
  int32_t _M0L6_2atmpS1241;
  int32_t _M0L6offsetS200;
  uint32_t _M0L1nS201;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1241 = _M0L10total__lenS209 - _M0L12digit__startS206;
  _M0L6offsetS200 = _M0L6_2atmpS1241;
  _M0L1nS201 = _M0L3numS210;
  while (1) {
    if (_M0L6offsetS200 >= 2) {
      uint32_t _M0L6_2atmpS1238 = _M0L1nS201 & 255u;
      int32_t _M0L9byte__valS202 = *(int32_t*)&_M0L6_2atmpS1238;
      int32_t _M0L2hiS203 = _M0L9byte__valS202 / 16;
      int32_t _M0L2loS204 = _M0L9byte__valS202 % 16;
      int32_t _M0L6_2atmpS1232 = _M0L12digit__startS206 + _M0L6offsetS200;
      int32_t _M0L6_2atmpS1230 = _M0L6_2atmpS1232 - 2;
      int32_t _M0L6_2atmpS1231 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS203];
      int32_t _M0L6_2atmpS1235;
      int32_t _M0L6_2atmpS1233;
      int32_t _M0L6_2atmpS1234;
      int32_t _M0L6_2atmpS1236;
      uint32_t _M0L6_2atmpS1237;
      _M0L6bufferS205[_M0L6_2atmpS1230] = _M0L6_2atmpS1231;
      _M0L6_2atmpS1235 = _M0L12digit__startS206 + _M0L6offsetS200;
      _M0L6_2atmpS1233 = _M0L6_2atmpS1235 - 1;
      _M0L6_2atmpS1234
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS204
      ];
      _M0L6bufferS205[_M0L6_2atmpS1233] = _M0L6_2atmpS1234;
      _M0L6_2atmpS1236 = _M0L6offsetS200 - 2;
      _M0L6_2atmpS1237 = _M0L1nS201 >> 8;
      _M0L6offsetS200 = _M0L6_2atmpS1236;
      _M0L1nS201 = _M0L6_2atmpS1237;
      continue;
    } else if (_M0L6offsetS200 == 1) {
      uint32_t _M0L6_2atmpS1240 = _M0L1nS201 & 15u;
      int32_t _M0L6nibbleS208 = *(int32_t*)&_M0L6_2atmpS1240;
      int32_t _M0L6_2atmpS1239 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS208];
      _M0L6bufferS205[_M0L12digit__startS206] = _M0L6_2atmpS1239;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS199
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS198;
  struct _M0TPB6Logger _M0L6_2atmpS1229;
  moonbit_string_t _result_2502;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS198);
  _M0L6_2atmpS1229
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS198
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS199, _M0L6_2atmpS1229);
  if (_M0L6_2atmpS1229.$1) {
    moonbit_decref(_M0L6_2atmpS1229.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2502 = _M0MPB13StringBuilder10to__string(_M0L6loggerS198);
  moonbit_decref_cycle_free(_M0L6loggerS198);
  return _result_2502;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS193,
  struct _M0TPB6Logger _M0L6loggerS192
) {
  moonbit_string_t _M0L6_2atmpS1226;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1226 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS193);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192.$0->$method_0(_M0L6loggerS192.$1, _M0L6_2atmpS1226);
  moonbit_decref_cycle_free(_M0L6_2atmpS1226);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1227;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1227 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1227);
  moonbit_decref_cycle_free(_M0L6_2atmpS1227);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1228;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1228 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1228);
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
  moonbit_string_t _M0L8_2afieldS2394;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2394 = _M0L4selfS190.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2394);
  return _M0L8_2afieldS2394;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L5valueS187,
  int32_t _M0L5startS188,
  int32_t _M0L3lenS189
) {
  int32_t _M0L6_2atmpS1225;
  int64_t _M0L6_2atmpS1224;
  struct _M0TPC16string10StringView _M0L6_2atmpS1223;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1225 = _M0L5startS188 + _M0L3lenS189;
  _M0L6_2atmpS1224 = (int64_t)_M0L6_2atmpS1225;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1223
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS187, _M0L5startS188, _M0L6_2atmpS1224);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS186, _M0L6_2atmpS1223);
  moonbit_decref_cycle_free(_M0L6_2atmpS1223.$0);
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
  int32_t _M0L6_2atmpS1207;
  int32_t _if__result_2503;
  int32_t _M0L6_2atmpS1215;
  int32_t _if__result_2504;
  int32_t _M0L6_2atmpS1217;
  int32_t _M0L6_2atmpS1218;
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
  _M0L6_2atmpS1207 = _M0Lm2loS180;
  if (_M0L6_2atmpS1207 > 0) {
    int32_t _M0L6_2atmpS1206 = _M0Lm2loS180;
    if (_M0L6_2atmpS1206 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1205 = _M0Lm2loS180;
      int32_t _M0L6_2atmpS1204 = _M0L4selfS179[_M0L6_2atmpS1205];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1204)) {
        int32_t _M0L6_2atmpS1203 = _M0Lm2loS180;
        int32_t _M0L6_2atmpS1202 = _M0L6_2atmpS1203 - 1;
        int32_t _M0L6_2atmpS1201 = _M0L4selfS179[_M0L6_2atmpS1202];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2503
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1201);
      } else {
        _if__result_2503 = 0;
      }
    } else {
      _if__result_2503 = 0;
    }
  } else {
    _if__result_2503 = 0;
  }
  if (_if__result_2503) {
    int32_t _M0L6_2atmpS1208 = _M0Lm2loS180;
    _M0Lm2loS180 = _M0L6_2atmpS1208 + 1;
  }
  _M0L6_2atmpS1215 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1215 > 0) {
    int32_t _M0L6_2atmpS1214 = _M0Lm2hiS182;
    if (_M0L6_2atmpS1214 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1213 = _M0Lm2hiS182;
      int32_t _M0L6_2atmpS1212 = _M0L4selfS179[_M0L6_2atmpS1213];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1212)) {
        int32_t _M0L6_2atmpS1211 = _M0Lm2hiS182;
        int32_t _M0L6_2atmpS1210 = _M0L6_2atmpS1211 - 1;
        int32_t _M0L6_2atmpS1209 = _M0L4selfS179[_M0L6_2atmpS1210];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2504
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1209);
      } else {
        _if__result_2504 = 0;
      }
    } else {
      _if__result_2504 = 0;
    }
  } else {
    _if__result_2504 = 0;
  }
  if (_if__result_2504) {
    int32_t _M0L6_2atmpS1216 = _M0Lm2hiS182;
    _M0Lm2hiS182 = _M0L6_2atmpS1216 - 1;
  }
  _M0L6_2atmpS1217 = _M0Lm2loS180;
  _M0L6_2atmpS1218 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1217 >= _M0L6_2atmpS1218) {
    int32_t _M0L6_2atmpS1219 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1220 = _M0Lm2loS180;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1219,
                                                 .$2 = _M0L6_2atmpS1220};
  } else {
    int32_t _M0L6_2atmpS1221 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1222 = _M0Lm2hiS182;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1221,
                                                 .$2 = _M0L6_2atmpS1222};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS1200;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS1200
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS1200);
  if (_M0L6_2atmpS1200.$1) {
    moonbit_decref(_M0L6_2atmpS1200.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS175,
  struct _M0TPB4Show _M0L4showS174
) {
  struct _M0TPB6Logger _M0L6_2atmpS1199;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS1199
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS175
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS174.$0->$method_0(_M0L4showS174.$1, _M0L6_2atmpS1199);
  if (_M0L6_2atmpS1199.$1) {
    moonbit_decref(_M0L6_2atmpS1199.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS173) {
  int64_t _M0L6_2atmpS1198;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1198 = (int64_t)_M0L4selfS173;
  return *(uint64_t*)&_M0L6_2atmpS1198;
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
  int32_t _M0L6_2atmpS1197;
  struct _M0TPC16string10StringView _M0L6_2atmpS1195;
  struct _M0TPB6Logger _M0L6_2atmpS1196;
  moonbit_string_t _result_2505;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1197 = Moonbit_array_length(_M0L4selfS171);
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS1195
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS171, .$1 = 0, .$2 = _M0L6_2atmpS1197
  };
  moonbit_incref_cycle_free(_M0L3bufS170);
  _M0L6_2atmpS1196
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS170
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1195, _M0L6_2atmpS1196, _M0L5quoteS172);
  moonbit_decref_cycle_free(_M0L6_2atmpS1195.$0);
  if (_M0L6_2atmpS1196.$1) {
    moonbit_decref(_M0L6_2atmpS1196.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2505 = _M0MPB13StringBuilder10to__string(_M0L3bufS170);
  moonbit_decref_cycle_free(_M0L3bufS170);
  return _result_2505;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS162,
  struct _M0TPB6Logger _M0L6loggerS160,
  int32_t _M0L5quoteS159
) {
  int32_t _M0L3endS1193;
  int32_t _M0L5startS1194;
  int32_t _M0L3lenS161;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS163;
  int32_t _M0L1iS164;
  int32_t _M0L3segS165;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS159) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  _M0L3endS1193 = _M0L4selfS162.$2;
  _M0L5startS1194 = _M0L4selfS162.$1;
  _M0L3lenS161 = _M0L3endS1193 - _M0L5startS1194;
  moonbit_incref_cycle_free(_M0L4selfS162.$0);
  if (_M0L6loggerS160.$1) {
    moonbit_incref(_M0L6loggerS160.$1);
  }
  _M0L6_2aenvS163
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _M0L6_2aenvS163->$0 = _M0L4selfS162;
  _M0L6_2aenvS163->$1 = _M0L6loggerS160;
  _M0L1iS164 = 0;
  _M0L3segS165 = 0;
  _2afor_166:;
  while (1) {
    moonbit_string_t _M0L3strS1190;
    int32_t _M0L5startS1192;
    int32_t _M0L6_2atmpS1191;
    int32_t _M0L4codeS167;
    int32_t _M0L1cS169;
    int32_t _M0L6_2atmpS1174;
    int32_t _M0L6_2atmpS1175;
    int32_t _M0L6_2atmpS1176;
    if (_M0L1iS164 >= _M0L3lenS161) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
      moonbit_decref_cycle_free(_M0L6_2aenvS163);
      break;
    }
    _M0L3strS1190 = _M0L4selfS162.$0;
    _M0L5startS1192 = _M0L4selfS162.$1;
    _M0L6_2atmpS1191 = _M0L5startS1192 + _M0L1iS164;
    _M0L4codeS167 = _M0L3strS1190[_M0L6_2atmpS1191];
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
        int32_t _M0L6_2atmpS1177;
        int32_t _M0L6_2atmpS1178;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1177 = _M0L1iS164 + 1;
        _M0L6_2atmpS1178 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1177;
        _M0L3segS165 = _M0L6_2atmpS1178;
        goto _2afor_166;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1179;
        int32_t _M0L6_2atmpS1180;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1179 = _M0L1iS164 + 1;
        _M0L6_2atmpS1180 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1179;
        _M0L3segS165 = _M0L6_2atmpS1180;
        goto _2afor_166;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1181;
        int32_t _M0L6_2atmpS1182;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1181 = _M0L1iS164 + 1;
        _M0L6_2atmpS1182 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1181;
        _M0L3segS165 = _M0L6_2atmpS1182;
        goto _2afor_166;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1183;
        int32_t _M0L6_2atmpS1184;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1183 = _M0L1iS164 + 1;
        _M0L6_2atmpS1184 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1183;
        _M0L3segS165 = _M0L6_2atmpS1184;
        goto _2afor_166;
        break;
      }
      default: {
        if (_M0L4codeS167 < 32) {
          int32_t _M0L6_2atmpS1186;
          moonbit_string_t _M0L6_2atmpS1185;
          int32_t _M0L6_2atmpS1187;
          int32_t _M0L6_2atmpS1188;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1186 = _M0L4codeS167 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1185 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1186);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, _M0L6_2atmpS1185);
          moonbit_decref_cycle_free(_M0L6_2atmpS1185);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1187 = _M0L1iS164 + 1;
          _M0L6_2atmpS1188 = _M0L1iS164 + 1;
          _M0L1iS164 = _M0L6_2atmpS1187;
          _M0L3segS165 = _M0L6_2atmpS1188;
          goto _2afor_166;
        } else {
          int32_t _M0L6_2atmpS1189 = _M0L1iS164 + 1;
          int32_t _tmp_2508 = _M0L3segS165;
          _M0L1iS164 = _M0L6_2atmpS1189;
          _M0L3segS165 = _tmp_2508;
          goto _2afor_166;
        }
        break;
      }
    }
    goto joinlet_2507;
    join_168:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1174 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS169);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, _M0L6_2atmpS1174);
    _M0L6_2atmpS1175 = _M0L1iS164 + 1;
    _M0L6_2atmpS1176 = _M0L1iS164 + 1;
    _M0L1iS164 = _M0L6_2atmpS1175;
    _M0L3segS165 = _M0L6_2atmpS1176;
    continue;
    joinlet_2507:;
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
    int64_t _M0L6_2atmpS1173 = (int64_t)_M0L1iS157;
    struct _M0TPC16string10StringView _M0L6_2atmpS1172;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1172
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS156, _M0L3segS158, _M0L6_2atmpS1173);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS154.$0->$method_2(_M0L6loggerS154.$1, _M0L6_2atmpS1172);
    moonbit_decref_cycle_free(_M0L6_2atmpS1172.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS145,
  int32_t _M0L5startS147,
  int64_t _M0L3endS149
) {
  int32_t _M0L3endS1170;
  int32_t _M0L5startS1171;
  int32_t _M0L3lenS144;
  int32_t _M0Lm2loS146;
  int32_t _M0Lm2hiS148;
  moonbit_string_t _M0L3strS152;
  int32_t _M0L4baseS153;
  int32_t _M0L6_2atmpS1148;
  int32_t _if__result_2509;
  int32_t _M0L6_2atmpS1158;
  int32_t _if__result_2510;
  int32_t _M0L6_2atmpS1160;
  int32_t _M0L6_2atmpS1161;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1170 = _M0L4selfS145.$2;
  _M0L5startS1171 = _M0L4selfS145.$1;
  _M0L3lenS144 = _M0L3endS1170 - _M0L5startS1171;
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
  _M0L6_2atmpS1148 = _M0Lm2loS146;
  if (_M0L6_2atmpS1148 > 0) {
    int32_t _M0L6_2atmpS1147 = _M0Lm2loS146;
    if (_M0L6_2atmpS1147 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1146 = _M0Lm2loS146;
      int32_t _M0L6_2atmpS1145 = _M0L4baseS153 + _M0L6_2atmpS1146;
      int32_t _M0L6_2atmpS1144 = _M0L3strS152[_M0L6_2atmpS1145];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1144)) {
        int32_t _M0L6_2atmpS1143 = _M0Lm2loS146;
        int32_t _M0L6_2atmpS1142 = _M0L4baseS153 + _M0L6_2atmpS1143;
        int32_t _M0L6_2atmpS1141 = _M0L6_2atmpS1142 - 1;
        int32_t _M0L6_2atmpS1140 = _M0L3strS152[_M0L6_2atmpS1141];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2509
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1140);
      } else {
        _if__result_2509 = 0;
      }
    } else {
      _if__result_2509 = 0;
    }
  } else {
    _if__result_2509 = 0;
  }
  if (_if__result_2509) {
    int32_t _M0L6_2atmpS1149 = _M0Lm2loS146;
    _M0Lm2loS146 = _M0L6_2atmpS1149 + 1;
  }
  _M0L6_2atmpS1158 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1158 > 0) {
    int32_t _M0L6_2atmpS1157 = _M0Lm2hiS148;
    if (_M0L6_2atmpS1157 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1156 = _M0Lm2hiS148;
      int32_t _M0L6_2atmpS1155 = _M0L4baseS153 + _M0L6_2atmpS1156;
      int32_t _M0L6_2atmpS1154 = _M0L3strS152[_M0L6_2atmpS1155];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1154)) {
        int32_t _M0L6_2atmpS1153 = _M0Lm2hiS148;
        int32_t _M0L6_2atmpS1152 = _M0L4baseS153 + _M0L6_2atmpS1153;
        int32_t _M0L6_2atmpS1151 = _M0L6_2atmpS1152 - 1;
        int32_t _M0L6_2atmpS1150 = _M0L3strS152[_M0L6_2atmpS1151];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2510
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1150);
      } else {
        _if__result_2510 = 0;
      }
    } else {
      _if__result_2510 = 0;
    }
  } else {
    _if__result_2510 = 0;
  }
  if (_if__result_2510) {
    int32_t _M0L6_2atmpS1159 = _M0Lm2hiS148;
    _M0Lm2hiS148 = _M0L6_2atmpS1159 - 1;
  }
  _M0L6_2atmpS1160 = _M0Lm2loS146;
  _M0L6_2atmpS1161 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1160 >= _M0L6_2atmpS1161) {
    int32_t _M0L6_2atmpS1165 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1162 = _M0L4baseS153 + _M0L6_2atmpS1165;
    int32_t _M0L6_2atmpS1164 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1163 = _M0L4baseS153 + _M0L6_2atmpS1164;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1162,
                                                 .$2 = _M0L6_2atmpS1163};
  } else {
    int32_t _M0L6_2atmpS1169 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1166 = _M0L4baseS153 + _M0L6_2atmpS1169;
    int32_t _M0L6_2atmpS1168 = _M0Lm2hiS148;
    int32_t _M0L6_2atmpS1167 = _M0L4baseS153 + _M0L6_2atmpS1168;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1166,
                                                 .$2 = _M0L6_2atmpS1167};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS143) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS142;
  int32_t _M0L6_2atmpS1137;
  int32_t _M0L6_2atmpS1136;
  int32_t _M0L6_2atmpS1139;
  int32_t _M0L6_2atmpS1138;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1135;
  moonbit_string_t _result_2511;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS142 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1137 = _M0IPC14byte4BytePB3Div3div(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1136
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1137);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1136);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1139 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1138
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1139);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1138);
  _M0L6_2atmpS1135 = _M0L7_2aselfS142;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2511 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1135);
  moonbit_decref_cycle_free(_M0L6_2atmpS1135);
  return _result_2511;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS141) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS141 < 10) {
    int32_t _M0L6_2atmpS1132;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1132 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1132);
  } else {
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1133;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1134 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1133 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1134, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1133);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS1130;
  int32_t _M0L6_2atmpS1131;
  int32_t _M0L6_2atmpS1129;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1130 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS1131 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS1129 = _M0L6_2atmpS1130 - _M0L6_2atmpS1131;
  return _M0L6_2atmpS1129 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS1127;
  int32_t _M0L6_2atmpS1128;
  int32_t _M0L6_2atmpS1126;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1127 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS1128 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS1126 = _M0L6_2atmpS1127 % _M0L6_2atmpS1128;
  return _M0L6_2atmpS1126 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS1124;
  int32_t _M0L6_2atmpS1125;
  int32_t _M0L6_2atmpS1123;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1124 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS1125 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS1123 = _M0L6_2atmpS1124 / _M0L6_2atmpS1125;
  return _M0L6_2atmpS1123 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS133,
  int32_t _M0L4thatS134
) {
  int32_t _M0L6_2atmpS1121;
  int32_t _M0L6_2atmpS1122;
  int32_t _M0L6_2atmpS1120;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1121 = (int32_t)_M0L4selfS133;
  _M0L6_2atmpS1122 = (int32_t)_M0L4thatS134;
  _M0L6_2atmpS1120 = _M0L6_2atmpS1121 + _M0L6_2atmpS1122;
  return _M0L6_2atmpS1120 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS132) {
  int32_t _M0L6_2atmpS1119;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1119 = (int32_t)_M0L4selfS132;
  return _M0L6_2atmpS1119;
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
  int32_t _M0L3lenS1118;
  int32_t _M0L8requiredS128;
  uint16_t* _M0L4dataS1113;
  int32_t _M0L6_2atmpS1112;
  int32_t _if__result_2512;
  uint16_t* _M0L4dataS1114;
  int32_t _M0L3lenS1115;
  int32_t _M0L3lenS1117;
  int32_t _M0L6_2atmpS1116;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS126 = Moonbit_array_length(_M0L3strS127);
  if (_M0L8str__lenS126 == 0) {
    return 0;
  }
  _M0L3lenS1118 = _M0L4selfS129->$1;
  _M0L8requiredS128 = _M0L3lenS1118 + _M0L8str__lenS126;
  _M0L4dataS1113 = _M0L4selfS129->$0;
  _M0L6_2atmpS1112 = Moonbit_array_length(_M0L4dataS1113);
  if (_M0L8requiredS128 > _M0L6_2atmpS1112) {
    _if__result_2512 = 1;
  } else {
    int32_t _M0L3lenS1111 = _M0L4selfS129->$1;
    _if__result_2512 = _M0L8requiredS128 < _M0L3lenS1111;
  }
  if (_if__result_2512) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS129, _M0L8requiredS128);
  }
  _M0L4dataS1114 = _M0L4selfS129->$0;
  _M0L3lenS1115 = _M0L4selfS129->$1;
  moonbit_incref_cycle_free(_M0L4dataS1114);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1114, _M0L3lenS1115, _M0L3strS127, 0, _M0L8str__lenS126);
  moonbit_decref_cycle_free(_M0L4dataS1114);
  _M0L3lenS1117 = _M0L4selfS129->$1;
  _M0L6_2atmpS1116 = _M0L3lenS1117 + _M0L8str__lenS126;
  _M0L4selfS129->$1 = _M0L6_2atmpS1116;
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
      int32_t _M0L6_2atmpS1108 = _M0L3strS123[_M0L1iS120];
      int32_t _M0L6_2atmpS1109;
      int32_t _M0L6_2atmpS1110;
      _M0L4selfS122[_M0L1jS121] = _M0L6_2atmpS1108;
      _M0L6_2atmpS1109 = _M0L1iS120 + 1;
      _M0L6_2atmpS1110 = _M0L1jS121 + 1;
      _M0L1iS120 = _M0L6_2atmpS1109;
      _M0L1jS121 = _M0L6_2atmpS1110;
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
    int32_t _M0L3lenS1079 = _M0L4selfS115->$1;
    uint16_t* _M0L4dataS1081 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS1080 = Moonbit_array_length(_M0L4dataS1081);
    uint16_t* _M0L4dataS1084;
    int32_t _M0L3lenS1085;
    int32_t _M0L6_2atmpS1086;
    int32_t _M0L3lenS1088;
    int32_t _M0L6_2atmpS1087;
    if (_M0L3lenS1079 >= _M0L6_2atmpS1080) {
      int32_t _M0L3lenS1083 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS1082 = _M0L3lenS1083 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS1082);
    }
    _M0L4dataS1084 = _M0L4selfS115->$0;
    _M0L3lenS1085 = _M0L4selfS115->$1;
    moonbit_incref_cycle_free(_M0L4dataS1084);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1086 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS113);
    if (
      _M0L3lenS1085 < 0
      || _M0L3lenS1085 >= Moonbit_array_length(_M0L4dataS1084)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1084[_M0L3lenS1085] = _M0L6_2atmpS1086;
    moonbit_decref_cycle_free(_M0L4dataS1084);
    _M0L3lenS1088 = _M0L4selfS115->$1;
    _M0L6_2atmpS1087 = _M0L3lenS1088 + 1;
    _M0L4selfS115->$1 = _M0L6_2atmpS1087;
  } else if (_M0L4codeS113 <= 1114111u) {
    uint16_t* _M0L4dataS1092 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS1090 = Moonbit_array_length(_M0L4dataS1092);
    int32_t _M0L3lenS1091 = _M0L4selfS115->$1;
    int32_t _M0L6_2atmpS1089 = _M0L6_2atmpS1090 - _M0L3lenS1091;
    uint32_t _M0L4codeS116;
    uint16_t* _M0L4dataS1095;
    int32_t _M0L3lenS1096;
    uint32_t _M0L6_2atmpS1099;
    uint32_t _M0L6_2atmpS1098;
    int32_t _M0L6_2atmpS1097;
    uint16_t* _M0L4dataS1100;
    int32_t _M0L3lenS1105;
    int32_t _M0L6_2atmpS1101;
    uint32_t _M0L6_2atmpS1104;
    uint32_t _M0L6_2atmpS1103;
    int32_t _M0L6_2atmpS1102;
    int32_t _M0L3lenS1107;
    int32_t _M0L6_2atmpS1106;
    if (_M0L6_2atmpS1089 < 2) {
      int32_t _M0L3lenS1094 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS1093 = _M0L3lenS1094 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS1093);
    }
    _M0L4codeS116 = _M0L4codeS113 - 65536u;
    _M0L4dataS1095 = _M0L4selfS115->$0;
    _M0L3lenS1096 = _M0L4selfS115->$1;
    _M0L6_2atmpS1099 = _M0L4codeS116 >> 10;
    _M0L6_2atmpS1098 = 55296u + _M0L6_2atmpS1099;
    moonbit_incref_cycle_free(_M0L4dataS1095);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1097 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1098);
    if (
      _M0L3lenS1096 < 0
      || _M0L3lenS1096 >= Moonbit_array_length(_M0L4dataS1095)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1095[_M0L3lenS1096] = _M0L6_2atmpS1097;
    moonbit_decref_cycle_free(_M0L4dataS1095);
    _M0L4dataS1100 = _M0L4selfS115->$0;
    _M0L3lenS1105 = _M0L4selfS115->$1;
    _M0L6_2atmpS1101 = _M0L3lenS1105 + 1;
    _M0L6_2atmpS1104 = _M0L4codeS116 & 1023u;
    _M0L6_2atmpS1103 = 56320u + _M0L6_2atmpS1104;
    moonbit_incref_cycle_free(_M0L4dataS1100);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1102 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1103);
    if (
      _M0L6_2atmpS1101 < 0
      || _M0L6_2atmpS1101 >= Moonbit_array_length(_M0L4dataS1100)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1100[_M0L6_2atmpS1101] = _M0L6_2atmpS1102;
    moonbit_decref_cycle_free(_M0L4dataS1100);
    _M0L3lenS1107 = _M0L4selfS115->$1;
    _M0L6_2atmpS1106 = _M0L3lenS1107 + 2;
    _M0L4selfS115->$1 = _M0L6_2atmpS1106;
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
  uint16_t* _M0L4dataS1078;
  int32_t _M0L6_2atmpS1076;
  int32_t _M0L3lenS1077;
  int32_t _M0L13new__capacityS109;
  uint16_t* _M0L4dataS1073;
  int32_t _M0L6_2atmpS1074;
  int32_t _M0L3lenS1075;
  uint16_t* _M0L9new__dataS112;
  uint16_t* _M0L6_2aoldS2395;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1078 = _M0L4selfS110->$0;
  _M0L6_2atmpS1076 = Moonbit_array_length(_M0L4dataS1078);
  _M0L3lenS1077 = _M0L4selfS110->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS109
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1076, _M0L3lenS1077, _M0L8requiredS111);
  _M0L4dataS1073 = _M0L4selfS110->$0;
  moonbit_incref_cycle_free(_M0L4dataS1073);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1074 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1075 = _M0L4selfS110->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS112
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1073, _M0L13new__capacityS109, _M0L6_2atmpS1074, _M0L3lenS1075, 0, 0);
  _M0L6_2aoldS2395 = _M0L4selfS110->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2395);
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
  int32_t _M0L6_2atmpS1072;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1072 = *(int32_t*)&_M0L4selfS102;
  return (uint16_t)_M0L6_2atmpS1072;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS101) {
  int32_t _M0L6_2atmpS1071;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1071 = _M0L4selfS101;
  return *(uint32_t*)&_M0L6_2atmpS1071;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS99
) {
  int32_t _M0L3lenS1062;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1062 = _M0L4selfS99->$1;
  if (_M0L3lenS1062 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1063 = _M0L4selfS99->$1;
    uint16_t* _M0L4dataS1065 = _M0L4selfS99->$0;
    int32_t _M0L6_2atmpS1064 = Moonbit_array_length(_M0L4dataS1065);
    if (_M0L3lenS1063 == _M0L6_2atmpS1064) {
      uint16_t* _M0L4dataS1066 = _M0L4selfS99->$0;
      moonbit_incref_cycle_free(_M0L4dataS1066);
      return _M0L4dataS1066;
    } else {
      uint16_t* _M0L4dataS1067 = _M0L4selfS99->$0;
      int32_t _M0L3lenS1068 = _M0L4selfS99->$1;
      int32_t _M0L6_2atmpS1069;
      int32_t _M0L3lenS1070;
      uint16_t* _M0L4dataS100;
      moonbit_incref_cycle_free(_M0L4dataS1067);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1069 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1070 = _M0L4selfS99->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS100
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1067, _M0L3lenS1068, _M0L6_2atmpS1069, _M0L3lenS1070, 0, 0);
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
  int32_t _if__result_2515;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS1058 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS1059 = Moonbit_array_length(_M0L3srcS96);
          if (_M0L6_2atmpS1058 <= _M0L6_2atmpS1059) {
            int32_t _M0L6_2atmpS1057 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_2515 = _M0L6_2atmpS1057 <= _M0L13allocate__lenS92;
          } else {
            _if__result_2515 = 0;
          }
        } else {
          _if__result_2515 = 0;
        }
      } else {
        _if__result_2515 = 0;
      }
    } else {
      _if__result_2515 = 0;
    }
  } else {
    _if__result_2515 = 0;
  }
  if (_if__result_2515) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L4initS97, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS1061;
    moonbit_string_t _M0L6_2atmpS1060;
    uint16_t* _result_2516;
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
    _M0L6_2atmpS1061 = Moonbit_array_length(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS1061);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1060
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2516 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1060);
    moonbit_decref_cycle_free(_M0L6_2atmpS1060);
    return _result_2516;
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
  struct _M0TPB13StringBuilder* _block_2517;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS83 < 1) {
    _M0L7initialS82 = 1;
  } else {
    int32_t _M0L6_2atmpS1056 = _M0L10size__hintS83 + 1;
    _M0L7initialS82 = _M0L6_2atmpS1056 / 2;
  }
  _M0L4dataS84 = (uint16_t*)moonbit_make_string(_M0L7initialS82, 0);
  _block_2517
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2517)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 81, 0);
  _block_2517->$0 = _M0L4dataS84;
  _block_2517->$1 = 0;
  return _block_2517;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS81) {
  int32_t _M0L6_2atmpS1055;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1055 = (int32_t)_M0L4selfS81;
  return _M0L6_2atmpS1055;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_2518;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS1046 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS1047;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1047
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
          if (_M0L6_2atmpS1046 <= _M0L6_2atmpS1047) {
            int32_t _M0L6_2atmpS1045 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_2518 = _M0L6_2atmpS1045 <= _M0L13allocate__lenS69;
          } else {
            _if__result_2518 = 0;
          }
        } else {
          _if__result_2518 = 0;
        }
      } else {
        _if__result_2518 = 0;
      }
    } else {
      _if__result_2518 = 0;
    }
  } else {
    _if__result_2518 = 0;
  }
  if (_if__result_2518) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS69, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS73, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS1049;
    moonbit_string_t _M0L6_2atmpS1048;
    moonbit_string_t* _result_2519;
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
    _M0L6_2atmpS1049 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
    moonbit_decref_cycle_free(_M0L3srcS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS1049);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1048
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS74);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2519
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1048);
    moonbit_decref_cycle_free(_M0L6_2atmpS1048);
    return _result_2519;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L3lenS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS78
) {
  int32_t _if__result_2520;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS75 >= 0) {
    if (_M0L3lenS76 >= 0) {
      if (_M0L11src__offsetS77 >= 0) {
        if (_M0L11dst__offsetS78 >= 0) {
          int32_t _M0L6_2atmpS1051 = _M0L11src__offsetS77 + _M0L3lenS76;
          int32_t _M0L6_2atmpS1052;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1052
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
          if (_M0L6_2atmpS1051 <= _M0L6_2atmpS1052) {
            int32_t _M0L6_2atmpS1050 = _M0L11dst__offsetS78 + _M0L3lenS76;
            _if__result_2520 = _M0L6_2atmpS1050 <= _M0L13allocate__lenS75;
          } else {
            _if__result_2520 = 0;
          }
        } else {
          _if__result_2520 = 0;
        }
      } else {
        _if__result_2520 = 0;
      }
    } else {
      _if__result_2520 = 0;
    }
  } else {
    _if__result_2520 = 0;
  }
  if (_if__result_2520) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS75, 0, _M0L3srcS79, _M0L11src__offsetS77, _M0L11dst__offsetS78, _M0L3lenS76);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS80;
    int32_t _M0L6_2atmpS1054;
    moonbit_string_t _M0L6_2atmpS1053;
    struct _M0TUsiE** _result_2521;
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
    _M0L6_2atmpS1054 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
    moonbit_decref_cycle_free(_M0L3srcS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L6_2atmpS1054);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1053
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS80);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS80);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2521
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1053);
    moonbit_decref_cycle_free(_M0L6_2atmpS1053);
    return _result_2521;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS1042;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS1042
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS63, _M0L6_2atmpS1042);
  if (_M0L6_2atmpS1042.$1) {
    moonbit_decref(_M0L6_2atmpS1042.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  int32_t _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS1043;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS1043
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS65, _M0L6_2atmpS1043);
  if (_M0L6_2atmpS1043.$1) {
    moonbit_decref(_M0L6_2atmpS1043.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  uint64_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS1044;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS1044
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS67, _M0L6_2atmpS1044);
  if (_M0L6_2atmpS1044.$1) {
    moonbit_decref(_M0L6_2atmpS1044.$1);
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
        int32_t _M0L6_2atmpS1015 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS1017 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS1016;
        int32_t _M0L6_2atmpS1018;
        if (
          _M0L6_2atmpS1017 < 0
          || _M0L6_2atmpS1017 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1016 = (int32_t)_M0L3srcS15[_M0L6_2atmpS1017];
        if (
          _M0L6_2atmpS1015 < 0
          || _M0L6_2atmpS1015 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS1015] = _M0L6_2atmpS1016;
        _M0L6_2atmpS1018 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS1018;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1023 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS1023;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS1019 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS1021 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS1020;
        int32_t _M0L6_2atmpS1022;
        if (
          _M0L6_2atmpS1021 < 0
          || _M0L6_2atmpS1021 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1020 = (int32_t)_M0L3srcS15[_M0L6_2atmpS1021];
        if (
          _M0L6_2atmpS1019 < 0
          || _M0L6_2atmpS1019 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS1019] = _M0L6_2atmpS1020;
        _M0L6_2atmpS1022 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS1022;
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
        int32_t _M0L6_2atmpS1024 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS1026 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS1025;
        moonbit_string_t _M0L6_2aoldS2396;
        int32_t _M0L6_2atmpS1027;
        if (
          _M0L6_2atmpS1026 < 0
          || _M0L6_2atmpS1026 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1025 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS1026];
        if (
          _M0L6_2atmpS1024 < 0
          || _M0L6_2atmpS1024 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2396 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS1024];
        moonbit_incref_cycle_free(_M0L6_2atmpS1025);
        moonbit_decref_cycle_free(_M0L6_2aoldS2396);
        _M0L3dstS23[_M0L6_2atmpS1024] = _M0L6_2atmpS1025;
        _M0L6_2atmpS1027 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS1027;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1032 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS1032;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS1028 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS1030 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS1029;
        moonbit_string_t _M0L6_2aoldS2397;
        int32_t _M0L6_2atmpS1031;
        if (
          _M0L6_2atmpS1030 < 0
          || _M0L6_2atmpS1030 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1029 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS1030];
        if (
          _M0L6_2atmpS1028 < 0
          || _M0L6_2atmpS1028 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2397 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS1028];
        moonbit_incref_cycle_free(_M0L6_2atmpS1029);
        moonbit_decref_cycle_free(_M0L6_2aoldS2397);
        _M0L3dstS23[_M0L6_2atmpS1028] = _M0L6_2atmpS1029;
        _M0L6_2atmpS1031 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS1031;
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
        int32_t _M0L6_2atmpS1033 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS1035 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS1034;
        struct _M0TUsiE* _M0L6_2aoldS2398;
        int32_t _M0L6_2atmpS1036;
        if (
          _M0L6_2atmpS1035 < 0
          || _M0L6_2atmpS1035 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1034 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS1035];
        if (
          _M0L6_2atmpS1033 < 0
          || _M0L6_2atmpS1033 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2398 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS1033];
        if (_M0L6_2atmpS1034) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1034);
        }
        if (_M0L6_2aoldS2398) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2398);
        }
        _M0L3dstS32[_M0L6_2atmpS1033] = _M0L6_2atmpS1034;
        _M0L6_2atmpS1036 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS1036;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1041 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS1041;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS1037 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS1039 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS1038;
        struct _M0TUsiE* _M0L6_2aoldS2399;
        int32_t _M0L6_2atmpS1040;
        if (
          _M0L6_2atmpS1039 < 0
          || _M0L6_2atmpS1039 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1038 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS1039];
        if (
          _M0L6_2atmpS1037 < 0
          || _M0L6_2atmpS1037 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2399 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS1037];
        if (_M0L6_2atmpS1038) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1038);
        }
        if (_M0L6_2aoldS2399) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2399);
        }
        _M0L3dstS32[_M0L6_2atmpS1037] = _M0L6_2atmpS1038;
        _M0L6_2atmpS1040 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS1040;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS986) {
  switch (Moonbit_object_tag(_M0L4_2aeS986)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS986);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
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
  void* _M0L11_2aobj__ptrS1010,
  struct _M0TPB4Show _M0L8_2aparamS1009
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1008 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1010;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1008, _M0L8_2aparamS1009);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1007,
  struct _M0TPB4Show _M0L8_2aparamS1006
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1005 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1007;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1005, _M0L8_2aparamS1006);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1004,
  int32_t _M0L8_2aparamS1003
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1002 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1004;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1002, _M0L8_2aparamS1003);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1001,
  struct _M0TPC16string10StringView _M0L8_2aparamS1000
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS999 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1001;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS999, _M0L8_2aparamS1000);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS998,
  moonbit_string_t _M0L8_2aparamS995,
  int32_t _M0L8_2aparamS996,
  int32_t _M0L8_2aparamS997
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS994 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS998;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS994, _M0L8_2aparamS995, _M0L8_2aparamS996, _M0L8_2aparamS997);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS993,
  moonbit_string_t _M0L8_2aparamS992
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS991 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS993;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS991, _M0L8_2aparamS992);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1014;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS979;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS980;
  int32_t _M0L7_2abindS981;
  struct _M0TUsiE** _M0L7_2abindS982;
  int32_t _M0L6_2acntS2404;
  int32_t _M0L2__S983;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1014
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS979
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _M0L12async__testsS979->$0 = _M0L6_2atmpS1014;
  _M0L12async__testsS979->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS980
  = _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS981 = _M0L7_2abindS980->$1;
  _M0L7_2abindS982 = _M0L7_2abindS980->$0;
  _M0L6_2acntS2404
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS980));
  if (_M0L6_2acntS2404 > 1) {
    int32_t _M0L11_2anew__cntS2405 = _M0L6_2acntS2404 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS980), _M0L11_2anew__cntS2405);
    moonbit_incref_cycle_free(_M0L7_2abindS982);
  } else if (_M0L6_2acntS2404 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS980);
  }
  _M0L2__S983 = 0;
  while (1) {
    if (_M0L2__S983 < _M0L7_2abindS981) {
      struct _M0TUsiE* _M0L3argS984 =
        (struct _M0TUsiE*)_M0L7_2abindS982[_M0L2__S983];
      moonbit_string_t _M0L6_2atmpS1011 = _M0L3argS984->$0;
      int32_t _M0L6_2atmpS1012 = _M0L3argS984->$1;
      int32_t _M0L6_2atmpS1013;
      moonbit_incref_cycle_free(_M0L6_2atmpS1011);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples22tripod__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS979, _M0L6_2atmpS1011, _M0L6_2atmpS1012);
      moonbit_decref_cycle_free(_M0L6_2atmpS1011);
      _M0L6_2atmpS1013 = _M0L2__S983 + 1;
      _M0L2__S983 = _M0L6_2atmpS1013;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS982);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples22tripod__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22tripod__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS979);
  moonbit_decref_cycle_free(_M0L12async__testsS979);
  moonbit_flush_cycles();
  return 0;
}