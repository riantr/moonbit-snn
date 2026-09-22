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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070;

struct _M0TP26RiantR8snn__mbt9TripodHet;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt16AdExParameterHet;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod;

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* $0;
  struct _M0TP26RiantR8snn__mbt9TripodHet* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGbE* $3;
  moonbit_string_t $4;
  moonbit_string_t $5;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $6;
  
};

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1082(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1075(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1070(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1047(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1040(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*
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

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

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
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_29 =
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
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_22 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 78, 111, 
    114, 109, 97, 108, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    99, 117, 114, 114, 101, 110, 116, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_37 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 50, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 103, 108, 117, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_12 =
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
} const moonbit_string_literal_32 =
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
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 115, 111, 
    109, 97, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    99, 117, 114, 114, 101, 110, 116, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1082$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1082
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[114] =
  {
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070, $1)
    / 4
    * 2,
    sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075)
    / 4, 1,
    offsetof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    * 2, sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2635
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1103,
  moonbit_string_t _M0L8filenameS1072,
  int32_t _M0L5indexS1074
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070* _closure_2666;
  struct _M0TWEu* _M0L13handle__startS1070;
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075* _closure_2667;
  struct _M0TWssbEu* _M0L14handle__resultS1075;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1082;
  void* _M0L11_2atry__errS1097;
  struct moonbit_result_0 _tmp_2669;
  int32_t _handle__error__result_2670;
  int32_t _M0L6_2atmpS2623;
  void* _M0L3errS1098;
  moonbit_string_t _M0L4nameS1100;
  struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1101;
  moonbit_string_t _M0L7_2anameS1102;
  int32_t _M0L6_2acntS2660;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1072);
  _closure_2666
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070));
  Moonbit_object_header(_closure_2666)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2666->code
  = &_M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1070;
  _closure_2666->$0 = _M0L5indexS1074;
  _closure_2666->$1 = _M0L8filenameS1072;
  _M0L13handle__startS1070 = (struct _M0TWEu*)_closure_2666;
  moonbit_incref_cycle_free(_M0L8filenameS1072);
  _closure_2667
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075*)moonbit_malloc(sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075));
  Moonbit_object_header(_closure_2667)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2667->code
  = &_M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1075;
  _closure_2667->$0 = _M0L5indexS1074;
  _closure_2667->$1 = _M0L8filenameS1072;
  _M0L14handle__resultS1075 = (struct _M0TWssbEu*)_closure_2667;
  _M0L17error__to__stringS1082
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1082$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2669
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1103, _M0L8filenameS1072, _M0L5indexS1074, _M0L13handle__startS1070, _M0L14handle__resultS1075, _M0L17error__to__stringS1082);
  if (_tmp_2669.tag) {
    int32_t const _M0L5_2aokS2632 = _tmp_2669.data.ok;
    _handle__error__result_2670 = _M0L5_2aokS2632;
  } else {
    void* const _M0L6_2aerrS2633 = _tmp_2669.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
    moonbit_decref_cycle_free(_M0L13handle__startS1070);
    _M0L11_2atry__errS1097 = _M0L6_2aerrS2633;
    goto join_1096;
  }
  if (_handle__error__result_2670) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
    moonbit_decref_cycle_free(_M0L13handle__startS1070);
    _M0L6_2atmpS2623 = 1;
  } else {
    struct moonbit_result_0 _tmp_2671;
    int32_t _handle__error__result_2672;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2671
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1103, _M0L8filenameS1072, _M0L5indexS1074, _M0L13handle__startS1070, _M0L14handle__resultS1075, _M0L17error__to__stringS1082);
    if (_tmp_2671.tag) {
      int32_t const _M0L5_2aokS2630 = _tmp_2671.data.ok;
      _handle__error__result_2672 = _M0L5_2aokS2630;
    } else {
      void* const _M0L6_2aerrS2631 = _tmp_2671.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
      moonbit_decref_cycle_free(_M0L13handle__startS1070);
      _M0L11_2atry__errS1097 = _M0L6_2aerrS2631;
      goto join_1096;
    }
    if (_handle__error__result_2672) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
      moonbit_decref_cycle_free(_M0L13handle__startS1070);
      _M0L6_2atmpS2623 = 1;
    } else {
      struct moonbit_result_0 _tmp_2673;
      int32_t _handle__error__result_2674;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2673
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1103, _M0L8filenameS1072, _M0L5indexS1074, _M0L13handle__startS1070, _M0L14handle__resultS1075, _M0L17error__to__stringS1082);
      if (_tmp_2673.tag) {
        int32_t const _M0L5_2aokS2628 = _tmp_2673.data.ok;
        _handle__error__result_2674 = _M0L5_2aokS2628;
      } else {
        void* const _M0L6_2aerrS2629 = _tmp_2673.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
        moonbit_decref_cycle_free(_M0L13handle__startS1070);
        _M0L11_2atry__errS1097 = _M0L6_2aerrS2629;
        goto join_1096;
      }
      if (_handle__error__result_2674) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
        moonbit_decref_cycle_free(_M0L13handle__startS1070);
        _M0L6_2atmpS2623 = 1;
      } else {
        struct moonbit_result_0 _tmp_2675;
        int32_t _handle__error__result_2676;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2675
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1103, _M0L8filenameS1072, _M0L5indexS1074, _M0L13handle__startS1070, _M0L14handle__resultS1075, _M0L17error__to__stringS1082);
        if (_tmp_2675.tag) {
          int32_t const _M0L5_2aokS2626 = _tmp_2675.data.ok;
          _handle__error__result_2676 = _M0L5_2aokS2626;
        } else {
          void* const _M0L6_2aerrS2627 = _tmp_2675.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
          moonbit_decref_cycle_free(_M0L13handle__startS1070);
          _M0L11_2atry__errS1097 = _M0L6_2aerrS2627;
          goto join_1096;
        }
        if (_handle__error__result_2676) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
          moonbit_decref_cycle_free(_M0L13handle__startS1070);
          _M0L6_2atmpS2623 = 1;
        } else {
          struct moonbit_result_0 _tmp_2677;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2677
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1103, _M0L8filenameS1072, _M0L5indexS1074, _M0L13handle__startS1070, _M0L14handle__resultS1075, _M0L17error__to__stringS1082);
          moonbit_decref_cycle_free(_M0L13handle__startS1070);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1082);
          if (_tmp_2677.tag) {
            int32_t const _M0L5_2aokS2624 = _tmp_2677.data.ok;
            _M0L6_2atmpS2623 = _M0L5_2aokS2624;
          } else {
            void* const _M0L6_2aerrS2625 = _tmp_2677.data.err;
            _M0L11_2atry__errS1097 = _M0L6_2aerrS2625;
            goto join_1096;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2623) {
    void* _M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2634 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2634)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2634)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1097
    = _M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2634;
    goto join_1096;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1075);
  }
  goto joinlet_2668;
  join_1096:;
  _M0L3errS1098 = _M0L11_2atry__errS1097;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1101
  = (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1098;
  _M0L7_2anameS1102 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1101->$0;
  _M0L6_2acntS2660
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1101));
  if (_M0L6_2acntS2660 > 1) {
    int32_t _M0L11_2anew__cntS2661 = _M0L6_2acntS2660 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1101), _M0L11_2anew__cntS2661);
    moonbit_incref_cycle_free(_M0L7_2anameS1102);
  } else if (_M0L6_2acntS2660 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1101);
  }
  _M0L4nameS1100 = _M0L7_2anameS1102;
  goto join_1099;
  goto joinlet_2678;
  join_1099:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1075(_M0L14handle__resultS1075, _M0L4nameS1100, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1075);
  moonbit_decref_cycle_free(_M0L4nameS1100);
  joinlet_2678:;
  joinlet_2668:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1082(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2622,
  void* _M0L3errS1083
) {
  void* _M0L1eS1085;
  moonbit_string_t _M0L1eS1087;
  moonbit_string_t _result_2681;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1083)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1088 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1083;
      moonbit_string_t _M0L4_2aeS1089 = _M0L10_2aFailureS1088->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1089);
      _M0L1eS1087 = _M0L4_2aeS1089;
      goto join_1086;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1090 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1083;
      moonbit_string_t _M0L4_2aeS1091 = _M0L15_2aInspectErrorS1090->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1091);
      _M0L1eS1087 = _M0L4_2aeS1091;
      goto join_1086;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1092 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1083;
      moonbit_string_t _M0L4_2aeS1093 = _M0L16_2aSnapshotErrorS1092->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1093);
      _M0L1eS1087 = _M0L4_2aeS1093;
      goto join_1086;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1094 =
        (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1083;
      moonbit_string_t _M0L4_2aeS1095 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1094->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1095);
      _M0L1eS1087 = _M0L4_2aeS1095;
      goto join_1086;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1083);
      _M0L1eS1085 = _M0L3errS1083;
      goto join_1084;
      break;
    }
  }
  join_1086:;
  return _M0L1eS1087;
  join_1084:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2681 = _M0FP15Error10to__string(_M0L1eS1085);
  moonbit_decref_cycle_free(_M0L1eS1085);
  return _result_2681;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1075(
  struct _M0TWssbEu* _M0L6_2aenvS2619,
  moonbit_string_t _M0L10__testnameS1076,
  moonbit_string_t _M0L7messageS1077,
  int32_t _M0L7skippedS1078
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075* _M0L14_2acasted__envS2620;
  moonbit_string_t _M0L8filenameS1072;
  int32_t _M0L5indexS1074;
  moonbit_string_t _M0L10file__nameS1079;
  moonbit_string_t _M0L7messageS1080;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1081;
  moonbit_string_t _M0L6_2atmpS2621;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2620
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1075*)_M0L6_2aenvS2619;
  _M0L8filenameS1072 = _M0L14_2acasted__envS2620->$1;
  _M0L5indexS1074 = _M0L14_2acasted__envS2620->$0;
  if (!_M0L7skippedS1078 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1079
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1072, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1080
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1077, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1081
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1081, _M0L10file__nameS1079);
  moonbit_decref_cycle_free(_M0L10file__nameS1079);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1081, _M0L5indexS1074);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1081, _M0L7messageS1080);
  moonbit_decref_cycle_free(_M0L7messageS1080);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2621
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1081);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1081);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2621);
  moonbit_decref_cycle_free(_M0L6_2atmpS2621);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1070(
  struct _M0TWEu* _M0L6_2aenvS2616
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070* _M0L14_2acasted__envS2617;
  moonbit_string_t _M0L8filenameS1072;
  int32_t _M0L5indexS1074;
  moonbit_string_t _M0L10file__nameS1071;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1073;
  moonbit_string_t _M0L6_2atmpS2618;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2617
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__current__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1070*)_M0L6_2aenvS2616;
  _M0L8filenameS1072 = _M0L14_2acasted__envS2617->$1;
  _M0L5indexS1074 = _M0L14_2acasted__envS2617->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1071
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1072, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1073
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1073, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1073, _M0L10file__nameS1071);
  moonbit_decref_cycle_free(_M0L10file__nameS1071);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1073, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1073, _M0L5indexS1074);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1073, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2618
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1073);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1073);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2618);
  moonbit_decref_cycle_free(_M0L6_2atmpS2618);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1040;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1047;
  struct _M0TUsiE** _M0L6_2atmpS2615;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1054;
  moonbit_string_t* _M0L9cli__argsS1055;
  moonbit_string_t _M0L6_2atmpS2614;
  moonbit_string_t _M0L6_2atmpS2613;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1056;
  int32_t _M0L7_2abindS1057;
  moonbit_string_t* _M0L7_2abindS1058;
  int32_t _M0L6_2acntS2662;
  int32_t _M0L2__S1059;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1040 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1047 = 0;
  _M0L6_2atmpS2615 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1054
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1054)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1054->$0 = _M0L6_2atmpS2615;
  _M0L16file__and__indexS1054->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1055
  = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1055)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2614 = (moonbit_string_t)_M0L9cli__argsS1055[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2614);
  moonbit_decref_cycle_free(_M0L9cli__argsS1055);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2613
  = _M0MP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2614);
  moonbit_decref_cycle_free(_M0L6_2atmpS2614);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1056
  = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1047(_M0L51moonbit__test__driver__internal__split__mbt__stringS1047, _M0L6_2atmpS2613, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2613);
  _M0L7_2abindS1057 = _M0L10test__argsS1056->$1;
  _M0L7_2abindS1058 = _M0L10test__argsS1056->$0;
  _M0L6_2acntS2662
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1056));
  if (_M0L6_2acntS2662 > 1) {
    int32_t _M0L11_2anew__cntS2663 = _M0L6_2acntS2662 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1056), _M0L11_2anew__cntS2663);
    moonbit_incref_cycle_free(_M0L7_2abindS1058);
  } else if (_M0L6_2acntS2662 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1056);
  }
  _M0L2__S1059 = 0;
  while (1) {
    if (_M0L2__S1059 < _M0L7_2abindS1057) {
      moonbit_string_t _M0L3argS1060 =
        (moonbit_string_t)_M0L7_2abindS1058[_M0L2__S1059];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1061;
      moonbit_string_t _M0L4fileS1062;
      moonbit_string_t _M0L5rangeS1063;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1064;
      moonbit_string_t _M0L6_2atmpS2611;
      int32_t _M0L5startS1065;
      moonbit_string_t _M0L6_2atmpS2610;
      int32_t _M0L3endS1066;
      int32_t _M0L1iS1067;
      int32_t _M0L6_2atmpS2612;
      moonbit_incref_cycle_free(_M0L3argS1060);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1061
      = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1047(_M0L51moonbit__test__driver__internal__split__mbt__stringS1047, _M0L3argS1060, 58);
      moonbit_decref_cycle_free(_M0L3argS1060);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1062
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1061, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1063
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1061, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1061);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1064
      = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1047(_M0L51moonbit__test__driver__internal__split__mbt__stringS1047, _M0L5rangeS1063, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1063);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2611
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1064, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1065
      = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1040(_M0L45moonbit__test__driver__internal__parse__int__S1040, _M0L6_2atmpS2611);
      moonbit_decref_cycle_free(_M0L6_2atmpS2611);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2610
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1064, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1064);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1066
      = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1040(_M0L45moonbit__test__driver__internal__parse__int__S1040, _M0L6_2atmpS2610);
      moonbit_decref_cycle_free(_M0L6_2atmpS2610);
      _M0L1iS1067 = _M0L5startS1065;
      while (1) {
        if (_M0L1iS1067 < _M0L3endS1066) {
          struct _M0TUsiE* _M0L8_2atupleS2608;
          int32_t _M0L6_2atmpS2609;
          moonbit_incref_cycle_free(_M0L4fileS1062);
          _M0L8_2atupleS2608
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2608)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2608->$0 = _M0L4fileS1062;
          _M0L8_2atupleS2608->$1 = _M0L1iS1067;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1054, _M0L8_2atupleS2608);
          _M0L6_2atmpS2609 = _M0L1iS1067 + 1;
          _M0L1iS1067 = _M0L6_2atmpS2609;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1062);
        }
        break;
      }
      _M0L6_2atmpS2612 = _M0L2__S1059 + 1;
      _M0L2__S1059 = _M0L6_2atmpS2612;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1058);
    }
    break;
  }
  return _M0L16file__and__indexS1054;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1047(
  int32_t _M0L6_2aenvS2589,
  moonbit_string_t _M0L1sS1048,
  int32_t _M0L3sepS1049
) {
  moonbit_string_t* _M0L6_2atmpS2607;
  struct _M0TPB5ArrayGsE* _M0L3resS1050;
  struct _M0TPB8MutLocalGiE* _M0L1iS1051;
  struct _M0TPB8MutLocalGiE* _M0L5startS1052;
  int32_t _M0L3valS2602;
  int32_t _M0L6_2atmpS2603;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2607 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1050
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1050)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1050->$0 = _M0L6_2atmpS2607;
  _M0L3resS1050->$1 = 0;
  _M0L1iS1051
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1051)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1051->$0 = 0;
  _M0L5startS1052
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1052)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1052->$0 = 0;
  while (1) {
    int32_t _M0L3valS2590 = _M0L1iS1051->$0;
    int32_t _M0L6_2atmpS2591 = Moonbit_array_length(_M0L1sS1048);
    if (_M0L3valS2590 < _M0L6_2atmpS2591) {
      int32_t _M0L3valS2594 = _M0L1iS1051->$0;
      int32_t _M0L6_2atmpS2593;
      int32_t _M0L6_2atmpS2592;
      int32_t _M0L3valS2601;
      int32_t _M0L6_2atmpS2600;
      if (
        _M0L3valS2594 < 0
        || _M0L3valS2594 >= Moonbit_array_length(_M0L1sS1048)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2593 = _M0L1sS1048[_M0L3valS2594];
      _M0L6_2atmpS2592 = _M0L6_2atmpS2593;
      if (_M0L6_2atmpS2592 == _M0L3sepS1049) {
        int32_t _M0L3valS2596 = _M0L5startS1052->$0;
        int32_t _M0L3valS2597 = _M0L1iS1051->$0;
        moonbit_string_t _M0L6_2atmpS2595;
        int32_t _M0L3valS2599;
        int32_t _M0L6_2atmpS2598;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2595
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1048, _M0L3valS2596, _M0L3valS2597);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1050, _M0L6_2atmpS2595);
        _M0L3valS2599 = _M0L1iS1051->$0;
        _M0L6_2atmpS2598 = _M0L3valS2599 + 1;
        _M0L5startS1052->$0 = _M0L6_2atmpS2598;
      }
      _M0L3valS2601 = _M0L1iS1051->$0;
      _M0L6_2atmpS2600 = _M0L3valS2601 + 1;
      _M0L1iS1051->$0 = _M0L6_2atmpS2600;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1051);
    }
    break;
  }
  _M0L3valS2602 = _M0L5startS1052->$0;
  _M0L6_2atmpS2603 = Moonbit_array_length(_M0L1sS1048);
  if (_M0L3valS2602 < _M0L6_2atmpS2603) {
    int32_t _M0L3valS2605 = _M0L5startS1052->$0;
    int32_t _M0L6_2atmpS2606;
    moonbit_string_t _M0L6_2atmpS2604;
    moonbit_decref_cycle_free(_M0L5startS1052);
    _M0L6_2atmpS2606 = Moonbit_array_length(_M0L1sS1048);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2604
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1048, _M0L3valS2605, _M0L6_2atmpS2606);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1050, _M0L6_2atmpS2604);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1052);
  }
  return _M0L3resS1050;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1040(
  int32_t _M0L6_2aenvS2582,
  moonbit_string_t _M0L1sS1041
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1042;
  int32_t _M0L3lenS1043;
  int32_t _M0L7_2abindS1044;
  int32_t _M0L1iS1045;
  int32_t _result_2686;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1042
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1042)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1042->$0 = 0;
  _M0L3lenS1043 = Moonbit_array_length(_M0L1sS1041);
  _M0L7_2abindS1044 = 0;
  _M0L1iS1045 = _M0L7_2abindS1044;
  while (1) {
    if (_M0L1iS1045 < _M0L3lenS1043) {
      int32_t _M0L3valS2587 = _M0L3resS1042->$0;
      int32_t _M0L6_2atmpS2584 = _M0L3valS2587 * 10;
      int32_t _M0L6_2atmpS2586;
      int32_t _M0L6_2atmpS2585;
      int32_t _M0L6_2atmpS2583;
      int32_t _M0L6_2atmpS2588;
      if (
        _M0L1iS1045 < 0 || _M0L1iS1045 >= Moonbit_array_length(_M0L1sS1041)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2586 = _M0L1sS1041[_M0L1iS1045];
      _M0L6_2atmpS2585 = _M0L6_2atmpS2586 - 48;
      _M0L6_2atmpS2583 = _M0L6_2atmpS2584 + _M0L6_2atmpS2585;
      _M0L3resS1042->$0 = _M0L6_2atmpS2583;
      _M0L6_2atmpS2588 = _M0L1iS1045 + 1;
      _M0L1iS1045 = _M0L6_2atmpS2588;
      continue;
    }
    break;
  }
  _result_2686 = _M0L3resS1042->$0;
  moonbit_decref_cycle_free(_M0L3resS1042);
  return _result_2686;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31tripod__current__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1039
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1039);
  return _M0L4selfS1039;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1009,
  moonbit_string_t _M0L12_2adiscard__S1010,
  int32_t _M0L12_2adiscard__S1011,
  struct _M0TWEu* _M0L12_2adiscard__S1012,
  struct _M0TWssbEu* _M0L12_2adiscard__S1013,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1014
) {
  struct moonbit_result_0 _result_2687;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2687.tag = 1;
  _result_2687.data.ok = 0;
  return _result_2687;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1015,
  moonbit_string_t _M0L12_2adiscard__S1016,
  int32_t _M0L12_2adiscard__S1017,
  struct _M0TWEu* _M0L12_2adiscard__S1018,
  struct _M0TWssbEu* _M0L12_2adiscard__S1019,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1020
) {
  struct moonbit_result_0 _result_2688;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2688.tag = 1;
  _result_2688.data.ok = 0;
  return _result_2688;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1021,
  moonbit_string_t _M0L12_2adiscard__S1022,
  int32_t _M0L12_2adiscard__S1023,
  struct _M0TWEu* _M0L12_2adiscard__S1024,
  struct _M0TWssbEu* _M0L12_2adiscard__S1025,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1026
) {
  struct moonbit_result_0 _result_2689;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2689.tag = 1;
  _result_2689.data.ok = 0;
  return _result_2689;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1027,
  moonbit_string_t _M0L12_2adiscard__S1028,
  int32_t _M0L12_2adiscard__S1029,
  struct _M0TWEu* _M0L12_2adiscard__S1030,
  struct _M0TWssbEu* _M0L12_2adiscard__S1031,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1032
) {
  struct moonbit_result_0 _result_2690;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2690.tag = 1;
  _result_2690.data.ok = 0;
  return _result_2690;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1033,
  moonbit_string_t _M0L12_2adiscard__S1034,
  int32_t _M0L12_2adiscard__S1035,
  struct _M0TWEu* _M0L12_2adiscard__S1036,
  struct _M0TWssbEu* _M0L12_2adiscard__S1037,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1038
) {
  struct moonbit_result_0 _result_2691;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _result_2691.tag = 1;
  _result_2691.data.ok = 0;
  return _result_2691;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1008
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t _M0L1nS973,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L1pS974
) {
  float _M0L2vtS2581;
  struct _M0TPB5ArrayGfE* _M0L7vt__arrS972;
  float _M0L2vrS2580;
  struct _M0TPB5ArrayGfE* _M0L7vr__arrS975;
  float _M0L2elS2579;
  struct _M0TPB5ArrayGfE* _M0L7el__arrS976;
  float _M0L2tmS2578;
  struct _M0TPB5ArrayGfE* _M0L7tm__arrS977;
  float _M0L1rS2577;
  struct _M0TPB5ArrayGfE* _M0L6r__arrS978;
  float _M0L9dt__slopeS2576;
  struct _M0TPB5ArrayGfE* _M0L14dt__slope__arrS979;
  float _M0L2twS2575;
  struct _M0TPB5ArrayGfE* _M0L7tw__arrS980;
  float _M0L1aS2574;
  struct _M0TPB5ArrayGfE* _M0L6a__arrS981;
  float _M0L1bS2573;
  struct _M0TPB5ArrayGfE* _M0L6b__arrS982;
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _block_2692;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2vtS2581 = _M0L1pS974->$2;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vt__arrS972 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L2vtS2581);
  _M0L2vrS2580 = _M0L1pS974->$3;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vr__arrS975 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L2vrS2580);
  _M0L2elS2579 = _M0L1pS974->$4;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7el__arrS976 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L2elS2579);
  _M0L2tmS2578 = _M0L1pS974->$5;
  #line 302 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tm__arrS977 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L2tmS2578);
  _M0L1rS2577 = _M0L1pS974->$6;
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6r__arrS978 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L1rS2577);
  _M0L9dt__slopeS2576 = _M0L1pS974->$7;
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L14dt__slope__arrS979
  = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L9dt__slopeS2576);
  _M0L2twS2575 = _M0L1pS974->$8;
  #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tw__arrS980 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L2twS2575);
  _M0L1aS2574 = _M0L1pS974->$9;
  #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6a__arrS981 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L1aS2574);
  _M0L1bS2573 = _M0L1pS974->$10;
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6b__arrS982 = _M0MPC15array5Array4makeGfE(_M0L1nS973, _M0L1bS2573);
  _block_2692
  = (struct _M0TP26RiantR8snn__mbt16AdExParameterHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet));
  Moonbit_object_header(_block_2692)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2692->$0 = _M0L7vt__arrS972;
  _block_2692->$1 = _M0L7vr__arrS975;
  _block_2692->$2 = _M0L7el__arrS976;
  _block_2692->$3 = _M0L7tm__arrS977;
  _block_2692->$4 = _M0L6r__arrS978;
  _block_2692->$5 = _M0L14dt__slope__arrS979;
  _block_2692->$6 = _M0L7tw__arrS980;
  _block_2692->$7 = _M0L6a__arrS981;
  _block_2692->$8 = _M0L6b__arrS982;
  return _block_2692;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS968;
  float _M0L2glS969;
  float _M0L2tmS970;
  float _M0L1rS971;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_2693;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS968 = 0x1.19p+8f;
  _M0L2glS969 = 0x1.4p+5f;
  _M0L2tmS970 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS971 = 0x1p+0f / 0x1.4p+5f;
  _block_2693
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_2693)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2693->$0 = _M0L1cS968;
  _block_2693->$1 = _M0L2glS969;
  _block_2693->$2 = -0x1.9p+5f;
  _block_2693->$3 = -0x1.1a66666666666p+6f;
  _block_2693->$4 = -0x1.1a66666666666p+6f;
  _block_2693->$5 = _M0L2tmS970;
  _block_2693->$6 = _M0L1rS971;
  _block_2693->$7 = 0x1p+1f;
  _block_2693->$8 = 0x1.2p+7f;
  _block_2693->$9 = 0x1p+2f;
  _block_2693->$10 = 0x1.42p+6f;
  return _block_2693;
}

int32_t _M0FP26RiantR8snn__mbt17step__tripod__het(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS949,
  float _M0L2dtS956
) {
  int32_t _M0L1nS948;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2572;
  float _M0L2atS950;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2571;
  float _M0L6tau__aS951;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2570;
  float _M0L11tabs__constS952;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2569;
  float _M0L2upS953;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2568;
  float _M0L12ap__membraneS954;
  float _M0L6_2atmpS2567;
  float _M0L6_2atmpS2566;
  int32_t _M0L11tabs__stepsS955;
  int32_t _M0L7_2abindS957;
  int32_t _M0L7_2abindS958;
  int32_t _M0L1iS959;
  int32_t _M0L7_2abindS961;
  int32_t _M0L1kS962;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS948 = _M0L1pS949->$27;
  _M0L11soma__spikeS2572 = _M0L1pS949->$1;
  _M0L2atS950 = _M0L11soma__spikeS2572->$0;
  _M0L11soma__spikeS2571 = _M0L1pS949->$1;
  _M0L6tau__aS951 = _M0L11soma__spikeS2571->$1;
  _M0L11soma__spikeS2570 = _M0L1pS949->$1;
  _M0L11tabs__constS952 = _M0L11soma__spikeS2570->$3;
  _M0L11soma__spikeS2569 = _M0L1pS949->$1;
  _M0L2upS953 = _M0L11soma__spikeS2569->$4;
  _M0L11soma__spikeS2568 = _M0L1pS949->$1;
  _M0L12ap__membraneS954 = _M0L11soma__spikeS2568->$2;
  _M0L6_2atmpS2567 = _M0L2upS953 + _M0L11tabs__constS952;
  _M0L6_2atmpS2566 = _M0L6_2atmpS2567 / _M0L2dtS956;
  #line 278 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L11tabs__stepsS955 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2566);
  #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(_M0L1pS949, _M0L2dtS956);
  #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(_M0L1pS949, _M0L2dtS956);
  #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(_M0L1pS949);
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(_M0L1pS949);
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS949, _M0L2dtS956, 0);
  _M0L7_2abindS957 = 0;
  _M0L7_2abindS958 = _M0L1nS948 * 4;
  _M0L1iS959 = _M0L7_2abindS957;
  while (1) {
    if (_M0L1iS959 < _M0L7_2abindS958) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2420 = _M0L1pS949->$36;
      struct _M0TPB5ArrayGfE* _M0L2dvS2422 = _M0L1pS949->$35;
      float _M0L6_2atmpS2421;
      int32_t _M0L6_2atmpS2423;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2421 = _M0MPC15array5Array2atGfE(_M0L2dvS2422, _M0L1iS959);
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2420, _M0L1iS959, _M0L6_2atmpS2421);
      _M0L6_2atmpS2423 = _M0L1iS959 + 1;
      _M0L1iS959 = _M0L6_2atmpS2423;
      continue;
    }
    break;
  }
  #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS949, _M0L2dtS956, 1);
  _M0L7_2abindS961 = 0;
  _M0L1kS962 = _M0L7_2abindS961;
  while (1) {
    if (_M0L1kS962 < _M0L1nS948) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2565 =
        _M0L1pS949->$0;
      struct _M0TPB5ArrayGfE* _M0L2vrS2564 = _M0L11soma__paramS2565->$1;
      float _M0L5vr__kS965;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2563;
      struct _M0TPB5ArrayGfE* _M0L1bS2562;
      float _M0L4b__kS966;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2425;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2428;
      int32_t _M0L6_2atmpS2427;
      int32_t _M0L6_2atmpS2426;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2429;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2440;
      float _M0L6_2atmpS2431;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2439;
      struct _M0TPB5ArrayGfE* _M0L2vtS2438;
      float _M0L6_2atmpS2435;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2437;
      float _M0L6_2atmpS2436;
      float _M0L6_2atmpS2434;
      float _M0L6_2atmpS2433;
      float _M0L6_2atmpS2432;
      float _M0L6_2atmpS2430;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2442;
      int32_t _M0L6_2atmpS2441;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2561;
      float _M0L6_2atmpS2556;
      struct _M0TPB5ArrayGfE* _M0L2dvS2559;
      int32_t _M0L6_2atmpS2560;
      float _M0L6_2atmpS2558;
      float _M0L6_2atmpS2557;
      float _M0L10v__s__predS967;
      struct _M0TPB5ArrayGbE* _M0L4fireS2480;
      int32_t _M0L6_2atmpS2481;
      struct _M0TPB5ArrayGbE* _M0L4fireS2482;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2498;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2510;
      float _M0L6_2atmpS2500;
      float _M0L6_2atmpS2502;
      struct _M0TPB5ArrayGfE* _M0L2dvS2508;
      int32_t _M0L6_2atmpS2509;
      float _M0L6_2atmpS2504;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2506;
      int32_t _M0L6_2atmpS2507;
      float _M0L6_2atmpS2505;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2501;
      float _M0L6_2atmpS2499;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2511;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2525;
      float _M0L6_2atmpS2513;
      float _M0L6_2atmpS2515;
      struct _M0TPB5ArrayGfE* _M0L2dvS2522;
      int32_t _M0L6_2atmpS2524;
      int32_t _M0L6_2atmpS2523;
      float _M0L6_2atmpS2517;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2519;
      int32_t _M0L6_2atmpS2521;
      int32_t _M0L6_2atmpS2520;
      float _M0L6_2atmpS2518;
      float _M0L6_2atmpS2516;
      float _M0L6_2atmpS2514;
      float _M0L6_2atmpS2512;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2526;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2540;
      float _M0L6_2atmpS2528;
      float _M0L6_2atmpS2530;
      struct _M0TPB5ArrayGfE* _M0L2dvS2537;
      int32_t _M0L6_2atmpS2539;
      int32_t _M0L6_2atmpS2538;
      float _M0L6_2atmpS2532;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2534;
      int32_t _M0L6_2atmpS2536;
      int32_t _M0L6_2atmpS2535;
      float _M0L6_2atmpS2533;
      float _M0L6_2atmpS2531;
      float _M0L6_2atmpS2529;
      float _M0L6_2atmpS2527;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2541;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2555;
      float _M0L6_2atmpS2543;
      float _M0L6_2atmpS2545;
      struct _M0TPB5ArrayGfE* _M0L2dvS2552;
      int32_t _M0L6_2atmpS2554;
      int32_t _M0L6_2atmpS2553;
      float _M0L6_2atmpS2547;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2549;
      int32_t _M0L6_2atmpS2551;
      int32_t _M0L6_2atmpS2550;
      float _M0L6_2atmpS2548;
      float _M0L6_2atmpS2546;
      float _M0L6_2atmpS2544;
      float _M0L6_2atmpS2542;
      int32_t _M0L6_2atmpS2424;
      #line 297 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L5vr__kS965 = _M0MPC15array5Array2atGfE(_M0L2vrS2564, _M0L1kS962);
      _M0L11soma__paramS2563 = _M0L1pS949->$0;
      _M0L1bS2562 = _M0L11soma__paramS2563->$8;
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L4b__kS966 = _M0MPC15array5Array2atGfE(_M0L1bS2562, _M0L1kS962);
      _M0L4tabsS2425 = _M0L1pS949->$34;
      _M0L4tabsS2428 = _M0L1pS949->$34;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2427
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2428, _M0L1kS962);
      _M0L6_2atmpS2426 = _M0L6_2atmpS2427 - 1;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2425, _M0L1kS962, _M0L6_2atmpS2426);
      _M0L9thresholdS2429 = _M0L1pS949->$33;
      _M0L9thresholdS2440 = _M0L1pS949->$33;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2431
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2440, _M0L1kS962);
      _M0L11soma__paramS2439 = _M0L1pS949->$0;
      _M0L2vtS2438 = _M0L11soma__paramS2439->$0;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2435 = _M0MPC15array5Array2atGfE(_M0L2vtS2438, _M0L1kS962);
      _M0L9thresholdS2437 = _M0L1pS949->$33;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2436
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2437, _M0L1kS962);
      _M0L6_2atmpS2434 = _M0L6_2atmpS2435 - _M0L6_2atmpS2436;
      _M0L6_2atmpS2433 = _M0L2dtS956 * _M0L6_2atmpS2434;
      _M0L6_2atmpS2432 = _M0L6_2atmpS2433 / _M0L6tau__aS951;
      _M0L6_2atmpS2430 = _M0L6_2atmpS2431 + _M0L6_2atmpS2432;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2429, _M0L1kS962, _M0L6_2atmpS2430);
      _M0L4tabsS2442 = _M0L1pS949->$34;
      #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2441
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2442, _M0L1kS962);
      if (_M0L6_2atmpS2441 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2443 = _M0L1pS949->$28;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2444;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2461;
        float _M0L6_2atmpS2446;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2460;
        float _M0L6_2atmpS2457;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2459;
        float _M0L6_2atmpS2458;
        float _M0L6_2atmpS2456;
        float _M0L6_2atmpS2452;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2455;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2454;
        float _M0L6_2atmpS2453;
        float _M0L6_2atmpS2448;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2451;
        struct _M0TPB5ArrayGfE* _M0L1cS2450;
        float _M0L6_2atmpS2449;
        float _M0L6_2atmpS2447;
        float _M0L6_2atmpS2445;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2462;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2479;
        float _M0L6_2atmpS2464;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2478;
        float _M0L6_2atmpS2475;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2477;
        float _M0L6_2atmpS2476;
        float _M0L6_2atmpS2474;
        float _M0L6_2atmpS2470;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2473;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2472;
        float _M0L6_2atmpS2471;
        float _M0L6_2atmpS2466;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2469;
        struct _M0TPB5ArrayGfE* _M0L1cS2468;
        float _M0L6_2atmpS2467;
        float _M0L6_2atmpS2465;
        float _M0L6_2atmpS2463;
        #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2443, _M0L1kS962, _M0L5vr__kS965);
        _M0L5v__d1S2444 = _M0L1pS949->$30;
        _M0L5v__d1S2461 = _M0L1pS949->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2446
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2461, _M0L1kS962);
        _M0L4v__sS2460 = _M0L1pS949->$28;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2457
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2460, _M0L1kS962);
        _M0L5v__d1S2459 = _M0L1pS949->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2458
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2459, _M0L1kS962);
        _M0L6_2atmpS2456 = _M0L6_2atmpS2457 - _M0L6_2atmpS2458;
        _M0L6_2atmpS2452 = _M0L2dtS956 * _M0L6_2atmpS2456;
        _M0L2d1S2455 = _M0L1pS949->$4;
        _M0L3gaxS2454 = _M0L2d1S2455->$3;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2453
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2454, _M0L1kS962);
        _M0L6_2atmpS2448 = _M0L6_2atmpS2452 * _M0L6_2atmpS2453;
        _M0L2d1S2451 = _M0L1pS949->$4;
        _M0L1cS2450 = _M0L2d1S2451->$2;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2449 = _M0MPC15array5Array2atGfE(_M0L1cS2450, _M0L1kS962);
        _M0L6_2atmpS2447 = _M0L6_2atmpS2448 / _M0L6_2atmpS2449;
        _M0L6_2atmpS2445 = _M0L6_2atmpS2446 + _M0L6_2atmpS2447;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2444, _M0L1kS962, _M0L6_2atmpS2445);
        _M0L5v__d2S2462 = _M0L1pS949->$31;
        _M0L5v__d2S2479 = _M0L1pS949->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2464
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2479, _M0L1kS962);
        _M0L4v__sS2478 = _M0L1pS949->$28;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2475
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2478, _M0L1kS962);
        _M0L5v__d2S2477 = _M0L1pS949->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2476
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2477, _M0L1kS962);
        _M0L6_2atmpS2474 = _M0L6_2atmpS2475 - _M0L6_2atmpS2476;
        _M0L6_2atmpS2470 = _M0L2dtS956 * _M0L6_2atmpS2474;
        _M0L2d2S2473 = _M0L1pS949->$5;
        _M0L3gaxS2472 = _M0L2d2S2473->$3;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2471
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2472, _M0L1kS962);
        _M0L6_2atmpS2466 = _M0L6_2atmpS2470 * _M0L6_2atmpS2471;
        _M0L2d2S2469 = _M0L1pS949->$5;
        _M0L1cS2468 = _M0L2d2S2469->$2;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2467 = _M0MPC15array5Array2atGfE(_M0L1cS2468, _M0L1kS962);
        _M0L6_2atmpS2465 = _M0L6_2atmpS2466 / _M0L6_2atmpS2467;
        _M0L6_2atmpS2463 = _M0L6_2atmpS2464 + _M0L6_2atmpS2465;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2462, _M0L1kS962, _M0L6_2atmpS2463);
        goto join_963;
      }
      _M0L4v__sS2561 = _M0L1pS949->$28;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2556
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2561, _M0L1kS962);
      _M0L2dvS2559 = _M0L1pS949->$35;
      _M0L6_2atmpS2560 = _M0L1kS962 * 4;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2558
      = _M0MPC15array5Array2atGfE(_M0L2dvS2559, _M0L6_2atmpS2560);
      _M0L6_2atmpS2557 = _M0L6_2atmpS2558 * _M0L2dtS956;
      _M0L10v__s__predS967 = _M0L6_2atmpS2556 + _M0L6_2atmpS2557;
      _M0L4fireS2480 = _M0L1pS949->$32;
      _M0L6_2atmpS2481 = _M0L10v__s__predS967 >= -0x1.4p+3f;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2480, _M0L1kS962, _M0L6_2atmpS2481);
      _M0L4fireS2482 = _M0L1pS949->$32;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2482, _M0L1kS962)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2483 = _M0L1pS949->$35;
        int32_t _M0L6_2atmpS2484 = _M0L1kS962 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2487 = _M0L1pS949->$28;
        float _M0L6_2atmpS2486;
        float _M0L6_2atmpS2485;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2488;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2489;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2492;
        float _M0L6_2atmpS2491;
        float _M0L6_2atmpS2490;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2493;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2496;
        float _M0L6_2atmpS2495;
        float _M0L6_2atmpS2494;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2497;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2486
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2487, _M0L1kS962);
        _M0L6_2atmpS2485 = _M0L12ap__membraneS954 - _M0L6_2atmpS2486;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2483, _M0L6_2atmpS2484, _M0L6_2atmpS2485);
        _M0L4v__sS2488 = _M0L1pS949->$28;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2488, _M0L1kS962, _M0L12ap__membraneS954);
        _M0L4w__sS2489 = _M0L1pS949->$29;
        _M0L4w__sS2492 = _M0L1pS949->$29;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2491
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2492, _M0L1kS962);
        _M0L6_2atmpS2490 = _M0L6_2atmpS2491 + _M0L4b__kS966;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2489, _M0L1kS962, _M0L6_2atmpS2490);
        _M0L9thresholdS2493 = _M0L1pS949->$33;
        _M0L9thresholdS2496 = _M0L1pS949->$33;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2495
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2496, _M0L1kS962);
        _M0L6_2atmpS2494 = _M0L6_2atmpS2495 + _M0L2atS950;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2493, _M0L1kS962, _M0L6_2atmpS2494);
        _M0L4tabsS2497 = _M0L1pS949->$34;
        #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2497, _M0L1kS962, _M0L11tabs__stepsS955);
        goto join_963;
      }
      _M0L4v__sS2498 = _M0L1pS949->$28;
      _M0L4v__sS2510 = _M0L1pS949->$28;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2500
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2510, _M0L1kS962);
      _M0L6_2atmpS2502 = 0x1p-1f * _M0L2dtS956;
      _M0L2dvS2508 = _M0L1pS949->$35;
      _M0L6_2atmpS2509 = _M0L1kS962 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2504
      = _M0MPC15array5Array2atGfE(_M0L2dvS2508, _M0L6_2atmpS2509);
      _M0L8dv__tempS2506 = _M0L1pS949->$36;
      _M0L6_2atmpS2507 = _M0L1kS962 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2505
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2506, _M0L6_2atmpS2507);
      _M0L6_2atmpS2503 = _M0L6_2atmpS2504 + _M0L6_2atmpS2505;
      _M0L6_2atmpS2501 = _M0L6_2atmpS2502 * _M0L6_2atmpS2503;
      _M0L6_2atmpS2499 = _M0L6_2atmpS2500 + _M0L6_2atmpS2501;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS2498, _M0L1kS962, _M0L6_2atmpS2499);
      _M0L5v__d1S2511 = _M0L1pS949->$30;
      _M0L5v__d1S2525 = _M0L1pS949->$30;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2513
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2525, _M0L1kS962);
      _M0L6_2atmpS2515 = 0x1p-1f * _M0L2dtS956;
      _M0L2dvS2522 = _M0L1pS949->$35;
      _M0L6_2atmpS2524 = _M0L1kS962 * 4;
      _M0L6_2atmpS2523 = _M0L6_2atmpS2524 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2517
      = _M0MPC15array5Array2atGfE(_M0L2dvS2522, _M0L6_2atmpS2523);
      _M0L8dv__tempS2519 = _M0L1pS949->$36;
      _M0L6_2atmpS2521 = _M0L1kS962 * 4;
      _M0L6_2atmpS2520 = _M0L6_2atmpS2521 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2518
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2519, _M0L6_2atmpS2520);
      _M0L6_2atmpS2516 = _M0L6_2atmpS2517 + _M0L6_2atmpS2518;
      _M0L6_2atmpS2514 = _M0L6_2atmpS2515 * _M0L6_2atmpS2516;
      _M0L6_2atmpS2512 = _M0L6_2atmpS2513 + _M0L6_2atmpS2514;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S2511, _M0L1kS962, _M0L6_2atmpS2512);
      _M0L5v__d2S2526 = _M0L1pS949->$31;
      _M0L5v__d2S2540 = _M0L1pS949->$31;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2528
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2540, _M0L1kS962);
      _M0L6_2atmpS2530 = 0x1p-1f * _M0L2dtS956;
      _M0L2dvS2537 = _M0L1pS949->$35;
      _M0L6_2atmpS2539 = _M0L1kS962 * 4;
      _M0L6_2atmpS2538 = _M0L6_2atmpS2539 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2532
      = _M0MPC15array5Array2atGfE(_M0L2dvS2537, _M0L6_2atmpS2538);
      _M0L8dv__tempS2534 = _M0L1pS949->$36;
      _M0L6_2atmpS2536 = _M0L1kS962 * 4;
      _M0L6_2atmpS2535 = _M0L6_2atmpS2536 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2533
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2534, _M0L6_2atmpS2535);
      _M0L6_2atmpS2531 = _M0L6_2atmpS2532 + _M0L6_2atmpS2533;
      _M0L6_2atmpS2529 = _M0L6_2atmpS2530 * _M0L6_2atmpS2531;
      _M0L6_2atmpS2527 = _M0L6_2atmpS2528 + _M0L6_2atmpS2529;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S2526, _M0L1kS962, _M0L6_2atmpS2527);
      _M0L4w__sS2541 = _M0L1pS949->$29;
      _M0L4w__sS2555 = _M0L1pS949->$29;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2543
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2555, _M0L1kS962);
      _M0L6_2atmpS2545 = 0x1p-1f * _M0L2dtS956;
      _M0L2dvS2552 = _M0L1pS949->$35;
      _M0L6_2atmpS2554 = _M0L1kS962 * 4;
      _M0L6_2atmpS2553 = _M0L6_2atmpS2554 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2547
      = _M0MPC15array5Array2atGfE(_M0L2dvS2552, _M0L6_2atmpS2553);
      _M0L8dv__tempS2549 = _M0L1pS949->$36;
      _M0L6_2atmpS2551 = _M0L1kS962 * 4;
      _M0L6_2atmpS2550 = _M0L6_2atmpS2551 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2548
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2549, _M0L6_2atmpS2550);
      _M0L6_2atmpS2546 = _M0L6_2atmpS2547 + _M0L6_2atmpS2548;
      _M0L6_2atmpS2544 = _M0L6_2atmpS2545 * _M0L6_2atmpS2546;
      _M0L6_2atmpS2542 = _M0L6_2atmpS2543 + _M0L6_2atmpS2544;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS2541, _M0L1kS962, _M0L6_2atmpS2542);
      goto join_963;
      goto joinlet_2696;
      join_963:;
      _M0L6_2atmpS2424 = _M0L1kS962 + 1;
      _M0L1kS962 = _M0L6_2atmpS2424;
      continue;
      joinlet_2696:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__het__heun__step(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS923,
  float _M0L2dtS936,
  int32_t _M0L11store__tempS935
) {
  int32_t _M0L1nS922;
  struct _M0TPB8MutLocalGiE* _M0L1kS924;
  #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS922 = _M0L1pS923->$27;
  _M0L1kS924
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS924)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS924->$0 = 0;
  while (1) {
    int32_t _M0L3valS2227 = _M0L1kS924->$0;
    if (_M0L3valS2227 < _M0L1nS922) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2419 =
        _M0L1pS923->$0;
      struct _M0TPB5ArrayGfE* _M0L2vtS2417 = _M0L11soma__paramS2419->$0;
      int32_t _M0L3valS2418 = _M0L1kS924->$0;
      float _M0L2vtS925;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2416;
      struct _M0TPB5ArrayGfE* _M0L2elS2414;
      int32_t _M0L3valS2415;
      float _M0L2elS926;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2413;
      struct _M0TPB5ArrayGfE* _M0L2tmS2411;
      int32_t _M0L3valS2412;
      float _M0L2tmS927;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2410;
      struct _M0TPB5ArrayGfE* _M0L1rS2408;
      int32_t _M0L3valS2409;
      float _M0L6r__valS928;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2407;
      struct _M0TPB5ArrayGfE* _M0L9dt__slopeS2405;
      int32_t _M0L3valS2406;
      float _M0L9dt__slopeS929;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2404;
      struct _M0TPB5ArrayGfE* _M0L2twS2402;
      int32_t _M0L3valS2403;
      float _M0L2twS930;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2401;
      struct _M0TPB5ArrayGfE* _M0L1aS2399;
      int32_t _M0L3valS2400;
      float _M0L1aS931;
      struct _M0TPB5ArrayGfE* _M0L1cS2397;
      int32_t _M0L3valS2398;
      float _M0L6c__valS932;
      struct _M0TPB5ArrayGfE* _M0L2glS2395;
      int32_t _M0L3valS2396;
      float _M0L7gl__valS933;
      float _M0L2dsS934;
      float _M0L3dd1S937;
      float _M0L3dd2S938;
      float _M0L2dwS939;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2375;
      int32_t _M0L3valS2376;
      float _M0L6_2atmpS2374;
      float _M0L6_2atmpS2370;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2372;
      int32_t _M0L3valS2373;
      float _M0L6_2atmpS2371;
      float _M0L6_2atmpS2369;
      float _M0L6_2atmpS2368;
      float _M0L6_2atmpS2363;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2367;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2365;
      int32_t _M0L3valS2366;
      float _M0L6_2atmpS2364;
      float _M0L3ic1S940;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2361;
      int32_t _M0L3valS2362;
      float _M0L6_2atmpS2360;
      float _M0L6_2atmpS2356;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2358;
      int32_t _M0L3valS2359;
      float _M0L6_2atmpS2357;
      float _M0L6_2atmpS2355;
      float _M0L6_2atmpS2354;
      float _M0L6_2atmpS2349;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2353;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2351;
      int32_t _M0L3valS2352;
      float _M0L6_2atmpS2350;
      float _M0L3ic2S941;
      float _M0L9exp__termS942;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2337;
      int32_t _M0L3valS2338;
      float _M0L6_2atmpS2336;
      float _M0L6_2atmpS2335;
      float _M0L6_2atmpS2334;
      float _M0L6_2atmpS2333;
      float _M0L6_2atmpS2329;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2331;
      int32_t _M0L3valS2332;
      float _M0L6_2atmpS2330;
      float _M0L6_2atmpS2328;
      float _M0L6_2atmpS2324;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2326;
      int32_t _M0L3valS2327;
      float _M0L6_2atmpS2325;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2323;
      float _M0L6_2atmpS2318;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2320;
      int32_t _M0L3valS2321;
      float _M0L6_2atmpS2319;
      float _M0L6_2atmpS2317;
      float _M0L10dv__s__valS943;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2315;
      int32_t _M0L3valS2316;
      float _M0L6_2atmpS2314;
      float _M0L6_2atmpS2313;
      float _M0L6_2atmpS2308;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2312;
      struct _M0TPB5ArrayGfE* _M0L2gmS2310;
      int32_t _M0L3valS2311;
      float _M0L6_2atmpS2309;
      float _M0L6_2atmpS2304;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2306;
      int32_t _M0L3valS2307;
      float _M0L6_2atmpS2305;
      float _M0L6_2atmpS2303;
      float _M0L6_2atmpS2299;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2301;
      int32_t _M0L3valS2302;
      float _M0L6_2atmpS2300;
      float _M0L6_2atmpS2294;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2298;
      struct _M0TPB5ArrayGfE* _M0L1cS2296;
      int32_t _M0L3valS2297;
      float _M0L6_2atmpS2295;
      float _M0L11dv__d1__valS944;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2292;
      int32_t _M0L3valS2293;
      float _M0L6_2atmpS2291;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2285;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2289;
      struct _M0TPB5ArrayGfE* _M0L2gmS2287;
      int32_t _M0L3valS2288;
      float _M0L6_2atmpS2286;
      float _M0L6_2atmpS2281;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2283;
      int32_t _M0L3valS2284;
      float _M0L6_2atmpS2282;
      float _M0L6_2atmpS2280;
      float _M0L6_2atmpS2276;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2278;
      int32_t _M0L3valS2279;
      float _M0L6_2atmpS2277;
      float _M0L6_2atmpS2271;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2275;
      struct _M0TPB5ArrayGfE* _M0L1cS2273;
      int32_t _M0L3valS2274;
      float _M0L6_2atmpS2272;
      float _M0L11dv__d2__valS945;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2269;
      int32_t _M0L3valS2270;
      float _M0L6_2atmpS2268;
      float _M0L6_2atmpS2267;
      float _M0L6_2atmpS2266;
      float _M0L6_2atmpS2261;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2264;
      int32_t _M0L3valS2265;
      float _M0L6_2atmpS2263;
      float _M0L6_2atmpS2262;
      float _M0L6_2atmpS2260;
      float _M0L7dw__valS946;
      int32_t _M0L3valS2259;
      int32_t _M0L6_2atmpS2258;
      #line 216 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2vtS925 = _M0MPC15array5Array2atGfE(_M0L2vtS2417, _M0L3valS2418);
      _M0L11soma__paramS2416 = _M0L1pS923->$0;
      _M0L2elS2414 = _M0L11soma__paramS2416->$2;
      _M0L3valS2415 = _M0L1kS924->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2elS926 = _M0MPC15array5Array2atGfE(_M0L2elS2414, _M0L3valS2415);
      _M0L11soma__paramS2413 = _M0L1pS923->$0;
      _M0L2tmS2411 = _M0L11soma__paramS2413->$3;
      _M0L3valS2412 = _M0L1kS924->$0;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2tmS927 = _M0MPC15array5Array2atGfE(_M0L2tmS2411, _M0L3valS2412);
      _M0L11soma__paramS2410 = _M0L1pS923->$0;
      _M0L1rS2408 = _M0L11soma__paramS2410->$4;
      _M0L3valS2409 = _M0L1kS924->$0;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6r__valS928 = _M0MPC15array5Array2atGfE(_M0L1rS2408, _M0L3valS2409);
      _M0L11soma__paramS2407 = _M0L1pS923->$0;
      _M0L9dt__slopeS2405 = _M0L11soma__paramS2407->$5;
      _M0L3valS2406 = _M0L1kS924->$0;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L9dt__slopeS929
      = _M0MPC15array5Array2atGfE(_M0L9dt__slopeS2405, _M0L3valS2406);
      _M0L11soma__paramS2404 = _M0L1pS923->$0;
      _M0L2twS2402 = _M0L11soma__paramS2404->$6;
      _M0L3valS2403 = _M0L1kS924->$0;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2twS930 = _M0MPC15array5Array2atGfE(_M0L2twS2402, _M0L3valS2403);
      _M0L11soma__paramS2401 = _M0L1pS923->$0;
      _M0L1aS2399 = _M0L11soma__paramS2401->$7;
      _M0L3valS2400 = _M0L1kS924->$0;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L1aS931 = _M0MPC15array5Array2atGfE(_M0L1aS2399, _M0L3valS2400);
      _M0L1cS2397 = _M0L1pS923->$2;
      _M0L3valS2398 = _M0L1kS924->$0;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6c__valS932 = _M0MPC15array5Array2atGfE(_M0L1cS2397, _M0L3valS2398);
      _M0L2glS2395 = _M0L1pS923->$3;
      _M0L3valS2396 = _M0L1kS924->$0;
      #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L7gl__valS933
      = _M0MPC15array5Array2atGfE(_M0L2glS2395, _M0L3valS2396);
      if (_M0L11store__tempS935) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2392 = _M0L1pS923->$36;
        int32_t _M0L3valS2394 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2393 = _M0L3valS2394 * 4;
        float _M0L6_2atmpS2391;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2391
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2392, _M0L6_2atmpS2393);
        _M0L2dsS934 = _M0L6_2atmpS2391 * _M0L2dtS936;
      } else {
        _M0L2dsS934 = 0x0p+0f;
      }
      if (_M0L11store__tempS935) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2387 = _M0L1pS923->$36;
        int32_t _M0L3valS2390 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2389 = _M0L3valS2390 * 4;
        int32_t _M0L6_2atmpS2388 = _M0L6_2atmpS2389 + 1;
        float _M0L6_2atmpS2386;
        #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2386
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2387, _M0L6_2atmpS2388);
        _M0L3dd1S937 = _M0L6_2atmpS2386 * _M0L2dtS936;
      } else {
        _M0L3dd1S937 = 0x0p+0f;
      }
      if (_M0L11store__tempS935) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2382 = _M0L1pS923->$36;
        int32_t _M0L3valS2385 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2384 = _M0L3valS2385 * 4;
        int32_t _M0L6_2atmpS2383 = _M0L6_2atmpS2384 + 2;
        float _M0L6_2atmpS2381;
        #line 228 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2381
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2382, _M0L6_2atmpS2383);
        _M0L3dd2S938 = _M0L6_2atmpS2381 * _M0L2dtS936;
      } else {
        _M0L3dd2S938 = 0x0p+0f;
      }
      if (_M0L11store__tempS935) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2377 = _M0L1pS923->$36;
        int32_t _M0L3valS2380 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2379 = _M0L3valS2380 * 4;
        int32_t _M0L6_2atmpS2378 = _M0L6_2atmpS2379 + 3;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L2dwS939
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2377, _M0L6_2atmpS2378);
      } else {
        _M0L2dwS939 = 0x0p+0f;
      }
      _M0L5v__d1S2375 = _M0L1pS923->$30;
      _M0L3valS2376 = _M0L1kS924->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2374
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2375, _M0L3valS2376);
      _M0L6_2atmpS2370 = _M0L6_2atmpS2374 + _M0L3dd1S937;
      _M0L4v__sS2372 = _M0L1pS923->$28;
      _M0L3valS2373 = _M0L1kS924->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2371
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2372, _M0L3valS2373);
      _M0L6_2atmpS2369 = _M0L6_2atmpS2370 - _M0L6_2atmpS2371;
      _M0L6_2atmpS2368 = _M0L6_2atmpS2369 - _M0L2dsS934;
      _M0L6_2atmpS2363 = -_M0L6_2atmpS2368;
      _M0L2d1S2367 = _M0L1pS923->$4;
      _M0L3gaxS2365 = _M0L2d1S2367->$3;
      _M0L3valS2366 = _M0L1kS924->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2364
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2365, _M0L3valS2366);
      _M0L3ic1S940 = _M0L6_2atmpS2363 * _M0L6_2atmpS2364;
      _M0L5v__d2S2361 = _M0L1pS923->$31;
      _M0L3valS2362 = _M0L1kS924->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2360
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2361, _M0L3valS2362);
      _M0L6_2atmpS2356 = _M0L6_2atmpS2360 + _M0L3dd2S938;
      _M0L4v__sS2358 = _M0L1pS923->$28;
      _M0L3valS2359 = _M0L1kS924->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2357
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2358, _M0L3valS2359);
      _M0L6_2atmpS2355 = _M0L6_2atmpS2356 - _M0L6_2atmpS2357;
      _M0L6_2atmpS2354 = _M0L6_2atmpS2355 - _M0L2dsS934;
      _M0L6_2atmpS2349 = -_M0L6_2atmpS2354;
      _M0L2d2S2353 = _M0L1pS923->$5;
      _M0L3gaxS2351 = _M0L2d2S2353->$3;
      _M0L3valS2352 = _M0L1kS924->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2350
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2351, _M0L3valS2352);
      _M0L3ic2S941 = _M0L6_2atmpS2349 * _M0L6_2atmpS2350;
      if (_M0L9dt__slopeS929 < 0x0p+0f) {
        _M0L9exp__termS942 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2347 = _M0L1pS923->$28;
        int32_t _M0L3valS2348 = _M0L1kS924->$0;
        float _M0L6_2atmpS2346;
        float _M0L6_2atmpS2342;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2344;
        int32_t _M0L3valS2345;
        float _M0L6_2atmpS2343;
        float _M0L6_2atmpS2341;
        float _M0L6_2atmpS2340;
        float _M0L6_2atmpS2339;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2346
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2347, _M0L3valS2348);
        _M0L6_2atmpS2342 = _M0L6_2atmpS2346 + _M0L2dsS934;
        _M0L9thresholdS2344 = _M0L1pS923->$33;
        _M0L3valS2345 = _M0L1kS924->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2343
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2344, _M0L3valS2345);
        _M0L6_2atmpS2341 = _M0L6_2atmpS2342 - _M0L6_2atmpS2343;
        _M0L6_2atmpS2340 = _M0L6_2atmpS2341 / _M0L9dt__slopeS929;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2339 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2340);
        _M0L9exp__termS942 = _M0L9dt__slopeS929 * _M0L6_2atmpS2339;
      }
      _M0L4v__sS2337 = _M0L1pS923->$28;
      _M0L3valS2338 = _M0L1kS924->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2336
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2337, _M0L3valS2338);
      _M0L6_2atmpS2335 = _M0L2elS926 - _M0L6_2atmpS2336;
      _M0L6_2atmpS2334 = _M0L6_2atmpS2335 - _M0L2dsS934;
      _M0L6_2atmpS2333 = _M0L7gl__valS933 * _M0L6_2atmpS2334;
      _M0L6_2atmpS2329 = _M0L6_2atmpS2333 + _M0L9exp__termS942;
      _M0L4w__sS2331 = _M0L1pS923->$29;
      _M0L3valS2332 = _M0L1kS924->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2330
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2331, _M0L3valS2332);
      _M0L6_2atmpS2328 = _M0L6_2atmpS2329 - _M0L6_2atmpS2330;
      _M0L6_2atmpS2324 = _M0L6_2atmpS2328 - _M0L2dwS939;
      _M0L12syn__curr__sS2326 = _M0L1pS923->$37;
      _M0L3valS2327 = _M0L1kS924->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2325
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2326, _M0L3valS2327);
      _M0L6_2atmpS2322 = _M0L6_2atmpS2324 - _M0L6_2atmpS2325;
      _M0L6_2atmpS2323 = _M0L3ic1S940 + _M0L3ic2S941;
      _M0L6_2atmpS2318 = _M0L6_2atmpS2322 - _M0L6_2atmpS2323;
      _M0L4i__sS2320 = _M0L1pS923->$6;
      _M0L3valS2321 = _M0L1kS924->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2319
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2320, _M0L3valS2321);
      _M0L6_2atmpS2317 = _M0L6_2atmpS2318 + _M0L6_2atmpS2319;
      _M0L10dv__s__valS943 = _M0L6_2atmpS2317 / _M0L6c__valS932;
      _M0L5v__d1S2315 = _M0L1pS923->$30;
      _M0L3valS2316 = _M0L1kS924->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2314
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2315, _M0L3valS2316);
      _M0L6_2atmpS2313 = _M0L2elS926 - _M0L6_2atmpS2314;
      _M0L6_2atmpS2308 = _M0L6_2atmpS2313 - _M0L3dd1S937;
      _M0L2d1S2312 = _M0L1pS923->$4;
      _M0L2gmS2310 = _M0L2d1S2312->$4;
      _M0L3valS2311 = _M0L1kS924->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2309
      = _M0MPC15array5Array2atGfE(_M0L2gmS2310, _M0L3valS2311);
      _M0L6_2atmpS2304 = _M0L6_2atmpS2308 * _M0L6_2atmpS2309;
      _M0L13syn__curr__d1S2306 = _M0L1pS923->$38;
      _M0L3valS2307 = _M0L1kS924->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2305
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2306, _M0L3valS2307);
      _M0L6_2atmpS2303 = _M0L6_2atmpS2304 - _M0L6_2atmpS2305;
      _M0L6_2atmpS2299 = _M0L6_2atmpS2303 + _M0L3ic1S940;
      _M0L5i__d1S2301 = _M0L1pS923->$7;
      _M0L3valS2302 = _M0L1kS924->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2300
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2301, _M0L3valS2302);
      _M0L6_2atmpS2294 = _M0L6_2atmpS2299 + _M0L6_2atmpS2300;
      _M0L2d1S2298 = _M0L1pS923->$4;
      _M0L1cS2296 = _M0L2d1S2298->$2;
      _M0L3valS2297 = _M0L1kS924->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2295
      = _M0MPC15array5Array2atGfE(_M0L1cS2296, _M0L3valS2297);
      _M0L11dv__d1__valS944 = _M0L6_2atmpS2294 / _M0L6_2atmpS2295;
      _M0L5v__d2S2292 = _M0L1pS923->$31;
      _M0L3valS2293 = _M0L1kS924->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2291
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2292, _M0L3valS2293);
      _M0L6_2atmpS2290 = _M0L2elS926 - _M0L6_2atmpS2291;
      _M0L6_2atmpS2285 = _M0L6_2atmpS2290 - _M0L3dd2S938;
      _M0L2d2S2289 = _M0L1pS923->$5;
      _M0L2gmS2287 = _M0L2d2S2289->$4;
      _M0L3valS2288 = _M0L1kS924->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2286
      = _M0MPC15array5Array2atGfE(_M0L2gmS2287, _M0L3valS2288);
      _M0L6_2atmpS2281 = _M0L6_2atmpS2285 * _M0L6_2atmpS2286;
      _M0L13syn__curr__d2S2283 = _M0L1pS923->$39;
      _M0L3valS2284 = _M0L1kS924->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2282
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2283, _M0L3valS2284);
      _M0L6_2atmpS2280 = _M0L6_2atmpS2281 - _M0L6_2atmpS2282;
      _M0L6_2atmpS2276 = _M0L6_2atmpS2280 + _M0L3ic2S941;
      _M0L5i__d2S2278 = _M0L1pS923->$8;
      _M0L3valS2279 = _M0L1kS924->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2277
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2278, _M0L3valS2279);
      _M0L6_2atmpS2271 = _M0L6_2atmpS2276 + _M0L6_2atmpS2277;
      _M0L2d2S2275 = _M0L1pS923->$5;
      _M0L1cS2273 = _M0L2d2S2275->$2;
      _M0L3valS2274 = _M0L1kS924->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2272
      = _M0MPC15array5Array2atGfE(_M0L1cS2273, _M0L3valS2274);
      _M0L11dv__d2__valS945 = _M0L6_2atmpS2271 / _M0L6_2atmpS2272;
      _M0L4v__sS2269 = _M0L1pS923->$28;
      _M0L3valS2270 = _M0L1kS924->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2268
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2269, _M0L3valS2270);
      _M0L6_2atmpS2267 = _M0L6_2atmpS2268 + _M0L2dsS934;
      _M0L6_2atmpS2266 = _M0L6_2atmpS2267 - _M0L2elS926;
      _M0L6_2atmpS2261 = _M0L1aS931 * _M0L6_2atmpS2266;
      _M0L4w__sS2264 = _M0L1pS923->$29;
      _M0L3valS2265 = _M0L1kS924->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2263
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2264, _M0L3valS2265);
      _M0L6_2atmpS2262 = _M0L6_2atmpS2263 + _M0L2dwS939;
      _M0L6_2atmpS2260 = _M0L6_2atmpS2261 - _M0L6_2atmpS2262;
      _M0L7dw__valS946 = _M0L6_2atmpS2260 / _M0L2twS930;
      if (_M0L11store__tempS935) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2228 = _M0L1pS923->$36;
        int32_t _M0L3valS2230 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2229 = _M0L3valS2230 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2231;
        int32_t _M0L3valS2234;
        int32_t _M0L6_2atmpS2233;
        int32_t _M0L6_2atmpS2232;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2235;
        int32_t _M0L3valS2238;
        int32_t _M0L6_2atmpS2237;
        int32_t _M0L6_2atmpS2236;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2239;
        int32_t _M0L3valS2242;
        int32_t _M0L6_2atmpS2241;
        int32_t _M0L6_2atmpS2240;
        #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2228, _M0L6_2atmpS2229, _M0L10dv__s__valS943);
        _M0L8dv__tempS2231 = _M0L1pS923->$36;
        _M0L3valS2234 = _M0L1kS924->$0;
        _M0L6_2atmpS2233 = _M0L3valS2234 * 4;
        _M0L6_2atmpS2232 = _M0L6_2atmpS2233 + 1;
        #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2231, _M0L6_2atmpS2232, _M0L11dv__d1__valS944);
        _M0L8dv__tempS2235 = _M0L1pS923->$36;
        _M0L3valS2238 = _M0L1kS924->$0;
        _M0L6_2atmpS2237 = _M0L3valS2238 * 4;
        _M0L6_2atmpS2236 = _M0L6_2atmpS2237 + 2;
        #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2235, _M0L6_2atmpS2236, _M0L11dv__d2__valS945);
        _M0L8dv__tempS2239 = _M0L1pS923->$36;
        _M0L3valS2242 = _M0L1kS924->$0;
        _M0L6_2atmpS2241 = _M0L3valS2242 * 4;
        _M0L6_2atmpS2240 = _M0L6_2atmpS2241 + 3;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2239, _M0L6_2atmpS2240, _M0L7dw__valS946);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2243 = _M0L1pS923->$35;
        int32_t _M0L3valS2245 = _M0L1kS924->$0;
        int32_t _M0L6_2atmpS2244 = _M0L3valS2245 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2246;
        int32_t _M0L3valS2249;
        int32_t _M0L6_2atmpS2248;
        int32_t _M0L6_2atmpS2247;
        struct _M0TPB5ArrayGfE* _M0L2dvS2250;
        int32_t _M0L3valS2253;
        int32_t _M0L6_2atmpS2252;
        int32_t _M0L6_2atmpS2251;
        struct _M0TPB5ArrayGfE* _M0L2dvS2254;
        int32_t _M0L3valS2257;
        int32_t _M0L6_2atmpS2256;
        int32_t _M0L6_2atmpS2255;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2243, _M0L6_2atmpS2244, _M0L10dv__s__valS943);
        _M0L2dvS2246 = _M0L1pS923->$35;
        _M0L3valS2249 = _M0L1kS924->$0;
        _M0L6_2atmpS2248 = _M0L3valS2249 * 4;
        _M0L6_2atmpS2247 = _M0L6_2atmpS2248 + 1;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2246, _M0L6_2atmpS2247, _M0L11dv__d1__valS944);
        _M0L2dvS2250 = _M0L1pS923->$35;
        _M0L3valS2253 = _M0L1kS924->$0;
        _M0L6_2atmpS2252 = _M0L3valS2253 * 4;
        _M0L6_2atmpS2251 = _M0L6_2atmpS2252 + 2;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2250, _M0L6_2atmpS2251, _M0L11dv__d2__valS945);
        _M0L2dvS2254 = _M0L1pS923->$35;
        _M0L3valS2257 = _M0L1kS924->$0;
        _M0L6_2atmpS2256 = _M0L3valS2257 * 4;
        _M0L6_2atmpS2255 = _M0L6_2atmpS2256 + 3;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2254, _M0L6_2atmpS2255, _M0L7dw__valS946);
      }
      _M0L3valS2259 = _M0L1kS924->$0;
      _M0L6_2atmpS2258 = _M0L3valS2259 + 1;
      _M0L1kS924->$0 = _M0L6_2atmpS2258;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS924);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS918
) {
  int32_t _M0L1nS917;
  int32_t _M0L7_2abindS919;
  int32_t _M0L1iS920;
  #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS917 = _M0L1pS918->$27;
  _M0L7_2abindS919 = 0;
  _M0L1iS920 = _M0L7_2abindS919;
  while (1) {
    if (_M0L1iS920 < _M0L1nS917) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2186 = _M0L1pS918->$38;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2205 = _M0L1pS918->$11;
      float _M0L6_2atmpS2200;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2204;
      float _M0L6_2atmpS2202;
      float _M0L4e__eS2203;
      float _M0L6_2atmpS2201;
      float _M0L6_2atmpS2198;
      float _M0L7gsyn__eS2199;
      float _M0L6_2atmpS2188;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2197;
      float _M0L6_2atmpS2192;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2196;
      float _M0L6_2atmpS2194;
      float _M0L4e__iS2195;
      float _M0L6_2atmpS2193;
      float _M0L6_2atmpS2190;
      float _M0L7gsyn__iS2191;
      float _M0L6_2atmpS2189;
      float _M0L6_2atmpS2187;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2206;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2225;
      float _M0L6_2atmpS2220;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2224;
      float _M0L6_2atmpS2222;
      float _M0L4e__eS2223;
      float _M0L6_2atmpS2221;
      float _M0L6_2atmpS2218;
      float _M0L7gsyn__eS2219;
      float _M0L6_2atmpS2208;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2217;
      float _M0L6_2atmpS2212;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2216;
      float _M0L6_2atmpS2214;
      float _M0L4e__iS2215;
      float _M0L6_2atmpS2213;
      float _M0L6_2atmpS2210;
      float _M0L7gsyn__iS2211;
      float _M0L6_2atmpS2209;
      float _M0L6_2atmpS2207;
      int32_t _M0L6_2atmpS2226;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2200
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2205, _M0L1iS920);
      _M0L5v__d1S2204 = _M0L1pS918->$30;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2202
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2204, _M0L1iS920);
      _M0L4e__eS2203 = _M0L1pS918->$21;
      _M0L6_2atmpS2201 = _M0L6_2atmpS2202 - _M0L4e__eS2203;
      _M0L6_2atmpS2198 = _M0L6_2atmpS2200 * _M0L6_2atmpS2201;
      _M0L7gsyn__eS2199 = _M0L1pS918->$25;
      _M0L6_2atmpS2188 = _M0L6_2atmpS2198 * _M0L7gsyn__eS2199;
      _M0L6gi__d1S2197 = _M0L1pS918->$12;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2192
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2197, _M0L1iS920);
      _M0L5v__d1S2196 = _M0L1pS918->$30;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2194
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2196, _M0L1iS920);
      _M0L4e__iS2195 = _M0L1pS918->$22;
      _M0L6_2atmpS2193 = _M0L6_2atmpS2194 - _M0L4e__iS2195;
      _M0L6_2atmpS2190 = _M0L6_2atmpS2192 * _M0L6_2atmpS2193;
      _M0L7gsyn__iS2191 = _M0L1pS918->$26;
      _M0L6_2atmpS2189 = _M0L6_2atmpS2190 * _M0L7gsyn__iS2191;
      _M0L6_2atmpS2187 = _M0L6_2atmpS2188 + _M0L6_2atmpS2189;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S2186, _M0L1iS920, _M0L6_2atmpS2187);
      _M0L13syn__curr__d2S2206 = _M0L1pS918->$39;
      _M0L6ge__d2S2225 = _M0L1pS918->$13;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2220
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2225, _M0L1iS920);
      _M0L5v__d2S2224 = _M0L1pS918->$31;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2222
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2224, _M0L1iS920);
      _M0L4e__eS2223 = _M0L1pS918->$21;
      _M0L6_2atmpS2221 = _M0L6_2atmpS2222 - _M0L4e__eS2223;
      _M0L6_2atmpS2218 = _M0L6_2atmpS2220 * _M0L6_2atmpS2221;
      _M0L7gsyn__eS2219 = _M0L1pS918->$25;
      _M0L6_2atmpS2208 = _M0L6_2atmpS2218 * _M0L7gsyn__eS2219;
      _M0L6gi__d2S2217 = _M0L1pS918->$14;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2212
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2217, _M0L1iS920);
      _M0L5v__d2S2216 = _M0L1pS918->$31;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2214
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2216, _M0L1iS920);
      _M0L4e__iS2215 = _M0L1pS918->$22;
      _M0L6_2atmpS2213 = _M0L6_2atmpS2214 - _M0L4e__iS2215;
      _M0L6_2atmpS2210 = _M0L6_2atmpS2212 * _M0L6_2atmpS2213;
      _M0L7gsyn__iS2211 = _M0L1pS918->$26;
      _M0L6_2atmpS2209 = _M0L6_2atmpS2210 * _M0L7gsyn__iS2211;
      _M0L6_2atmpS2207 = _M0L6_2atmpS2208 + _M0L6_2atmpS2209;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S2206, _M0L1iS920, _M0L6_2atmpS2207);
      _M0L6_2atmpS2226 = _M0L1iS920 + 1;
      _M0L1iS920 = _M0L6_2atmpS2226;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS913
) {
  int32_t _M0L1nS912;
  int32_t _M0L7_2abindS914;
  int32_t _M0L1iS915;
  #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS912 = _M0L1pS913->$27;
  _M0L7_2abindS914 = 0;
  _M0L1iS915 = _M0L7_2abindS914;
  while (1) {
    if (_M0L1iS915 < _M0L1nS912) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2165 = _M0L1pS913->$37;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2184 = _M0L1pS913->$9;
      float _M0L6_2atmpS2179;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2183;
      float _M0L6_2atmpS2181;
      float _M0L4e__eS2182;
      float _M0L6_2atmpS2180;
      float _M0L6_2atmpS2177;
      float _M0L7gsyn__eS2178;
      float _M0L6_2atmpS2167;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2176;
      float _M0L6_2atmpS2171;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2175;
      float _M0L6_2atmpS2173;
      float _M0L4e__iS2174;
      float _M0L6_2atmpS2172;
      float _M0L6_2atmpS2169;
      float _M0L7gsyn__iS2170;
      float _M0L6_2atmpS2168;
      float _M0L6_2atmpS2166;
      int32_t _M0L6_2atmpS2185;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2179
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2184, _M0L1iS915);
      _M0L4v__sS2183 = _M0L1pS913->$28;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2181
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2183, _M0L1iS915);
      _M0L4e__eS2182 = _M0L1pS913->$21;
      _M0L6_2atmpS2180 = _M0L6_2atmpS2181 - _M0L4e__eS2182;
      _M0L6_2atmpS2177 = _M0L6_2atmpS2179 * _M0L6_2atmpS2180;
      _M0L7gsyn__eS2178 = _M0L1pS913->$25;
      _M0L6_2atmpS2167 = _M0L6_2atmpS2177 * _M0L7gsyn__eS2178;
      _M0L5gi__sS2176 = _M0L1pS913->$10;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2171
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2176, _M0L1iS915);
      _M0L4v__sS2175 = _M0L1pS913->$28;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2173
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2175, _M0L1iS915);
      _M0L4e__iS2174 = _M0L1pS913->$22;
      _M0L6_2atmpS2172 = _M0L6_2atmpS2173 - _M0L4e__iS2174;
      _M0L6_2atmpS2169 = _M0L6_2atmpS2171 * _M0L6_2atmpS2172;
      _M0L7gsyn__iS2170 = _M0L1pS913->$26;
      _M0L6_2atmpS2168 = _M0L6_2atmpS2169 * _M0L7gsyn__iS2170;
      _M0L6_2atmpS2166 = _M0L6_2atmpS2167 + _M0L6_2atmpS2168;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2165, _M0L1iS915, _M0L6_2atmpS2166);
      _M0L6_2atmpS2185 = _M0L1iS915 + 1;
      _M0L1iS915 = _M0L6_2atmpS2185;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS904,
  float _M0L2dtS907
) {
  int32_t _M0L1nS903;
  int32_t _M0L7_2abindS905;
  int32_t _M0L1iS906;
  int32_t _M0L7_2abindS909;
  int32_t _M0L1iS910;
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS903 = _M0L1pS904->$27;
  _M0L7_2abindS905 = 0;
  _M0L1iS906 = _M0L7_2abindS905;
  while (1) {
    if (_M0L1iS906 < _M0L1nS903) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2095 = _M0L1pS904->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2100 = _M0L1pS904->$11;
      float _M0L6_2atmpS2097;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2099;
      float _M0L6_2atmpS2098;
      float _M0L6_2atmpS2096;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2101;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2106;
      float _M0L6_2atmpS2103;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2105;
      float _M0L6_2atmpS2104;
      float _M0L6_2atmpS2102;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2107;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2116;
      float _M0L6_2atmpS2109;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2115;
      float _M0L6_2atmpS2114;
      float _M0L6_2atmpS2112;
      float _M0L6tau__eS2113;
      float _M0L6_2atmpS2111;
      float _M0L6_2atmpS2110;
      float _M0L6_2atmpS2108;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2117;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2126;
      float _M0L6_2atmpS2119;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2125;
      float _M0L6_2atmpS2124;
      float _M0L6_2atmpS2122;
      float _M0L6tau__iS2123;
      float _M0L6_2atmpS2121;
      float _M0L6_2atmpS2120;
      float _M0L6_2atmpS2118;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2127;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2132;
      float _M0L6_2atmpS2129;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2131;
      float _M0L6_2atmpS2130;
      float _M0L6_2atmpS2128;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2133;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2138;
      float _M0L6_2atmpS2135;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2137;
      float _M0L6_2atmpS2136;
      float _M0L6_2atmpS2134;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2139;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2148;
      float _M0L6_2atmpS2141;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2147;
      float _M0L6_2atmpS2146;
      float _M0L6_2atmpS2144;
      float _M0L6tau__eS2145;
      float _M0L6_2atmpS2143;
      float _M0L6_2atmpS2142;
      float _M0L6_2atmpS2140;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2149;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2158;
      float _M0L6_2atmpS2151;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2157;
      float _M0L6_2atmpS2156;
      float _M0L6_2atmpS2154;
      float _M0L6tau__iS2155;
      float _M0L6_2atmpS2153;
      float _M0L6_2atmpS2152;
      float _M0L6_2atmpS2150;
      int32_t _M0L6_2atmpS2159;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2097
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2100, _M0L1iS906);
      _M0L7glu__d1S2099 = _M0L1pS904->$17;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2098
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S2099, _M0L1iS906);
      _M0L6_2atmpS2096 = _M0L6_2atmpS2097 + _M0L6_2atmpS2098;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2095, _M0L1iS906, _M0L6_2atmpS2096);
      _M0L6gi__d1S2101 = _M0L1pS904->$12;
      _M0L6gi__d1S2106 = _M0L1pS904->$12;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2103
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2106, _M0L1iS906);
      _M0L8gaba__d1S2105 = _M0L1pS904->$18;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2104
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S2105, _M0L1iS906);
      _M0L6_2atmpS2102 = _M0L6_2atmpS2103 + _M0L6_2atmpS2104;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2101, _M0L1iS906, _M0L6_2atmpS2102);
      _M0L6ge__d1S2107 = _M0L1pS904->$11;
      _M0L6ge__d1S2116 = _M0L1pS904->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2109
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2116, _M0L1iS906);
      _M0L6ge__d1S2115 = _M0L1pS904->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2114
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2115, _M0L1iS906);
      _M0L6_2atmpS2112 = -_M0L6_2atmpS2114;
      _M0L6tau__eS2113 = _M0L1pS904->$23;
      _M0L6_2atmpS2111 = _M0L6_2atmpS2112 / _M0L6tau__eS2113;
      _M0L6_2atmpS2110 = _M0L2dtS907 * _M0L6_2atmpS2111;
      _M0L6_2atmpS2108 = _M0L6_2atmpS2109 + _M0L6_2atmpS2110;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2107, _M0L1iS906, _M0L6_2atmpS2108);
      _M0L6gi__d1S2117 = _M0L1pS904->$12;
      _M0L6gi__d1S2126 = _M0L1pS904->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2119
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2126, _M0L1iS906);
      _M0L6gi__d1S2125 = _M0L1pS904->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2124
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2125, _M0L1iS906);
      _M0L6_2atmpS2122 = -_M0L6_2atmpS2124;
      _M0L6tau__iS2123 = _M0L1pS904->$24;
      _M0L6_2atmpS2121 = _M0L6_2atmpS2122 / _M0L6tau__iS2123;
      _M0L6_2atmpS2120 = _M0L2dtS907 * _M0L6_2atmpS2121;
      _M0L6_2atmpS2118 = _M0L6_2atmpS2119 + _M0L6_2atmpS2120;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2117, _M0L1iS906, _M0L6_2atmpS2118);
      _M0L6ge__d2S2127 = _M0L1pS904->$13;
      _M0L6ge__d2S2132 = _M0L1pS904->$13;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2129
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2132, _M0L1iS906);
      _M0L7glu__d2S2131 = _M0L1pS904->$19;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2130
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S2131, _M0L1iS906);
      _M0L6_2atmpS2128 = _M0L6_2atmpS2129 + _M0L6_2atmpS2130;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2127, _M0L1iS906, _M0L6_2atmpS2128);
      _M0L6gi__d2S2133 = _M0L1pS904->$14;
      _M0L6gi__d2S2138 = _M0L1pS904->$14;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2135
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2138, _M0L1iS906);
      _M0L8gaba__d2S2137 = _M0L1pS904->$20;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2136
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S2137, _M0L1iS906);
      _M0L6_2atmpS2134 = _M0L6_2atmpS2135 + _M0L6_2atmpS2136;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2133, _M0L1iS906, _M0L6_2atmpS2134);
      _M0L6ge__d2S2139 = _M0L1pS904->$13;
      _M0L6ge__d2S2148 = _M0L1pS904->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2141
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2148, _M0L1iS906);
      _M0L6ge__d2S2147 = _M0L1pS904->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2146
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2147, _M0L1iS906);
      _M0L6_2atmpS2144 = -_M0L6_2atmpS2146;
      _M0L6tau__eS2145 = _M0L1pS904->$23;
      _M0L6_2atmpS2143 = _M0L6_2atmpS2144 / _M0L6tau__eS2145;
      _M0L6_2atmpS2142 = _M0L2dtS907 * _M0L6_2atmpS2143;
      _M0L6_2atmpS2140 = _M0L6_2atmpS2141 + _M0L6_2atmpS2142;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2139, _M0L1iS906, _M0L6_2atmpS2140);
      _M0L6gi__d2S2149 = _M0L1pS904->$14;
      _M0L6gi__d2S2158 = _M0L1pS904->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2151
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2158, _M0L1iS906);
      _M0L6gi__d2S2157 = _M0L1pS904->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2156
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2157, _M0L1iS906);
      _M0L6_2atmpS2154 = -_M0L6_2atmpS2156;
      _M0L6tau__iS2155 = _M0L1pS904->$24;
      _M0L6_2atmpS2153 = _M0L6_2atmpS2154 / _M0L6tau__iS2155;
      _M0L6_2atmpS2152 = _M0L2dtS907 * _M0L6_2atmpS2153;
      _M0L6_2atmpS2150 = _M0L6_2atmpS2151 + _M0L6_2atmpS2152;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2149, _M0L1iS906, _M0L6_2atmpS2150);
      _M0L6_2atmpS2159 = _M0L1iS906 + 1;
      _M0L1iS906 = _M0L6_2atmpS2159;
      continue;
    }
    break;
  }
  _M0L7_2abindS909 = 0;
  _M0L1iS910 = _M0L7_2abindS909;
  while (1) {
    if (_M0L1iS910 < _M0L1nS903) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2160 = _M0L1pS904->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2161;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2162;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2163;
      int32_t _M0L6_2atmpS2164;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S2160, _M0L1iS910, 0x0p+0f);
      _M0L8gaba__d1S2161 = _M0L1pS904->$18;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S2161, _M0L1iS910, 0x0p+0f);
      _M0L7glu__d2S2162 = _M0L1pS904->$19;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S2162, _M0L1iS910, 0x0p+0f);
      _M0L8gaba__d2S2163 = _M0L1pS904->$20;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S2163, _M0L1iS910, 0x0p+0f);
      _M0L6_2atmpS2164 = _M0L1iS910 + 1;
      _M0L1iS910 = _M0L6_2atmpS2164;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS895,
  float _M0L2dtS898
) {
  int32_t _M0L1nS894;
  int32_t _M0L7_2abindS896;
  int32_t _M0L1iS897;
  int32_t _M0L7_2abindS900;
  int32_t _M0L1iS901;
  #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS894 = _M0L1pS895->$27;
  _M0L7_2abindS896 = 0;
  _M0L1iS897 = _M0L7_2abindS896;
  while (1) {
    if (_M0L1iS897 < _M0L1nS894) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2059 = _M0L1pS895->$9;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2064 = _M0L1pS895->$9;
      float _M0L6_2atmpS2061;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2063;
      float _M0L6_2atmpS2062;
      float _M0L6_2atmpS2060;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2065;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2070;
      float _M0L6_2atmpS2067;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2069;
      float _M0L6_2atmpS2068;
      float _M0L6_2atmpS2066;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2071;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2080;
      float _M0L6_2atmpS2073;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2079;
      float _M0L6_2atmpS2078;
      float _M0L6_2atmpS2076;
      float _M0L6tau__eS2077;
      float _M0L6_2atmpS2075;
      float _M0L6_2atmpS2074;
      float _M0L6_2atmpS2072;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2081;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2090;
      float _M0L6_2atmpS2083;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2089;
      float _M0L6_2atmpS2088;
      float _M0L6_2atmpS2086;
      float _M0L6tau__iS2087;
      float _M0L6_2atmpS2085;
      float _M0L6_2atmpS2084;
      float _M0L6_2atmpS2082;
      int32_t _M0L6_2atmpS2091;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2061
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2064, _M0L1iS897);
      _M0L6glu__sS2063 = _M0L1pS895->$15;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2062
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2063, _M0L1iS897);
      _M0L6_2atmpS2060 = _M0L6_2atmpS2061 + _M0L6_2atmpS2062;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2059, _M0L1iS897, _M0L6_2atmpS2060);
      _M0L5gi__sS2065 = _M0L1pS895->$10;
      _M0L5gi__sS2070 = _M0L1pS895->$10;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2067
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2070, _M0L1iS897);
      _M0L7gaba__sS2069 = _M0L1pS895->$16;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2068
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2069, _M0L1iS897);
      _M0L6_2atmpS2066 = _M0L6_2atmpS2067 + _M0L6_2atmpS2068;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2065, _M0L1iS897, _M0L6_2atmpS2066);
      _M0L5ge__sS2071 = _M0L1pS895->$9;
      _M0L5ge__sS2080 = _M0L1pS895->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2073
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2080, _M0L1iS897);
      _M0L5ge__sS2079 = _M0L1pS895->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2078
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2079, _M0L1iS897);
      _M0L6_2atmpS2076 = -_M0L6_2atmpS2078;
      _M0L6tau__eS2077 = _M0L1pS895->$23;
      _M0L6_2atmpS2075 = _M0L6_2atmpS2076 / _M0L6tau__eS2077;
      _M0L6_2atmpS2074 = _M0L2dtS898 * _M0L6_2atmpS2075;
      _M0L6_2atmpS2072 = _M0L6_2atmpS2073 + _M0L6_2atmpS2074;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2071, _M0L1iS897, _M0L6_2atmpS2072);
      _M0L5gi__sS2081 = _M0L1pS895->$10;
      _M0L5gi__sS2090 = _M0L1pS895->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2083
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2090, _M0L1iS897);
      _M0L5gi__sS2089 = _M0L1pS895->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2088
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2089, _M0L1iS897);
      _M0L6_2atmpS2086 = -_M0L6_2atmpS2088;
      _M0L6tau__iS2087 = _M0L1pS895->$24;
      _M0L6_2atmpS2085 = _M0L6_2atmpS2086 / _M0L6tau__iS2087;
      _M0L6_2atmpS2084 = _M0L2dtS898 * _M0L6_2atmpS2085;
      _M0L6_2atmpS2082 = _M0L6_2atmpS2083 + _M0L6_2atmpS2084;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2081, _M0L1iS897, _M0L6_2atmpS2082);
      _M0L6_2atmpS2091 = _M0L1iS897 + 1;
      _M0L1iS897 = _M0L6_2atmpS2091;
      continue;
    }
    break;
  }
  _M0L7_2abindS900 = 0;
  _M0L1iS901 = _M0L7_2abindS900;
  while (1) {
    if (_M0L1iS901 < _M0L1nS894) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2092 = _M0L1pS895->$15;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2093;
      int32_t _M0L6_2atmpS2094;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2092, _M0L1iS901, 0x0p+0f);
      _M0L7gaba__sS2093 = _M0L1pS895->$16;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2093, _M0L1iS901, 0x0p+0f);
      _M0L6_2atmpS2094 = _M0L1iS901 + 1;
      _M0L1iS901 = _M0L6_2atmpS2094;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9TripodHet* _M0MP26RiantR8snn__mbt9TripodHet3new(
  int32_t _M0L1nS850,
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS854,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS862
) {
  struct _M0TPB5ArrayGfE* _M0L1cS849;
  struct _M0TPB5ArrayGfE* _M0L2glS851;
  int32_t _M0L7_2abindS852;
  int32_t _M0L1kS853;
  struct _M0TPB5ArrayGfE* _M0L4v__sS856;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S857;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S858;
  int32_t _M0L7_2abindS859;
  int32_t _M0L1kS860;
  struct _M0TPB5ArrayGfE* _M0L4w__sS864;
  struct _M0TPB5ArrayGbE* _M0L4fireS865;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS866;
  int32_t _M0L7_2abindS867;
  int32_t _M0L1kS868;
  struct _M0TPB5ArrayGiE* _M0L4tabsS870;
  struct _M0TPB5ArrayGfE* _M0L4i__sS871;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S872;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S873;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS874;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS875;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S876;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S877;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S878;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S879;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS880;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS881;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S882;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S883;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S884;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S885;
  int32_t _M0L6total4S886;
  struct _M0TPB5ArrayGfE* _M0L2dvS887;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS888;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS889;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S890;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S891;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S892;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S893;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2058;
  struct _M0TP26RiantR8snn__mbt9TripodHet* _block_2707;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1cS849 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2glS851 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L7_2abindS852 = 0;
  _M0L1kS853 = _M0L7_2abindS852;
  while (1) {
    if (_M0L1kS853 < _M0L1nS850) {
      struct _M0TPB5ArrayGfE* _M0L2tmS2030 = _M0L11soma__paramS854->$3;
      float _M0L6_2atmpS2026;
      struct _M0TPB5ArrayGfE* _M0L1rS2029;
      float _M0L6_2atmpS2028;
      float _M0L6_2atmpS2027;
      float _M0L6_2atmpS2025;
      struct _M0TPB5ArrayGfE* _M0L1rS2033;
      float _M0L6_2atmpS2032;
      float _M0L6_2atmpS2031;
      int32_t _M0L6_2atmpS2034;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2026 = _M0MPC15array5Array2atGfE(_M0L2tmS2030, _M0L1kS853);
      _M0L1rS2029 = _M0L11soma__paramS854->$4;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2028 = _M0MPC15array5Array2atGfE(_M0L1rS2029, _M0L1kS853);
      _M0L6_2atmpS2027 = 0x1p+0f / _M0L6_2atmpS2028;
      _M0L6_2atmpS2025 = _M0L6_2atmpS2026 * _M0L6_2atmpS2027;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L1cS849, _M0L1kS853, _M0L6_2atmpS2025);
      _M0L1rS2033 = _M0L11soma__paramS854->$4;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2032 = _M0MPC15array5Array2atGfE(_M0L1rS2033, _M0L1kS853);
      _M0L6_2atmpS2031 = 0x1p+0f / _M0L6_2atmpS2032;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L2glS851, _M0L1kS853, _M0L6_2atmpS2031);
      _M0L6_2atmpS2034 = _M0L1kS853 + 1;
      _M0L1kS853 = _M0L6_2atmpS2034;
      continue;
    }
    break;
  }
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4v__sS856 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d1S857 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d2S858 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L7_2abindS859 = 0;
  _M0L1kS860 = _M0L7_2abindS859;
  while (1) {
    if (_M0L1kS860 < _M0L1nS850) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2053 = _M0L11soma__paramS854->$0;
      float _M0L6_2atmpS2050;
      struct _M0TPB5ArrayGfE* _M0L2vrS2052;
      float _M0L6_2atmpS2051;
      float _M0L6spreadS861;
      struct _M0TPB5ArrayGfE* _M0L2vrS2039;
      float _M0L6_2atmpS2036;
      float _M0L6_2atmpS2038;
      float _M0L6_2atmpS2037;
      float _M0L6_2atmpS2035;
      struct _M0TPB5ArrayGfE* _M0L2vrS2044;
      float _M0L6_2atmpS2041;
      float _M0L6_2atmpS2043;
      float _M0L6_2atmpS2042;
      float _M0L6_2atmpS2040;
      struct _M0TPB5ArrayGfE* _M0L2vrS2049;
      float _M0L6_2atmpS2046;
      float _M0L6_2atmpS2048;
      float _M0L6_2atmpS2047;
      float _M0L6_2atmpS2045;
      int32_t _M0L6_2atmpS2054;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2050 = _M0MPC15array5Array2atGfE(_M0L2vtS2053, _M0L1kS860);
      _M0L2vrS2052 = _M0L11soma__paramS854->$1;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2051 = _M0MPC15array5Array2atGfE(_M0L2vrS2052, _M0L1kS860);
      _M0L6spreadS861 = _M0L6_2atmpS2050 - _M0L6_2atmpS2051;
      _M0L2vrS2039 = _M0L11soma__paramS854->$1;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2036 = _M0MPC15array5Array2atGfE(_M0L2vrS2039, _M0L1kS860);
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2038 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS862);
      _M0L6_2atmpS2037 = _M0L6_2atmpS2038 * _M0L6spreadS861;
      _M0L6_2atmpS2035 = _M0L6_2atmpS2036 + _M0L6_2atmpS2037;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS856, _M0L1kS860, _M0L6_2atmpS2035);
      _M0L2vrS2044 = _M0L11soma__paramS854->$1;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2041 = _M0MPC15array5Array2atGfE(_M0L2vrS2044, _M0L1kS860);
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2043 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS862);
      _M0L6_2atmpS2042 = _M0L6_2atmpS2043 * _M0L6spreadS861;
      _M0L6_2atmpS2040 = _M0L6_2atmpS2041 + _M0L6_2atmpS2042;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S857, _M0L1kS860, _M0L6_2atmpS2040);
      _M0L2vrS2049 = _M0L11soma__paramS854->$1;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2046 = _M0MPC15array5Array2atGfE(_M0L2vrS2049, _M0L1kS860);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2048 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS862);
      _M0L6_2atmpS2047 = _M0L6_2atmpS2048 * _M0L6spreadS861;
      _M0L6_2atmpS2045 = _M0L6_2atmpS2046 + _M0L6_2atmpS2047;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S858, _M0L1kS860, _M0L6_2atmpS2045);
      _M0L6_2atmpS2054 = _M0L1kS860 + 1;
      _M0L1kS860 = _M0L6_2atmpS2054;
      continue;
    }
    break;
  }
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4w__sS864 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4fireS865 = _M0MPC15array5Array4makeGbE(_M0L1nS850, 0);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L9thresholdS866 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L7_2abindS867 = 0;
  _M0L1kS868 = _M0L7_2abindS867;
  while (1) {
    if (_M0L1kS868 < _M0L1nS850) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2056 = _M0L11soma__paramS854->$0;
      float _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2057;
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2055 = _M0MPC15array5Array2atGfE(_M0L2vtS2056, _M0L1kS868);
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS866, _M0L1kS868, _M0L6_2atmpS2055);
      _M0L6_2atmpS2057 = _M0L1kS868 + 1;
      _M0L1kS868 = _M0L6_2atmpS2057;
      continue;
    }
    break;
  }
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4tabsS870 = _M0MPC15array5Array4makeGiE(_M0L1nS850, 1);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4i__sS871 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d1S872 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d2S873 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5ge__sS874 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5gi__sS875 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d1S876 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d1S877 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d2S878 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d2S879 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6glu__sS880 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7gaba__sS881 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d1S882 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d1S883 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d2S884 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d2S885 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L6total4S886 = _M0L1nS850 * 4;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2dvS887 = _M0MPC15array5Array4makeGfE(_M0L6total4S886, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8dv__tempS888 = _M0MPC15array5Array4makeGfE(_M0L6total4S886, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L12syn__curr__sS889 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d1S890 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d2S891 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d1S892 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS850);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d2S893 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS850);
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6_2atmpS2058 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS854);
  _block_2707
  = (struct _M0TP26RiantR8snn__mbt9TripodHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9TripodHet));
  Moonbit_object_header(_block_2707)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_2707->$0 = _M0L11soma__paramS854;
  _block_2707->$1 = _M0L6_2atmpS2058;
  _block_2707->$2 = _M0L1cS849;
  _block_2707->$3 = _M0L2glS851;
  _block_2707->$4 = _M0L2d1S892;
  _block_2707->$5 = _M0L2d2S893;
  _block_2707->$6 = _M0L4i__sS871;
  _block_2707->$7 = _M0L5i__d1S872;
  _block_2707->$8 = _M0L5i__d2S873;
  _block_2707->$9 = _M0L5ge__sS874;
  _block_2707->$10 = _M0L5gi__sS875;
  _block_2707->$11 = _M0L6ge__d1S876;
  _block_2707->$12 = _M0L6gi__d1S877;
  _block_2707->$13 = _M0L6ge__d2S878;
  _block_2707->$14 = _M0L6gi__d2S879;
  _block_2707->$15 = _M0L6glu__sS880;
  _block_2707->$16 = _M0L7gaba__sS881;
  _block_2707->$17 = _M0L7glu__d1S882;
  _block_2707->$18 = _M0L8gaba__d1S883;
  _block_2707->$19 = _M0L7glu__d2S884;
  _block_2707->$20 = _M0L8gaba__d2S885;
  _block_2707->$21 = 0x0p+0f;
  _block_2707->$22 = -0x1.2cp+6f;
  _block_2707->$23 = 0x1.8p+2f;
  _block_2707->$24 = 0x1p+1f;
  _block_2707->$25 = 0x1p+0f;
  _block_2707->$26 = 0x1p+0f;
  _block_2707->$27 = _M0L1nS850;
  _block_2707->$28 = _M0L4v__sS856;
  _block_2707->$29 = _M0L4w__sS864;
  _block_2707->$30 = _M0L5v__d1S857;
  _block_2707->$31 = _M0L5v__d2S858;
  _block_2707->$32 = _M0L4fireS865;
  _block_2707->$33 = _M0L9thresholdS866;
  _block_2707->$34 = _M0L4tabsS870;
  _block_2707->$35 = _M0L2dvS887;
  _block_2707->$36 = _M0L8dv__tempS888;
  _block_2707->$37 = _M0L12syn__curr__sS889;
  _block_2707->$38 = _M0L13syn__curr__d1S890;
  _block_2707->$39 = _M0L13syn__curr__d2S891;
  return _block_2707;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS842
) {
  struct _M0TPB5ArrayGfE* _M0L2elS841;
  struct _M0TPB5ArrayGfE* _M0L1cS843;
  struct _M0TPB5ArrayGfE* _M0L3gaxS844;
  struct _M0TPB5ArrayGfE* _M0L2gmS845;
  struct _M0TPB5ArrayGfE* _M0L1lS846;
  struct _M0TPB5ArrayGfE* _M0L1dS847;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS848;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_2708;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS841
  = _M0MPC15array5Array4makeGfE(_M0L1nS842, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS843 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS844 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS845 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS846 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS847 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS848 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x0p+0f);
  _block_2708
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_2708)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
  _block_2708->$0 = _M0L1nS842;
  _block_2708->$1 = _M0L2elS841;
  _block_2708->$2 = _M0L1cS843;
  _block_2708->$3 = _M0L3gaxS844;
  _block_2708->$4 = _M0L2gmS845;
  _block_2708->$5 = _M0L1lS846;
  _block_2708->$6 = _M0L1dS847;
  _block_2708->$7 = _M0L11gax__parentS848;
  return _block_2708;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2709;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2709
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2709)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2709->$0 = 0x0p+0f;
  _block_2709->$1 = 0x1.4p+3f;
  _block_2709->$2 = 0x1.4p+3f;
  _block_2709->$3 = 0x1p+0f;
  _block_2709->$4 = 0x1p+0f;
  return _block_2709;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS839
) {
  struct _M0TUmmmmE* _M0L1sS838;
  uint64_t _M0L6_2atmpS2024;
  struct _M0TUmmmmE* _M0L1tS840;
  uint64_t _M0L6_2atmpS2020;
  uint64_t _M0L6_2atmpS2021;
  uint64_t _M0L6_2atmpS2022;
  uint64_t _M0L6_2atmpS2023;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2710;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS838 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS839);
  _M0L6_2atmpS2024 = _M0L1sS838->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS840 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2024);
  _M0L6_2atmpS2020 = _M0L1sS838->$0;
  _M0L6_2atmpS2021 = _M0L1sS838->$1;
  _M0L6_2atmpS2022 = _M0L1sS838->$2;
  moonbit_decref_cycle_free(_M0L1sS838);
  _M0L6_2atmpS2023 = _M0L1tS840->$0;
  moonbit_decref_cycle_free(_M0L1tS840);
  _block_2710
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2710)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2710->$0 = _M0L6_2atmpS2020;
  _block_2710->$1 = _M0L6_2atmpS2021;
  _block_2710->$2 = _M0L6_2atmpS2022;
  _block_2710->$3 = _M0L6_2atmpS2023;
  return _block_2710;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS830) {
  uint64_t _M0L2s1S829;
  uint64_t _M0L2z1S831;
  uint64_t _M0L2s2S832;
  uint64_t _M0L2z2S833;
  uint64_t _M0L2s3S834;
  uint64_t _M0L2z3S835;
  uint64_t _M0L2s4S836;
  uint64_t _M0L2z4S837;
  struct _M0TUmmmmE* _block_2711;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S829 = _M0L4seedS830 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S831 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S829);
  _M0L2s2S832 = _M0L2s1S829 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S833 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S832);
  _M0L2s3S834 = _M0L2s2S832 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S835 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S834);
  _M0L2s4S836 = _M0L2s3S834 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S837 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S836);
  _block_2711 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2711)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2711->$0 = _M0L2z1S831;
  _block_2711->$1 = _M0L2z2S833;
  _block_2711->$2 = _M0L2z3S835;
  _block_2711->$3 = _M0L2z4S837;
  return _block_2711;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS827) {
  uint64_t _M0L6_2atmpS2019;
  uint64_t _M0L6_2atmpS2018;
  uint64_t _M0L1zS826;
  uint64_t _M0L6_2atmpS2017;
  uint64_t _M0L6_2atmpS2016;
  uint64_t _M0L1zS828;
  uint64_t _M0L6_2atmpS2015;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2019 = _M0L1zS827 >> 30;
  _M0L6_2atmpS2018 = _M0L1zS827 ^ _M0L6_2atmpS2019;
  _M0L1zS826 = _M0L6_2atmpS2018 * 13787848793156543929ull;
  _M0L6_2atmpS2017 = _M0L1zS826 >> 27;
  _M0L6_2atmpS2016 = _M0L1zS826 ^ _M0L6_2atmpS2017;
  _M0L1zS828 = _M0L6_2atmpS2016 * 10723151780598845931ull;
  _M0L6_2atmpS2015 = _M0L1zS828 >> 31;
  return _M0L1zS828 ^ _M0L6_2atmpS2015;
}

struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0MP26RiantR8snn__mbt12PoissonLayer7with__n(
  float _M0L4rateS825,
  int32_t _M0L10n__sourcesS823
) {
  uint8_t* _M0L6_2atmpS2014;
  struct _M0TPB5ArrayGbE* _M0L6activeS820;
  int32_t _M0L7_2abindS821;
  int32_t _M0L2__S822;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _block_2713;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L6_2atmpS2014 = (uint8_t*)moonbit_empty_int8_array;
  _M0L6activeS820
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6activeS820)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _M0L6activeS820->$0 = _M0L6_2atmpS2014;
  _M0L6activeS820->$1 = 0;
  _M0L7_2abindS821 = 0;
  _M0L2__S822 = _M0L7_2abindS821;
  while (1) {
    if (_M0L2__S822 < _M0L10n__sourcesS823) {
      int32_t _M0L6_2atmpS2013;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array4pushGbE(_M0L6activeS820, 1);
      _M0L6_2atmpS2013 = _M0L2__S822 + 1;
      _M0L2__S822 = _M0L6_2atmpS2013;
      continue;
    }
    break;
  }
  _block_2713
  = (struct _M0TP26RiantR8snn__mbt12PoissonLayer*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonLayer));
  Moonbit_object_header(_block_2713)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _block_2713->$0 = _M0L4rateS825;
  _block_2713->$1 = _M0L10n__sourcesS823;
  _block_2713->$2 = _M0L6activeS820;
  _block_2713->$3 = 0x1p+0f;
  _block_2713->$4 = 0x0p+0f;
  _block_2713->$5 = 0x1p+0f;
  _block_2713->$6 = (moonbit_string_t)moonbit_string_literal_9.data;
  _block_2713->$7 = (moonbit_string_t)moonbit_string_literal_9.data;
  return _block_2713;
}

int32_t _M0FP26RiantR8snn__mbt24stimulate__layer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0L1sS806,
  float _M0L4timeS804,
  float _M0L2dtS809
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2012;
  int32_t _M0L6n__preS805;
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2011;
  int32_t _M0L7n__postS807;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2010;
  float _M0L4rateS2009;
  float _M0L6lambdaS808;
  struct _M0TPB5ArrayGfE* _M0L3bufS810;
  int32_t _M0L7_2abindS811;
  int32_t _M0L1iS812;
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L5paramS2012 = _M0L1sS806->$0;
  _M0L6n__preS805 = _M0L5paramS2012->$1;
  _M0L4postS2011 = _M0L1sS806->$1;
  _M0L7n__postS807 = _M0L4postS2011->$27;
  _M0L5paramS2010 = _M0L1sS806->$0;
  _M0L4rateS2009 = _M0L5paramS2010->$0;
  _M0L6lambdaS808 = _M0L4rateS2009 * _M0L2dtS809;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L3bufS810 = _M0FP26RiantR8snn__mbt22target__buffer__tripod(_M0L1sS806);
  if (_M0L6lambdaS808 <= 0x0p+0f) {
    moonbit_decref_cycle_free(_M0L3bufS810);
    return 0;
  }
  _M0L7_2abindS811 = 0;
  _M0L1iS812 = _M0L7_2abindS811;
  while (1) {
    if (_M0L1iS812 < _M0L6n__preS805) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2000 =
        _M0L1sS806->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS1999 = _M0L5paramS2000->$2;
      int32_t _M0L6_2atmpS1998;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2008;
      int32_t _M0L1kS815;
      int32_t _M0L6_2atmpS1997;
      moonbit_incref_cycle_free(_M0L6activeS1999);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
      _M0L6_2atmpS1998
      = _M0MPC15array5Array2atGbE(_M0L6activeS1999, _M0L1iS812);
      moonbit_decref_cycle_free(_M0L6activeS1999);
      if (!_M0L6_2atmpS1998) {
        goto join_813;
      }
      _M0L3rngS2008 = _M0L1sS806->$6;
      #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
      _M0L1kS815
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2008, _M0L6lambdaS808);
      if (_M0L1kS815 > 0) {
        int32_t _M0L7_2abindS816 = 0;
        int32_t _M0L1jS817 = _M0L7_2abindS816;
        while (1) {
          if (_M0L1jS817 < _M0L7n__postS807) {
            int32_t _M0L6_2atmpS2006 = _M0L1jS817 * _M0L6n__preS805;
            int32_t _M0L3idxS818 = _M0L6_2atmpS2006 + _M0L1iS812;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2001 = _M0L1sS806->$3;
            int32_t _M0L6_2atmpS2007;
            #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2001, _M0L3idxS818)
            ) {
              float _M0L6_2atmpS2003;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2005;
              float _M0L6_2atmpS2004;
              float _M0L6_2atmpS2002;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS2003
              = _M0MPC15array5Array2atGfE(_M0L3bufS810, _M0L1jS817);
              _M0L7weightsS2005 = _M0L1sS806->$2;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS2004
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2005, _M0L3idxS818);
              _M0L6_2atmpS2002 = _M0L6_2atmpS2003 + _M0L6_2atmpS2004;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0MPC15array5Array3setGfE(_M0L3bufS810, _M0L1jS817, _M0L6_2atmpS2002);
            }
            _M0L6_2atmpS2007 = _M0L1jS817 + 1;
            _M0L1jS817 = _M0L6_2atmpS2007;
            continue;
          }
          break;
        }
      }
      goto join_813;
      goto joinlet_2715;
      join_813:;
      _M0L6_2atmpS1997 = _M0L1iS812 + 1;
      _M0L1iS812 = _M0L6_2atmpS1997;
      continue;
      joinlet_2715:;
    } else {
      moonbit_decref_cycle_free(_M0L3bufS810);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS802,
  float _M0L6lambdaS796
) {
  float _M0L6_2atmpS1996;
  float _M0L6_2atmpS1995;
  double _M0L1lS797;
  struct _M0TPB8MutLocalGdE* _M0L1pS798;
  struct _M0TPB8MutLocalGiE* _M0L1kS799;
  float _M0L6_2atmpS1994;
  int32_t _M0L8ten__lamS801;
  int32_t _M0L3capS800;
  int32_t _M0L3valS1993;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS796 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS1996 = -_M0L6lambdaS796;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1995 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1996);
  _M0L1lS797 = (double)_M0L6_2atmpS1995;
  _M0L1pS798
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS798->$0 = 0x1p+0;
  _M0L1kS799
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS799->$0 = 0;
  _M0L6_2atmpS1994 = _M0L6lambdaS796 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS801 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1994);
  if (_M0L8ten__lamS801 > 100) {
    _M0L3capS800 = _M0L8ten__lamS801;
  } else {
    _M0L3capS800 = 100;
  }
  while (1) {
    int32_t _M0L3valS1985 = _M0L1kS799->$0;
    int32_t _M0L6_2atmpS1984 = _M0L3valS1985 + 1;
    double _M0L3valS1987;
    double _M0L6_2atmpS1988;
    double _M0L6_2atmpS1986;
    double _M0L3valS1989;
    int32_t _M0L3valS1991;
    _M0L1kS799->$0 = _M0L6_2atmpS1984;
    _M0L3valS1987 = _M0L1pS798->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS1988 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS802);
    _M0L6_2atmpS1986 = _M0L3valS1987 * _M0L6_2atmpS1988;
    _M0L1pS798->$0 = _M0L6_2atmpS1986;
    _M0L3valS1989 = _M0L1pS798->$0;
    if (_M0L3valS1989 < _M0L1lS797) {
      int32_t _M0L3valS1990;
      moonbit_decref_cycle_free(_M0L1pS798);
      _M0L3valS1990 = _M0L1kS799->$0;
      moonbit_decref_cycle_free(_M0L1kS799);
      return _M0L3valS1990 - 1;
    }
    _M0L3valS1991 = _M0L1kS799->$0;
    if (_M0L3valS1991 > _M0L3capS800) {
      int32_t _M0L3valS1992;
      moonbit_decref_cycle_free(_M0L1pS798);
      _M0L3valS1992 = _M0L1kS799->$0;
      moonbit_decref_cycle_free(_M0L1kS799);
      return _M0L3valS1992 - 1;
    }
    continue;
    break;
  }
  _M0L3valS1993 = _M0L1kS799->$0;
  moonbit_decref_cycle_free(_M0L1kS799);
  return _M0L3valS1993 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS794
) {
  uint64_t _M0L1uS793;
  uint64_t _M0L4bitsS795;
  double _M0L6_2atmpS1983;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS793 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS794);
  _M0L4bitsS795 = _M0L1uS793 >> 11;
  _M0L6_2atmpS1983 = (double)_M0L4bitsS795;
  return _M0L6_2atmpS1983 * 0x1p-53;
}

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt22target__buffer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0L1sS791
) {
  moonbit_string_t _M0L7_2abindS790;
  moonbit_string_t _M0L7_2abindS792;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L7_2abindS790 = _M0L1sS791->$4;
  _M0L7_2abindS792 = _M0L1sS791->$5;
  if (
    _M0L7_2abindS790 == (moonbit_string_t)moonbit_string_literal_13.data
    || Moonbit_array_length(_M0L7_2abindS790) == 4
       && 0
          == memcmp(_M0L7_2abindS790, (moonbit_string_t)moonbit_string_literal_13.data, 8)
  ) {
    if (
      _M0L7_2abindS792 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS792) == 3
         && 0
            == memcmp(_M0L7_2abindS792, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1976 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2636 = _M0L4postS1976->$15;
      moonbit_incref_cycle_free(_M0L8_2afieldS2636);
      return _M0L8_2afieldS2636;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1977 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2637 = _M0L4postS1977->$16;
      moonbit_incref_cycle_free(_M0L8_2afieldS2637);
      return _M0L8_2afieldS2637;
    }
  } else if (
           _M0L7_2abindS790
           == (moonbit_string_t)moonbit_string_literal_12.data
           || Moonbit_array_length(_M0L7_2abindS790) == 2
              && 0
                 == memcmp(_M0L7_2abindS790, (moonbit_string_t)moonbit_string_literal_12.data, 4)
         ) {
    if (
      _M0L7_2abindS792 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS792) == 3
         && 0
            == memcmp(_M0L7_2abindS792, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1978 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2638 = _M0L4postS1978->$17;
      moonbit_incref_cycle_free(_M0L8_2afieldS2638);
      return _M0L8_2afieldS2638;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1979 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2639 = _M0L4postS1979->$18;
      moonbit_incref_cycle_free(_M0L8_2afieldS2639);
      return _M0L8_2afieldS2639;
    }
  } else if (
           _M0L7_2abindS790
           == (moonbit_string_t)moonbit_string_literal_10.data
           || Moonbit_array_length(_M0L7_2abindS790) == 2
              && 0
                 == memcmp(_M0L7_2abindS790, (moonbit_string_t)moonbit_string_literal_10.data, 4)
         ) {
    if (
      _M0L7_2abindS792 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS792) == 3
         && 0
            == memcmp(_M0L7_2abindS792, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1980 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2640 = _M0L4postS1980->$19;
      moonbit_incref_cycle_free(_M0L8_2afieldS2640);
      return _M0L8_2afieldS2640;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1981 =
        _M0L1sS791->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2641 = _M0L4postS1981->$20;
      moonbit_incref_cycle_free(_M0L8_2afieldS2641);
      return _M0L8_2afieldS2641;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1982 = _M0L1sS791->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2642 = _M0L4postS1982->$17;
    moonbit_incref_cycle_free(_M0L8_2afieldS2642);
    return _M0L8_2afieldS2642;
  }
}

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0MP26RiantR8snn__mbt26PoissonLayerStimulusTripod3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS771,
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS773,
  moonbit_string_t _M0L19target__compartmentS788,
  moonbit_string_t _M0L12target__kindS789,
  float _M0L2muS784,
  float _M0L5sigmaS785,
  float _M0L7p__connS783,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS782
) {
  int32_t _M0L6n__preS770;
  int32_t _M0L7n__postS772;
  int32_t _M0L6_2atmpS1975;
  struct _M0TPB5ArrayGfE* _M0L7weightsS774;
  int32_t _M0L6_2atmpS1974;
  struct _M0TPB5ArrayGbE* _M0L12connectivityS775;
  moonbit_string_t _M0L4distS1973;
  int32_t _M0L11use__normalS776;
  int32_t _M0L7_2abindS777;
  int32_t _M0L1iS778;
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _block_2720;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L6n__preS770 = _M0L5paramS771->$1;
  _M0L7n__postS772 = _M0L4postS773->$27;
  _M0L6_2atmpS1975 = _M0L7n__postS772 * _M0L6n__preS770;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L7weightsS774 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS1975, 0x0p+0f);
  _M0L6_2atmpS1974 = _M0L7n__postS772 * _M0L6n__preS770;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L12connectivityS775 = _M0MPC15array5Array4makeGbE(_M0L6_2atmpS1974, 0);
  _M0L4distS1973 = _M0L5paramS771->$6;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L11use__normalS776
  = _M0L4distS1973 == (moonbit_string_t)moonbit_string_literal_14.data
    || Moonbit_array_length(_M0L4distS1973)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_14.data)
       && 0
          == memcmp(_M0L4distS1973, (moonbit_string_t)moonbit_string_literal_14.data, Moonbit_array_length(_M0L4distS1973) * 2);
  _M0L7_2abindS777 = 0;
  _M0L1iS778 = _M0L7_2abindS777;
  while (1) {
    if (_M0L1iS778 < _M0L6n__preS770) {
      int32_t _M0L7_2abindS779 = 0;
      int32_t _M0L1jS780 = _M0L7_2abindS779;
      int32_t _M0L6_2atmpS1972;
      while (1) {
        if (_M0L1jS780 < _M0L7n__postS772) {
          int32_t _M0L6_2atmpS1970 = _M0L1jS780 * _M0L6n__preS770;
          int32_t _M0L3idxS781 = _M0L6_2atmpS1970 + _M0L1iS778;
          float _M0L6_2atmpS1966;
          int32_t _M0L6_2atmpS1971;
          #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
          _M0L6_2atmpS1966 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS782);
          if (_M0L6_2atmpS1966 < _M0L7p__connS783) {
            float _M0L6_2atmpS1967;
            #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            _M0MPC15array5Array3setGbE(_M0L12connectivityS775, _M0L3idxS781, 1);
            if (_M0L11use__normalS776) {
              float _M0L6_2atmpS1969;
              float _M0L6_2atmpS1968;
              #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS1969
              = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS782);
              _M0L6_2atmpS1968 = _M0L6_2atmpS1969 * _M0L5sigmaS785;
              _M0L6_2atmpS1967 = _M0L2muS784 + _M0L6_2atmpS1968;
            } else {
              _M0L6_2atmpS1967 = _M0L2muS784;
            }
            #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            _M0MPC15array5Array3setGfE(_M0L7weightsS774, _M0L3idxS781, _M0L6_2atmpS1967);
          }
          _M0L6_2atmpS1971 = _M0L1jS780 + 1;
          _M0L1jS780 = _M0L6_2atmpS1971;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1972 = _M0L1iS778 + 1;
      _M0L1iS778 = _M0L6_2atmpS1972;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS771);
  moonbit_incref_cycle_free(_M0L4postS773);
  moonbit_incref_cycle_free(_M0L19target__compartmentS788);
  moonbit_incref_cycle_free(_M0L12target__kindS789);
  moonbit_incref_cycle_free(_M0L3rngS782);
  _block_2720
  = (struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod));
  Moonbit_object_header(_block_2720)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 81, 0);
  _block_2720->$0 = _M0L5paramS771;
  _block_2720->$1 = _M0L4postS773;
  _block_2720->$2 = _M0L7weightsS774;
  _block_2720->$3 = _M0L12connectivityS775;
  _block_2720->$4 = _M0L19target__compartmentS788;
  _block_2720->$5 = _M0L12target__kindS789;
  _block_2720->$6 = _M0L3rngS782;
  return _block_2720;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS768
) {
  uint32_t _M0L1uS767;
  uint32_t _M0L4bitsS769;
  double _M0L6_2atmpS1965;
  double _M0L6_2atmpS1964;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS767 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS768);
  _M0L4bitsS769 = _M0L1uS767 >> 8;
  _M0L6_2atmpS1965 = (double)_M0L4bitsS769;
  _M0L6_2atmpS1964 = _M0L6_2atmpS1965 * 0x1p-24;
  return (float)_M0L6_2atmpS1964;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS766
) {
  uint64_t _M0L1uS765;
  uint64_t _M0L6_2atmpS1963;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS765 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS766);
  _M0L6_2atmpS1963 = _M0L1uS765 >> 32;
  return (uint32_t)_M0L6_2atmpS1963;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS758
) {
  uint64_t _M0L2s0S757;
  uint64_t _M0L2s1S759;
  uint64_t _M0L2s2S760;
  uint64_t _M0L2s3S761;
  uint64_t _M0L3tmpS762;
  uint64_t _M0L6_2atmpS1962;
  uint64_t _M0L3resS763;
  uint64_t _M0L1tS764;
  uint64_t _M0L6_2atmpS1952;
  uint64_t _M0L6_2atmpS1953;
  uint64_t _M0L2s2S1955;
  uint64_t _M0L6_2atmpS1954;
  uint64_t _M0L2s3S1957;
  uint64_t _M0L6_2atmpS1956;
  uint64_t _M0L2s2S1959;
  uint64_t _M0L6_2atmpS1958;
  uint64_t _M0L2s3S1961;
  uint64_t _M0L6_2atmpS1960;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S757 = _M0L1rS758->$0;
  _M0L2s1S759 = _M0L1rS758->$1;
  _M0L2s2S760 = _M0L1rS758->$2;
  _M0L2s3S761 = _M0L1rS758->$3;
  _M0L3tmpS762 = _M0L2s0S757 + _M0L2s3S761;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1962 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS762, 23);
  _M0L3resS763 = _M0L6_2atmpS1962 + _M0L2s0S757;
  _M0L1tS764 = _M0L2s1S759 << 17;
  _M0L6_2atmpS1952 = _M0L2s2S760 ^ _M0L2s0S757;
  _M0L1rS758->$2 = _M0L6_2atmpS1952;
  _M0L6_2atmpS1953 = _M0L2s3S761 ^ _M0L2s1S759;
  _M0L1rS758->$3 = _M0L6_2atmpS1953;
  _M0L2s2S1955 = _M0L1rS758->$2;
  _M0L6_2atmpS1954 = _M0L2s1S759 ^ _M0L2s2S1955;
  _M0L1rS758->$1 = _M0L6_2atmpS1954;
  _M0L2s3S1957 = _M0L1rS758->$3;
  _M0L6_2atmpS1956 = _M0L2s0S757 ^ _M0L2s3S1957;
  _M0L1rS758->$0 = _M0L6_2atmpS1956;
  _M0L2s2S1959 = _M0L1rS758->$2;
  _M0L6_2atmpS1958 = _M0L2s2S1959 ^ _M0L1tS764;
  _M0L1rS758->$2 = _M0L6_2atmpS1958;
  _M0L2s3S1961 = _M0L1rS758->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1960 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1961, 45);
  _M0L1rS758->$3 = _M0L6_2atmpS1960;
  return _M0L3resS763;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS755, int32_t _M0L1kS756) {
  uint64_t _M0L6_2atmpS1949;
  int32_t _M0L6_2atmpS1951;
  uint64_t _M0L6_2atmpS1950;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1949 = _M0L1xS755 << (_M0L1kS756 & 63);
  _M0L6_2atmpS1951 = 64 - _M0L1kS756;
  _M0L6_2atmpS1950 = _M0L1xS755 >> (_M0L6_2atmpS1951 & 63);
  return _M0L6_2atmpS1949 | _M0L6_2atmpS1950;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS754) {
  double _M0L6_2atmpS1948;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1948 = (double)_M0L4selfS754;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1948);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS753) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS753 != _M0L4selfS753) {
    return 0;
  } else if (_M0L4selfS753 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS753 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS753;
  }
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS739,
  int32_t _M0L4elemS741
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS738;
  int32_t _M0L1iS740;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS738 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS739);
  _M0L1iS740 = 0;
  while (1) {
    if (_M0L1iS740 < _M0L3lenS739) {
      int32_t* _M0L3bufS1942 = _M0L3arrS738->$0;
      int32_t _M0L6_2atmpS1943;
      _M0L3bufS1942[_M0L1iS740] = _M0L4elemS741;
      _M0L6_2atmpS1943 = _M0L1iS740 + 1;
      _M0L1iS740 = _M0L6_2atmpS1943;
      continue;
    }
    break;
  }
  return _M0L3arrS738;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS744,
  float _M0L4elemS746
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS743;
  int32_t _M0L1iS745;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS743 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS744);
  _M0L1iS745 = 0;
  while (1) {
    if (_M0L1iS745 < _M0L3lenS744) {
      float* _M0L3bufS1944 = _M0L3arrS743->$0;
      int32_t _M0L6_2atmpS1945;
      _M0L3bufS1944[_M0L1iS745] = _M0L4elemS746;
      _M0L6_2atmpS1945 = _M0L1iS745 + 1;
      _M0L1iS745 = _M0L6_2atmpS1945;
      continue;
    }
    break;
  }
  return _M0L3arrS743;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS749,
  int32_t _M0L4elemS751
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS748;
  int32_t _M0L1iS750;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS748 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS749);
  _M0L1iS750 = 0;
  while (1) {
    if (_M0L1iS750 < _M0L3lenS749) {
      uint8_t* _M0L3bufS1946 = _M0L3arrS748->$0;
      int32_t _M0L6_2atmpS1947;
      _M0L3bufS1946[_M0L1iS750] = _M0L4elemS751;
      _M0L6_2atmpS1947 = _M0L1iS750 + 1;
      _M0L1iS750 = _M0L6_2atmpS1947;
      continue;
    }
    break;
  }
  return _M0L3arrS748;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS727,
  int32_t _M0L5indexS728,
  int32_t _M0L5valueS729
) {
  int32_t _M0L3lenS726;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS726 = _M0L4selfS727->$1;
  if (_M0L5indexS728 >= 0 && _M0L5indexS728 < _M0L3lenS726) {
    int32_t* _M0L6_2atmpS1939;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1939 = _M0MPC15array5Array6bufferGiE(_M0L4selfS727);
    _M0L6_2atmpS1939[_M0L5indexS728] = _M0L5valueS729;
    moonbit_decref_cycle_free(_M0L6_2atmpS1939);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS731,
  int32_t _M0L5indexS732,
  float _M0L5valueS733
) {
  int32_t _M0L3lenS730;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS730 = _M0L4selfS731->$1;
  if (_M0L5indexS732 >= 0 && _M0L5indexS732 < _M0L3lenS730) {
    float* _M0L6_2atmpS1940;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1940 = _M0MPC15array5Array6bufferGfE(_M0L4selfS731);
    _M0L6_2atmpS1940[_M0L5indexS732] = _M0L5valueS733;
    moonbit_decref_cycle_free(_M0L6_2atmpS1940);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS735,
  int32_t _M0L5indexS736,
  int32_t _M0L5valueS737
) {
  int32_t _M0L3lenS734;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS734 = _M0L4selfS735->$1;
  if (_M0L5indexS736 >= 0 && _M0L5indexS736 < _M0L3lenS734) {
    uint8_t* _M0L6_2atmpS1941;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1941 = _M0MPC15array5Array6bufferGbE(_M0L4selfS735);
    _M0L6_2atmpS1941[_M0L5indexS736] = _M0L5valueS737;
    moonbit_decref_cycle_free(_M0L6_2atmpS1941);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS715,
  int32_t _M0L5indexS716
) {
  int32_t _M0L3lenS714;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS714 = _M0L4selfS715->$1;
  if (_M0L5indexS716 >= 0 && _M0L5indexS716 < _M0L3lenS714) {
    uint8_t* _M0L6_2atmpS1935;
    int32_t _result_2724;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1935 = _M0MPC15array5Array6bufferGbE(_M0L4selfS715);
    _result_2724 = (int32_t)_M0L6_2atmpS1935[_M0L5indexS716];
    moonbit_decref_cycle_free(_M0L6_2atmpS1935);
    return _result_2724;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS718,
  int32_t _M0L5indexS719
) {
  int32_t _M0L3lenS717;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS717 = _M0L4selfS718->$1;
  if (_M0L5indexS719 >= 0 && _M0L5indexS719 < _M0L3lenS717) {
    int32_t* _M0L6_2atmpS1936;
    int32_t _result_2725;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1936 = _M0MPC15array5Array6bufferGiE(_M0L4selfS718);
    _result_2725 = (int32_t)_M0L6_2atmpS1936[_M0L5indexS719];
    moonbit_decref_cycle_free(_M0L6_2atmpS1936);
    return _result_2725;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS721,
  int32_t _M0L5indexS722
) {
  int32_t _M0L3lenS720;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS720 = _M0L4selfS721->$1;
  if (_M0L5indexS722 >= 0 && _M0L5indexS722 < _M0L3lenS720) {
    float* _M0L6_2atmpS1937;
    float _result_2726;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1937 = _M0MPC15array5Array6bufferGfE(_M0L4selfS721);
    _result_2726 = (float)_M0L6_2atmpS1937[_M0L5indexS722];
    moonbit_decref_cycle_free(_M0L6_2atmpS1937);
    return _result_2726;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS724,
  int32_t _M0L5indexS725
) {
  int32_t _M0L3lenS723;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS723 = _M0L4selfS724->$1;
  if (_M0L5indexS725 >= 0 && _M0L5indexS725 < _M0L3lenS723) {
    moonbit_string_t* _M0L6_2atmpS1938;
    moonbit_string_t _M0L6_2atmpS2643;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1938 = _M0MPC15array5Array6bufferGsE(_M0L4selfS724);
    _M0L6_2atmpS2643 = (moonbit_string_t)_M0L6_2atmpS1938[_M0L5indexS725];
    moonbit_incref_cycle_free(_M0L6_2atmpS2643);
    moonbit_decref_cycle_free(_M0L6_2atmpS1938);
    return _M0L6_2atmpS2643;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS713) {
  moonbit_string_t _M0L6_2atmpS1934;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1934 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS713);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1934);
  moonbit_decref_cycle_free(_M0L6_2atmpS1934);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS712) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS712);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS697) {
  uint64_t _M0L4bitsS700;
  uint64_t _M0L6_2atmpS1933;
  uint64_t _M0L6_2atmpS1932;
  int32_t _M0L8ieeeSignS701;
  uint64_t _M0L12ieeeMantissaS702;
  uint64_t _M0L6_2atmpS1931;
  uint64_t _M0L6_2atmpS1930;
  int32_t _M0L12ieeeExponentS703;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS704;
  struct _M0TPB17FloatingDecimal64* _M0L1vS705;
  moonbit_string_t _result_2728;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS697 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L3valS697 >= -0x1p+53 && _M0L3valS697 <= 0x1p+53) {
    if (_M0L3valS697 >= -0x1p+31 && _M0L3valS697 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS698;
      double _M0L6_2atmpS1919;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS698 = _M0MPC16double6Double7to__int(_M0L3valS697);
      _M0L6_2atmpS1919 = (double)_M0L1iS698;
      if (_M0L6_2atmpS1919 == _M0L3valS697) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS698, 10);
      }
    } else {
      int64_t _M0L1iS699;
      double _M0L6_2atmpS1920;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS699 = _M0MPC16double6Double9to__int64(_M0L3valS697);
      _M0L6_2atmpS1920 = (double)_M0L1iS699;
      if (_M0L6_2atmpS1920 == _M0L3valS697) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS699, 10);
      }
    }
  }
  _M0L4bitsS700 = *(int64_t*)&_M0L3valS697;
  _M0L6_2atmpS1933 = _M0L4bitsS700 >> 63;
  _M0L6_2atmpS1932 = _M0L6_2atmpS1933 & 1ull;
  _M0L8ieeeSignS701 = _M0L6_2atmpS1932 != 0ull;
  _M0L12ieeeMantissaS702 = _M0L4bitsS700 & 4503599627370495ull;
  _M0L6_2atmpS1931 = _M0L4bitsS700 >> 52;
  _M0L6_2atmpS1930 = _M0L6_2atmpS1931 & 2047ull;
  _M0L12ieeeExponentS703 = (int32_t)_M0L6_2atmpS1930;
  if (
    _M0L12ieeeExponentS703 == 2047
    || _M0L12ieeeExponentS703 == 0 && _M0L12ieeeMantissaS702 == 0ull
  ) {
    int32_t _M0L6_2atmpS1921 = _M0L12ieeeExponentS703 != 0;
    int32_t _M0L6_2atmpS1922 = _M0L12ieeeMantissaS702 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS701, _M0L6_2atmpS1921, _M0L6_2atmpS1922);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS704
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS702, _M0L12ieeeExponentS703);
  if (_M0L7_2abindS704 == 0) {
    uint32_t _M0L6_2atmpS1923;
    if (_M0L7_2abindS704) {
      moonbit_decref_cycle_free(_M0L7_2abindS704);
    }
    _M0L6_2atmpS1923 = *(uint32_t*)&_M0L12ieeeExponentS703;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS705 = _M0FPB3d2d(_M0L12ieeeMantissaS702, _M0L6_2atmpS1923);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS706 = _M0L7_2abindS704;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS707 = _M0L7_2aSomeS706;
    struct _M0TPB17FloatingDecimal64* _M0L1xS708 = _M0L4_2afS707;
    while (1) {
      uint64_t _M0L8mantissaS1929 = _M0L1xS708->$0;
      uint64_t _M0L1qS709 = _M0L8mantissaS1929 / 10ull;
      uint64_t _M0L8mantissaS1927 = _M0L1xS708->$0;
      uint64_t _M0L6_2atmpS1928 = 10ull * _M0L1qS709;
      uint64_t _M0L1rS710 = _M0L8mantissaS1927 - _M0L6_2atmpS1928;
      int32_t _M0L8exponentS1926;
      int32_t _M0L6_2atmpS1925;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1924;
      if (_M0L1rS710 != 0ull) {
        _M0L1vS705 = _M0L1xS708;
        break;
      }
      _M0L8exponentS1926 = _M0L1xS708->$1;
      moonbit_decref_cycle_free(_M0L1xS708);
      _M0L6_2atmpS1925 = _M0L8exponentS1926 + 1;
      _M0L6_2atmpS1924
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1924)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1924->$0 = _M0L1qS709;
      _M0L6_2atmpS1924->$1 = _M0L6_2atmpS1925;
      _M0L1xS708 = _M0L6_2atmpS1924;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2728 = _M0FPB9to__chars(_M0L1vS705, _M0L8ieeeSignS701);
  moonbit_decref_cycle_free(_M0L1vS705);
  return _result_2728;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS692,
  int32_t _M0L12ieeeExponentS694
) {
  uint64_t _M0L2m2S691;
  int32_t _M0L6_2atmpS1918;
  int32_t _M0L2e2S693;
  int32_t _M0L6_2atmpS1917;
  uint64_t _M0L6_2atmpS1916;
  uint64_t _M0L4maskS695;
  uint64_t _M0L8fractionS696;
  int32_t _M0L6_2atmpS1915;
  uint64_t _M0L6_2atmpS1914;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1913;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S691 = 4503599627370496ull | _M0L12ieeeMantissaS692;
  _M0L6_2atmpS1918 = _M0L12ieeeExponentS694 - 1023;
  _M0L2e2S693 = _M0L6_2atmpS1918 - 52;
  if (_M0L2e2S693 > 0) {
    return 0;
  }
  if (_M0L2e2S693 < -52) {
    return 0;
  }
  _M0L6_2atmpS1917 = -_M0L2e2S693;
  _M0L6_2atmpS1916 = 1ull << (_M0L6_2atmpS1917 & 63);
  _M0L4maskS695 = _M0L6_2atmpS1916 - 1ull;
  _M0L8fractionS696 = _M0L2m2S691 & _M0L4maskS695;
  if (_M0L8fractionS696 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1915 = -_M0L2e2S693;
  _M0L6_2atmpS1914 = _M0L2m2S691 >> (_M0L6_2atmpS1915 & 63);
  _M0L6_2atmpS1913
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1913)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1913->$0 = _M0L6_2atmpS1914;
  _M0L6_2atmpS1913->$1 = 0;
  return _M0L6_2atmpS1913;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS659,
  int32_t _M0L4signS657
) {
  moonbit_bytes_t _M0L6resultS655;
  int32_t _M0Lm5indexS656;
  uint64_t _M0L6outputS658;
  int32_t _M0L7olengthS660;
  int32_t _M0L8exponentS1912;
  int32_t _M0L6_2atmpS1911;
  int32_t _M0Lm3expS661;
  int32_t _M0L6_2atmpS1910;
  int32_t _M0L6_2atmpS1908;
  int32_t _M0L18scientificNotationS662;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS655 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS656 = 0;
  if (_M0L4signS657) {
    int32_t _M0L6_2atmpS1782 = _M0Lm5indexS656;
    int32_t _M0L6_2atmpS1783;
    if (
      _M0L6_2atmpS1782 < 0
      || _M0L6_2atmpS1782 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1782] = 45;
    _M0L6_2atmpS1783 = _M0Lm5indexS656;
    _M0Lm5indexS656 = _M0L6_2atmpS1783 + 1;
  }
  _M0L6outputS658 = _M0L1vS659->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS660 = _M0FPB17decimal__length17(_M0L6outputS658);
  _M0L8exponentS1912 = _M0L1vS659->$1;
  _M0L6_2atmpS1911 = _M0L8exponentS1912 + _M0L7olengthS660;
  _M0Lm3expS661 = _M0L6_2atmpS1911 - 1;
  _M0L6_2atmpS1910 = _M0Lm3expS661;
  if (_M0L6_2atmpS1910 >= -6) {
    int32_t _M0L6_2atmpS1909 = _M0Lm3expS661;
    _M0L6_2atmpS1908 = _M0L6_2atmpS1909 < 21;
  } else {
    _M0L6_2atmpS1908 = 0;
  }
  _M0L18scientificNotationS662 = !_M0L6_2atmpS1908;
  if (_M0L18scientificNotationS662) {
    int32_t _M0L7_2abindS663 = _M0L7olengthS660 - 1;
    uint64_t _M0L6outputS664;
    int32_t _M0L1iS665 = 0;
    uint64_t _M0L6outputS666 = _M0L6outputS658;
    int32_t _M0L6_2atmpS1784;
    int32_t _M0L6_2atmpS1788;
    int32_t _M0L6_2atmpS1787;
    int32_t _M0L6_2atmpS1786;
    int32_t _M0L6_2atmpS1785;
    int32_t _M0L6_2atmpS1792;
    int32_t _M0L6_2atmpS1793;
    int32_t _M0L6_2atmpS1794;
    int32_t _M0L6_2atmpS1795;
    int32_t _M0L6_2atmpS1796;
    int32_t _M0L6_2atmpS1802;
    int32_t _M0L6_2atmpS1835;
    moonbit_string_t _result_2730;
    while (1) {
      if (_M0L1iS665 < _M0L7_2abindS663) {
        uint64_t _M0L1cS667 = _M0L6outputS666 % 10ull;
        int32_t _M0L6_2atmpS1841 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1840 = _M0L6_2atmpS1841 + _M0L7olengthS660;
        int32_t _M0L6_2atmpS1836 = _M0L6_2atmpS1840 - _M0L1iS665;
        int32_t _M0L6_2atmpS1839 = (int32_t)_M0L1cS667;
        int32_t _M0L6_2atmpS1838 = 48 + _M0L6_2atmpS1839;
        int32_t _M0L6_2atmpS1837 = _M0L6_2atmpS1838 & 0xff;
        int32_t _M0L6_2atmpS1842;
        uint64_t _M0L6_2atmpS1843;
        if (
          _M0L6_2atmpS1836 < 0
          || _M0L6_2atmpS1836 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1836] = _M0L6_2atmpS1837;
        _M0L6_2atmpS1842 = _M0L1iS665 + 1;
        _M0L6_2atmpS1843 = _M0L6outputS666 / 10ull;
        _M0L1iS665 = _M0L6_2atmpS1842;
        _M0L6outputS666 = _M0L6_2atmpS1843;
        continue;
      } else {
        _M0L6outputS664 = _M0L6outputS666;
      }
      break;
    }
    _M0L6_2atmpS1784 = _M0Lm5indexS656;
    _M0L6_2atmpS1788 = (int32_t)_M0L6outputS664;
    _M0L6_2atmpS1787 = _M0L6_2atmpS1788 % 10;
    _M0L6_2atmpS1786 = 48 + _M0L6_2atmpS1787;
    _M0L6_2atmpS1785 = _M0L6_2atmpS1786 & 0xff;
    if (
      _M0L6_2atmpS1784 < 0
      || _M0L6_2atmpS1784 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1784] = _M0L6_2atmpS1785;
    if (_M0L7olengthS660 > 1) {
      int32_t _M0L6_2atmpS1790 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1789 = _M0L6_2atmpS1790 + 1;
      if (
        _M0L6_2atmpS1789 < 0
        || _M0L6_2atmpS1789 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1789] = 46;
    } else {
      int32_t _M0L6_2atmpS1791 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1791 - 1;
    }
    _M0L6_2atmpS1792 = _M0Lm5indexS656;
    _M0L6_2atmpS1793 = _M0L7olengthS660 + 1;
    _M0Lm5indexS656 = _M0L6_2atmpS1792 + _M0L6_2atmpS1793;
    _M0L6_2atmpS1794 = _M0Lm5indexS656;
    if (
      _M0L6_2atmpS1794 < 0
      || _M0L6_2atmpS1794 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1794] = 101;
    _M0L6_2atmpS1795 = _M0Lm5indexS656;
    _M0Lm5indexS656 = _M0L6_2atmpS1795 + 1;
    _M0L6_2atmpS1796 = _M0Lm3expS661;
    if (_M0L6_2atmpS1796 < 0) {
      int32_t _M0L6_2atmpS1797 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1798;
      int32_t _M0L6_2atmpS1799;
      if (
        _M0L6_2atmpS1797 < 0
        || _M0L6_2atmpS1797 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1797] = 45;
      _M0L6_2atmpS1798 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1798 + 1;
      _M0L6_2atmpS1799 = _M0Lm3expS661;
      _M0Lm3expS661 = -_M0L6_2atmpS1799;
    } else {
      int32_t _M0L6_2atmpS1800 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1801;
      if (
        _M0L6_2atmpS1800 < 0
        || _M0L6_2atmpS1800 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1800] = 43;
      _M0L6_2atmpS1801 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1801 + 1;
    }
    _M0L6_2atmpS1802 = _M0Lm3expS661;
    if (_M0L6_2atmpS1802 >= 100) {
      int32_t _M0L6_2atmpS1818 = _M0Lm3expS661;
      int32_t _M0L1aS669 = _M0L6_2atmpS1818 / 100;
      int32_t _M0L6_2atmpS1817 = _M0Lm3expS661;
      int32_t _M0L6_2atmpS1816 = _M0L6_2atmpS1817 / 10;
      int32_t _M0L1bS670 = _M0L6_2atmpS1816 % 10;
      int32_t _M0L6_2atmpS1815 = _M0Lm3expS661;
      int32_t _M0L1cS671 = _M0L6_2atmpS1815 % 10;
      int32_t _M0L6_2atmpS1803 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1805 = 48 + _M0L1aS669;
      int32_t _M0L6_2atmpS1804 = _M0L6_2atmpS1805 & 0xff;
      int32_t _M0L6_2atmpS1809;
      int32_t _M0L6_2atmpS1806;
      int32_t _M0L6_2atmpS1808;
      int32_t _M0L6_2atmpS1807;
      int32_t _M0L6_2atmpS1813;
      int32_t _M0L6_2atmpS1810;
      int32_t _M0L6_2atmpS1812;
      int32_t _M0L6_2atmpS1811;
      int32_t _M0L6_2atmpS1814;
      if (
        _M0L6_2atmpS1803 < 0
        || _M0L6_2atmpS1803 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1803] = _M0L6_2atmpS1804;
      _M0L6_2atmpS1809 = _M0Lm5indexS656;
      _M0L6_2atmpS1806 = _M0L6_2atmpS1809 + 1;
      _M0L6_2atmpS1808 = 48 + _M0L1bS670;
      _M0L6_2atmpS1807 = _M0L6_2atmpS1808 & 0xff;
      if (
        _M0L6_2atmpS1806 < 0
        || _M0L6_2atmpS1806 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1806] = _M0L6_2atmpS1807;
      _M0L6_2atmpS1813 = _M0Lm5indexS656;
      _M0L6_2atmpS1810 = _M0L6_2atmpS1813 + 2;
      _M0L6_2atmpS1812 = 48 + _M0L1cS671;
      _M0L6_2atmpS1811 = _M0L6_2atmpS1812 & 0xff;
      if (
        _M0L6_2atmpS1810 < 0
        || _M0L6_2atmpS1810 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1810] = _M0L6_2atmpS1811;
      _M0L6_2atmpS1814 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1814 + 3;
    } else {
      int32_t _M0L6_2atmpS1819 = _M0Lm3expS661;
      if (_M0L6_2atmpS1819 >= 10) {
        int32_t _M0L6_2atmpS1829 = _M0Lm3expS661;
        int32_t _M0L1aS672 = _M0L6_2atmpS1829 / 10;
        int32_t _M0L6_2atmpS1828 = _M0Lm3expS661;
        int32_t _M0L1bS673 = _M0L6_2atmpS1828 % 10;
        int32_t _M0L6_2atmpS1820 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1822 = 48 + _M0L1aS672;
        int32_t _M0L6_2atmpS1821 = _M0L6_2atmpS1822 & 0xff;
        int32_t _M0L6_2atmpS1826;
        int32_t _M0L6_2atmpS1823;
        int32_t _M0L6_2atmpS1825;
        int32_t _M0L6_2atmpS1824;
        int32_t _M0L6_2atmpS1827;
        if (
          _M0L6_2atmpS1820 < 0
          || _M0L6_2atmpS1820 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1820] = _M0L6_2atmpS1821;
        _M0L6_2atmpS1826 = _M0Lm5indexS656;
        _M0L6_2atmpS1823 = _M0L6_2atmpS1826 + 1;
        _M0L6_2atmpS1825 = 48 + _M0L1bS673;
        _M0L6_2atmpS1824 = _M0L6_2atmpS1825 & 0xff;
        if (
          _M0L6_2atmpS1823 < 0
          || _M0L6_2atmpS1823 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1823] = _M0L6_2atmpS1824;
        _M0L6_2atmpS1827 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1827 + 2;
      } else {
        int32_t _M0L6_2atmpS1830 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1833 = _M0Lm3expS661;
        int32_t _M0L6_2atmpS1832 = 48 + _M0L6_2atmpS1833;
        int32_t _M0L6_2atmpS1831 = _M0L6_2atmpS1832 & 0xff;
        int32_t _M0L6_2atmpS1834;
        if (
          _M0L6_2atmpS1830 < 0
          || _M0L6_2atmpS1830 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1830] = _M0L6_2atmpS1831;
        _M0L6_2atmpS1834 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1834 + 1;
      }
    }
    _M0L6_2atmpS1835 = _M0Lm5indexS656;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2730
    = _M0FPB19string__from__bytes(_M0L6resultS655, 0, _M0L6_2atmpS1835);
    moonbit_decref_cycle_free(_M0L6resultS655);
    return _result_2730;
  } else {
    int32_t _M0L6_2atmpS1844 = _M0Lm3expS661;
    int32_t _M0L6_2atmpS1907;
    moonbit_string_t _result_2736;
    if (_M0L6_2atmpS1844 < 0) {
      int32_t _M0L6_2atmpS1845 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1847;
      int32_t _M0L6_2atmpS1846;
      int32_t _M0L6_2atmpS1848;
      int32_t _M0L1iS674;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L7currentS676;
      int32_t _M0L1iS677;
      uint64_t _M0L6outputS678;
      if (
        _M0L6_2atmpS1845 < 0
        || _M0L6_2atmpS1845 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1845] = 48;
      _M0L6_2atmpS1847 = _M0Lm5indexS656;
      _M0L6_2atmpS1846 = _M0L6_2atmpS1847 + 1;
      if (
        _M0L6_2atmpS1846 < 0
        || _M0L6_2atmpS1846 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1846] = 46;
      _M0L6_2atmpS1848 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1848 + 2;
      _M0L1iS674 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1849 = _M0Lm3expS661;
        if (_M0L1iS674 > _M0L6_2atmpS1849) {
          int32_t _M0L6_2atmpS1852 = _M0Lm5indexS656;
          int32_t _M0L6_2atmpS1851 = _M0L6_2atmpS1852 - _M0L1iS674;
          int32_t _M0L6_2atmpS1850 = _M0L6_2atmpS1851 - 1;
          int32_t _M0L6_2atmpS1853;
          if (
            _M0L6_2atmpS1850 < 0
            || _M0L6_2atmpS1850 >= Moonbit_array_length(_M0L6resultS655)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS655[_M0L6_2atmpS1850] = 48;
          _M0L6_2atmpS1853 = _M0L1iS674 - 1;
          _M0L1iS674 = _M0L6_2atmpS1853;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1863 = _M0Lm5indexS656;
      _M0L6_2atmpS1865 = _M0Lm3expS661;
      _M0L6_2atmpS1864 = -1 - _M0L6_2atmpS1865;
      _M0L7currentS676 = _M0L6_2atmpS1863 + _M0L6_2atmpS1864;
      _M0L1iS677 = 0;
      _M0L6outputS678 = _M0L6outputS658;
      while (1) {
        if (_M0L1iS677 < _M0L7olengthS660) {
          int32_t _M0L6_2atmpS1860 = _M0L7currentS676 + _M0L7olengthS660;
          int32_t _M0L6_2atmpS1859 = _M0L6_2atmpS1860 - _M0L1iS677;
          int32_t _M0L6_2atmpS1854 = _M0L6_2atmpS1859 - 1;
          uint64_t _M0L6_2atmpS1858 = _M0L6outputS678 % 10ull;
          int32_t _M0L6_2atmpS1857 = (int32_t)_M0L6_2atmpS1858;
          int32_t _M0L6_2atmpS1856 = 48 + _M0L6_2atmpS1857;
          int32_t _M0L6_2atmpS1855 = _M0L6_2atmpS1856 & 0xff;
          int32_t _M0L6_2atmpS1861;
          uint64_t _M0L6_2atmpS1862;
          if (
            _M0L6_2atmpS1854 < 0
            || _M0L6_2atmpS1854 >= Moonbit_array_length(_M0L6resultS655)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS655[_M0L6_2atmpS1854] = _M0L6_2atmpS1855;
          _M0L6_2atmpS1861 = _M0L1iS677 + 1;
          _M0L6_2atmpS1862 = _M0L6outputS678 / 10ull;
          _M0L1iS677 = _M0L6_2atmpS1861;
          _M0L6outputS678 = _M0L6_2atmpS1862;
          continue;
        }
        break;
      }
      _M0Lm5indexS656 = _M0L7currentS676 + _M0L7olengthS660;
    } else {
      int32_t _M0L6_2atmpS1867 = _M0Lm3expS661;
      int32_t _M0L6_2atmpS1866 = _M0L6_2atmpS1867 + 1;
      if (_M0L6_2atmpS1866 >= _M0L7olengthS660) {
        int32_t _M0L1iS680 = 0;
        uint64_t _M0L6outputS681 = _M0L6outputS658;
        int32_t _M0L6_2atmpS1878;
        int32_t _M0L6_2atmpS1883;
        int32_t _M0L7_2abindS683;
        int32_t _M0L1iS684;
        int32_t _M0L6_2atmpS1884;
        int32_t _M0L6_2atmpS1887;
        int32_t _M0L6_2atmpS1886;
        int32_t _M0L6_2atmpS1885;
        while (1) {
          if (_M0L1iS680 < _M0L7olengthS660) {
            int32_t _M0L6_2atmpS1875 = _M0Lm5indexS656;
            int32_t _M0L6_2atmpS1874 = _M0L6_2atmpS1875 + _M0L7olengthS660;
            int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 - _M0L1iS680;
            int32_t _M0L6_2atmpS1868 = _M0L6_2atmpS1873 - 1;
            uint64_t _M0L6_2atmpS1872 = _M0L6outputS681 % 10ull;
            int32_t _M0L6_2atmpS1871 = (int32_t)_M0L6_2atmpS1872;
            int32_t _M0L6_2atmpS1870 = 48 + _M0L6_2atmpS1871;
            int32_t _M0L6_2atmpS1869 = _M0L6_2atmpS1870 & 0xff;
            int32_t _M0L6_2atmpS1876;
            uint64_t _M0L6_2atmpS1877;
            if (
              _M0L6_2atmpS1868 < 0
              || _M0L6_2atmpS1868 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1868] = _M0L6_2atmpS1869;
            _M0L6_2atmpS1876 = _M0L1iS680 + 1;
            _M0L6_2atmpS1877 = _M0L6outputS681 / 10ull;
            _M0L1iS680 = _M0L6_2atmpS1876;
            _M0L6outputS681 = _M0L6_2atmpS1877;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1878 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1878 + _M0L7olengthS660;
        _M0L6_2atmpS1883 = _M0Lm3expS661;
        _M0L7_2abindS683 = _M0L6_2atmpS1883 + 1;
        _M0L1iS684 = _M0L7olengthS660;
        while (1) {
          if (_M0L1iS684 < _M0L7_2abindS683) {
            int32_t _M0L6_2atmpS1881 = _M0Lm5indexS656;
            int32_t _M0L6_2atmpS1880 = _M0L6_2atmpS1881 + _M0L1iS684;
            int32_t _M0L6_2atmpS1879 = _M0L6_2atmpS1880 - _M0L7olengthS660;
            int32_t _M0L6_2atmpS1882;
            if (
              _M0L6_2atmpS1879 < 0
              || _M0L6_2atmpS1879 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1879] = 48;
            _M0L6_2atmpS1882 = _M0L1iS684 + 1;
            _M0L1iS684 = _M0L6_2atmpS1882;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1884 = _M0Lm5indexS656;
        _M0L6_2atmpS1887 = _M0Lm3expS661;
        _M0L6_2atmpS1886 = _M0L6_2atmpS1887 + 1;
        _M0L6_2atmpS1885 = _M0L6_2atmpS1886 - _M0L7olengthS660;
        _M0Lm5indexS656 = _M0L6_2atmpS1884 + _M0L6_2atmpS1885;
      } else {
        int32_t _M0L6_2atmpS1904 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1903 = _M0L6_2atmpS1904 + 1;
        int32_t _M0L1iS686 = 0;
        int32_t _M0L7currentS687 = _M0L6_2atmpS1903;
        uint64_t _M0L6outputS688 = _M0L6outputS658;
        int32_t _M0L6_2atmpS1905;
        int32_t _M0L6_2atmpS1906;
        while (1) {
          if (_M0L1iS686 < _M0L7olengthS660) {
            int32_t _M0L6_2atmpS1899 = _M0L7olengthS660 - _M0L1iS686;
            int32_t _M0L6_2atmpS1897 = _M0L6_2atmpS1899 - 1;
            int32_t _M0L6_2atmpS1898 = _M0Lm3expS661;
            int32_t _M0L7currentS689;
            int32_t _M0L6_2atmpS1894;
            int32_t _M0L6_2atmpS1893;
            int32_t _M0L6_2atmpS1888;
            uint64_t _M0L6_2atmpS1892;
            int32_t _M0L6_2atmpS1891;
            int32_t _M0L6_2atmpS1890;
            int32_t _M0L6_2atmpS1889;
            int32_t _M0L6_2atmpS1895;
            uint64_t _M0L6_2atmpS1896;
            if (_M0L6_2atmpS1897 == _M0L6_2atmpS1898) {
              int32_t _M0L6_2atmpS1902 = _M0L7currentS687 + _M0L7olengthS660;
              int32_t _M0L6_2atmpS1901 = _M0L6_2atmpS1902 - _M0L1iS686;
              int32_t _M0L6_2atmpS1900 = _M0L6_2atmpS1901 - 1;
              if (
                _M0L6_2atmpS1900 < 0
                || _M0L6_2atmpS1900 >= Moonbit_array_length(_M0L6resultS655)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS655[_M0L6_2atmpS1900] = 46;
              _M0L7currentS689 = _M0L7currentS687 - 1;
            } else {
              _M0L7currentS689 = _M0L7currentS687;
            }
            _M0L6_2atmpS1894 = _M0L7currentS689 + _M0L7olengthS660;
            _M0L6_2atmpS1893 = _M0L6_2atmpS1894 - _M0L1iS686;
            _M0L6_2atmpS1888 = _M0L6_2atmpS1893 - 1;
            _M0L6_2atmpS1892 = _M0L6outputS688 % 10ull;
            _M0L6_2atmpS1891 = (int32_t)_M0L6_2atmpS1892;
            _M0L6_2atmpS1890 = 48 + _M0L6_2atmpS1891;
            _M0L6_2atmpS1889 = _M0L6_2atmpS1890 & 0xff;
            if (
              _M0L6_2atmpS1888 < 0
              || _M0L6_2atmpS1888 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1888] = _M0L6_2atmpS1889;
            _M0L6_2atmpS1895 = _M0L1iS686 + 1;
            _M0L6_2atmpS1896 = _M0L6outputS688 / 10ull;
            _M0L1iS686 = _M0L6_2atmpS1895;
            _M0L7currentS687 = _M0L7currentS689;
            _M0L6outputS688 = _M0L6_2atmpS1896;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1905 = _M0Lm5indexS656;
        _M0L6_2atmpS1906 = _M0L7olengthS660 + 1;
        _M0Lm5indexS656 = _M0L6_2atmpS1905 + _M0L6_2atmpS1906;
      }
    }
    _M0L6_2atmpS1907 = _M0Lm5indexS656;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2736
    = _M0FPB19string__from__bytes(_M0L6resultS655, 0, _M0L6_2atmpS1907);
    moonbit_decref_cycle_free(_M0L6resultS655);
    return _result_2736;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS601,
  uint32_t _M0L12ieeeExponentS600
) {
  int32_t _M0Lm2e2S598;
  uint64_t _M0Lm2m2S599;
  uint64_t _M0L6_2atmpS1781;
  uint64_t _M0L6_2atmpS1780;
  int32_t _M0L4evenS602;
  uint64_t _M0L6_2atmpS1779;
  uint64_t _M0L2mvS603;
  int32_t _M0L7mmShiftS604;
  uint64_t _M0Lm2vrS605;
  uint64_t _M0Lm2vpS606;
  uint64_t _M0Lm2vmS607;
  int32_t _M0Lm3e10S608;
  int32_t _M0Lm17vmIsTrailingZerosS609;
  int32_t _M0Lm17vrIsTrailingZerosS610;
  int32_t _M0L6_2atmpS1681;
  int32_t _M0Lm7removedS629;
  int32_t _M0Lm16lastRemovedDigitS630;
  uint64_t _M0Lm6outputS631;
  int32_t _M0L6_2atmpS1777;
  int32_t _M0L6_2atmpS1778;
  int32_t _M0L3expS654;
  uint64_t _M0L6_2atmpS1776;
  struct _M0TPB17FloatingDecimal64* _block_2742;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S598 = 0;
  _M0Lm2m2S599 = 0ull;
  if (_M0L12ieeeExponentS600 == 0u) {
    _M0Lm2e2S598 = -1076;
    _M0Lm2m2S599 = _M0L12ieeeMantissaS601;
  } else {
    int32_t _M0L6_2atmpS1680 = *(int32_t*)&_M0L12ieeeExponentS600;
    int32_t _M0L6_2atmpS1679 = _M0L6_2atmpS1680 - 1023;
    int32_t _M0L6_2atmpS1678 = _M0L6_2atmpS1679 - 52;
    _M0Lm2e2S598 = _M0L6_2atmpS1678 - 2;
    _M0Lm2m2S599 = 4503599627370496ull | _M0L12ieeeMantissaS601;
  }
  _M0L6_2atmpS1781 = _M0Lm2m2S599;
  _M0L6_2atmpS1780 = _M0L6_2atmpS1781 & 1ull;
  _M0L4evenS602 = _M0L6_2atmpS1780 == 0ull;
  _M0L6_2atmpS1779 = _M0Lm2m2S599;
  _M0L2mvS603 = 4ull * _M0L6_2atmpS1779;
  _M0L7mmShiftS604
  = _M0L12ieeeMantissaS601 != 0ull || _M0L12ieeeExponentS600 <= 1u;
  _M0Lm2vrS605 = 0ull;
  _M0Lm2vpS606 = 0ull;
  _M0Lm2vmS607 = 0ull;
  _M0Lm3e10S608 = 0;
  _M0Lm17vmIsTrailingZerosS609 = 0;
  _M0Lm17vrIsTrailingZerosS610 = 0;
  _M0L6_2atmpS1681 = _M0Lm2e2S598;
  if (_M0L6_2atmpS1681 >= 0) {
    int32_t _M0L6_2atmpS1703 = _M0Lm2e2S598;
    int32_t _M0L6_2atmpS1699;
    int32_t _M0L6_2atmpS1702;
    int32_t _M0L6_2atmpS1701;
    int32_t _M0L6_2atmpS1700;
    int32_t _M0L1qS611;
    int32_t _M0L6_2atmpS1698;
    int32_t _M0L6_2atmpS1697;
    int32_t _M0L1kS612;
    int32_t _M0L6_2atmpS1696;
    int32_t _M0L6_2atmpS1695;
    int32_t _M0L6_2atmpS1694;
    int32_t _M0L1iS613;
    struct _M0TPB8Pow5Pair _M0L4pow5S614;
    uint64_t _M0L6_2atmpS1693;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS615;
    uint64_t _M0L8_2avrOutS616;
    uint64_t _M0L8_2avpOutS617;
    uint64_t _M0L8_2avmOutS618;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1699 = _M0FPB9log10Pow2(_M0L6_2atmpS1703);
    _M0L6_2atmpS1702 = _M0Lm2e2S598;
    _M0L6_2atmpS1701 = _M0L6_2atmpS1702 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1700 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1701);
    _M0L1qS611 = _M0L6_2atmpS1699 - _M0L6_2atmpS1700;
    _M0Lm3e10S608 = _M0L1qS611;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1698 = _M0FPB8pow5bits(_M0L1qS611);
    _M0L6_2atmpS1697 = 125 + _M0L6_2atmpS1698;
    _M0L1kS612 = _M0L6_2atmpS1697 - 1;
    _M0L6_2atmpS1696 = _M0Lm2e2S598;
    _M0L6_2atmpS1695 = -_M0L6_2atmpS1696;
    _M0L6_2atmpS1694 = _M0L6_2atmpS1695 + _M0L1qS611;
    _M0L1iS613 = _M0L6_2atmpS1694 + _M0L1kS612;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S614 = _M0FPB22double__computeInvPow5(_M0L1qS611);
    _M0L6_2atmpS1693 = _M0Lm2m2S599;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS615
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1693, _M0L4pow5S614, _M0L1iS613, _M0L7mmShiftS604);
    _M0L8_2avrOutS616 = _M0L7_2abindS615.$0;
    _M0L8_2avpOutS617 = _M0L7_2abindS615.$1;
    _M0L8_2avmOutS618 = _M0L7_2abindS615.$2;
    _M0Lm2vrS605 = _M0L8_2avrOutS616;
    _M0Lm2vpS606 = _M0L8_2avpOutS617;
    _M0Lm2vmS607 = _M0L8_2avmOutS618;
    if (_M0L1qS611 <= 21) {
      int32_t _M0L6_2atmpS1689 = (int32_t)_M0L2mvS603;
      uint64_t _M0L6_2atmpS1692 = _M0L2mvS603 / 5ull;
      int32_t _M0L6_2atmpS1691 = (int32_t)_M0L6_2atmpS1692;
      int32_t _M0L6_2atmpS1690 = 5 * _M0L6_2atmpS1691;
      int32_t _M0L6mvMod5S619 = _M0L6_2atmpS1689 - _M0L6_2atmpS1690;
      if (_M0L6mvMod5S619 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS610
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS603, _M0L1qS611);
      } else if (_M0L4evenS602) {
        uint64_t _M0L6_2atmpS1683 = _M0L2mvS603 - 1ull;
        uint64_t _M0L6_2atmpS1684;
        uint64_t _M0L6_2atmpS1682;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1684 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS604);
        _M0L6_2atmpS1682 = _M0L6_2atmpS1683 - _M0L6_2atmpS1684;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS609
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1682, _M0L1qS611);
      } else {
        uint64_t _M0L6_2atmpS1685 = _M0Lm2vpS606;
        uint64_t _M0L6_2atmpS1688 = _M0L2mvS603 + 2ull;
        int32_t _M0L6_2atmpS1687;
        uint64_t _M0L6_2atmpS1686;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1687
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1688, _M0L1qS611);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1686 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1687);
        _M0Lm2vpS606 = _M0L6_2atmpS1685 - _M0L6_2atmpS1686;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1717 = _M0Lm2e2S598;
    int32_t _M0L6_2atmpS1716 = -_M0L6_2atmpS1717;
    int32_t _M0L6_2atmpS1711;
    int32_t _M0L6_2atmpS1715;
    int32_t _M0L6_2atmpS1714;
    int32_t _M0L6_2atmpS1713;
    int32_t _M0L6_2atmpS1712;
    int32_t _M0L1qS620;
    int32_t _M0L6_2atmpS1704;
    int32_t _M0L6_2atmpS1710;
    int32_t _M0L6_2atmpS1709;
    int32_t _M0L1iS621;
    int32_t _M0L6_2atmpS1708;
    int32_t _M0L1kS622;
    int32_t _M0L1jS623;
    struct _M0TPB8Pow5Pair _M0L4pow5S624;
    uint64_t _M0L6_2atmpS1707;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS625;
    uint64_t _M0L8_2avrOutS626;
    uint64_t _M0L8_2avpOutS627;
    uint64_t _M0L8_2avmOutS628;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1711 = _M0FPB9log10Pow5(_M0L6_2atmpS1716);
    _M0L6_2atmpS1715 = _M0Lm2e2S598;
    _M0L6_2atmpS1714 = -_M0L6_2atmpS1715;
    _M0L6_2atmpS1713 = _M0L6_2atmpS1714 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1712 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1713);
    _M0L1qS620 = _M0L6_2atmpS1711 - _M0L6_2atmpS1712;
    _M0L6_2atmpS1704 = _M0Lm2e2S598;
    _M0Lm3e10S608 = _M0L1qS620 + _M0L6_2atmpS1704;
    _M0L6_2atmpS1710 = _M0Lm2e2S598;
    _M0L6_2atmpS1709 = -_M0L6_2atmpS1710;
    _M0L1iS621 = _M0L6_2atmpS1709 - _M0L1qS620;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1708 = _M0FPB8pow5bits(_M0L1iS621);
    _M0L1kS622 = _M0L6_2atmpS1708 - 125;
    _M0L1jS623 = _M0L1qS620 - _M0L1kS622;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S624 = _M0FPB19double__computePow5(_M0L1iS621);
    _M0L6_2atmpS1707 = _M0Lm2m2S599;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS625
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1707, _M0L4pow5S624, _M0L1jS623, _M0L7mmShiftS604);
    _M0L8_2avrOutS626 = _M0L7_2abindS625.$0;
    _M0L8_2avpOutS627 = _M0L7_2abindS625.$1;
    _M0L8_2avmOutS628 = _M0L7_2abindS625.$2;
    _M0Lm2vrS605 = _M0L8_2avrOutS626;
    _M0Lm2vpS606 = _M0L8_2avpOutS627;
    _M0Lm2vmS607 = _M0L8_2avmOutS628;
    if (_M0L1qS620 <= 1) {
      _M0Lm17vrIsTrailingZerosS610 = 1;
      if (_M0L4evenS602) {
        int32_t _M0L6_2atmpS1705;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1705 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS604);
        _M0Lm17vmIsTrailingZerosS609 = _M0L6_2atmpS1705 == 1;
      } else {
        uint64_t _M0L6_2atmpS1706 = _M0Lm2vpS606;
        _M0Lm2vpS606 = _M0L6_2atmpS1706 - 1ull;
      }
    } else if (_M0L1qS620 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS610
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS603, _M0L1qS620);
    }
  }
  _M0Lm7removedS629 = 0;
  _M0Lm16lastRemovedDigitS630 = 0;
  _M0Lm6outputS631 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS609 || _M0Lm17vrIsTrailingZerosS610) {
    int32_t _if__result_2739;
    uint64_t _M0L6_2atmpS1747;
    uint64_t _M0L6_2atmpS1753;
    uint64_t _M0L6_2atmpS1754;
    int32_t _if__result_2740;
    int32_t _M0L6_2atmpS1750;
    int64_t _M0L6_2atmpS1749;
    uint64_t _M0L6_2atmpS1748;
    while (1) {
      uint64_t _M0L6_2atmpS1730 = _M0Lm2vpS606;
      uint64_t _M0L7vpDiv10S632 = _M0L6_2atmpS1730 / 10ull;
      uint64_t _M0L6_2atmpS1729 = _M0Lm2vmS607;
      uint64_t _M0L7vmDiv10S633 = _M0L6_2atmpS1729 / 10ull;
      uint64_t _M0L6_2atmpS1728;
      int32_t _M0L6_2atmpS1725;
      int32_t _M0L6_2atmpS1727;
      int32_t _M0L6_2atmpS1726;
      int32_t _M0L7vmMod10S635;
      uint64_t _M0L6_2atmpS1724;
      uint64_t _M0L7vrDiv10S636;
      uint64_t _M0L6_2atmpS1723;
      int32_t _M0L6_2atmpS1720;
      int32_t _M0L6_2atmpS1722;
      int32_t _M0L6_2atmpS1721;
      int32_t _M0L7vrMod10S637;
      int32_t _M0L6_2atmpS1719;
      if (_M0L7vpDiv10S632 <= _M0L7vmDiv10S633) {
        break;
      }
      _M0L6_2atmpS1728 = _M0Lm2vmS607;
      _M0L6_2atmpS1725 = (int32_t)_M0L6_2atmpS1728;
      _M0L6_2atmpS1727 = (int32_t)_M0L7vmDiv10S633;
      _M0L6_2atmpS1726 = 10 * _M0L6_2atmpS1727;
      _M0L7vmMod10S635 = _M0L6_2atmpS1725 - _M0L6_2atmpS1726;
      _M0L6_2atmpS1724 = _M0Lm2vrS605;
      _M0L7vrDiv10S636 = _M0L6_2atmpS1724 / 10ull;
      _M0L6_2atmpS1723 = _M0Lm2vrS605;
      _M0L6_2atmpS1720 = (int32_t)_M0L6_2atmpS1723;
      _M0L6_2atmpS1722 = (int32_t)_M0L7vrDiv10S636;
      _M0L6_2atmpS1721 = 10 * _M0L6_2atmpS1722;
      _M0L7vrMod10S637 = _M0L6_2atmpS1720 - _M0L6_2atmpS1721;
      _M0Lm17vmIsTrailingZerosS609
      = _M0Lm17vmIsTrailingZerosS609 && _M0L7vmMod10S635 == 0;
      if (_M0Lm17vrIsTrailingZerosS610) {
        int32_t _M0L6_2atmpS1718 = _M0Lm16lastRemovedDigitS630;
        _M0Lm17vrIsTrailingZerosS610 = _M0L6_2atmpS1718 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS610 = 0;
      }
      _M0Lm16lastRemovedDigitS630 = _M0L7vrMod10S637;
      _M0Lm2vrS605 = _M0L7vrDiv10S636;
      _M0Lm2vpS606 = _M0L7vpDiv10S632;
      _M0Lm2vmS607 = _M0L7vmDiv10S633;
      _M0L6_2atmpS1719 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1719 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS609) {
      while (1) {
        uint64_t _M0L6_2atmpS1743 = _M0Lm2vmS607;
        uint64_t _M0L7vmDiv10S638 = _M0L6_2atmpS1743 / 10ull;
        uint64_t _M0L6_2atmpS1742 = _M0Lm2vmS607;
        int32_t _M0L6_2atmpS1739 = (int32_t)_M0L6_2atmpS1742;
        int32_t _M0L6_2atmpS1741 = (int32_t)_M0L7vmDiv10S638;
        int32_t _M0L6_2atmpS1740 = 10 * _M0L6_2atmpS1741;
        int32_t _M0L7vmMod10S639 = _M0L6_2atmpS1739 - _M0L6_2atmpS1740;
        uint64_t _M0L6_2atmpS1738;
        uint64_t _M0L7vpDiv10S641;
        uint64_t _M0L6_2atmpS1737;
        uint64_t _M0L7vrDiv10S642;
        uint64_t _M0L6_2atmpS1736;
        int32_t _M0L6_2atmpS1733;
        int32_t _M0L6_2atmpS1735;
        int32_t _M0L6_2atmpS1734;
        int32_t _M0L7vrMod10S643;
        int32_t _M0L6_2atmpS1732;
        if (_M0L7vmMod10S639 != 0) {
          break;
        }
        _M0L6_2atmpS1738 = _M0Lm2vpS606;
        _M0L7vpDiv10S641 = _M0L6_2atmpS1738 / 10ull;
        _M0L6_2atmpS1737 = _M0Lm2vrS605;
        _M0L7vrDiv10S642 = _M0L6_2atmpS1737 / 10ull;
        _M0L6_2atmpS1736 = _M0Lm2vrS605;
        _M0L6_2atmpS1733 = (int32_t)_M0L6_2atmpS1736;
        _M0L6_2atmpS1735 = (int32_t)_M0L7vrDiv10S642;
        _M0L6_2atmpS1734 = 10 * _M0L6_2atmpS1735;
        _M0L7vrMod10S643 = _M0L6_2atmpS1733 - _M0L6_2atmpS1734;
        if (_M0Lm17vrIsTrailingZerosS610) {
          int32_t _M0L6_2atmpS1731 = _M0Lm16lastRemovedDigitS630;
          _M0Lm17vrIsTrailingZerosS610 = _M0L6_2atmpS1731 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS610 = 0;
        }
        _M0Lm16lastRemovedDigitS630 = _M0L7vrMod10S643;
        _M0Lm2vrS605 = _M0L7vrDiv10S642;
        _M0Lm2vpS606 = _M0L7vpDiv10S641;
        _M0Lm2vmS607 = _M0L7vmDiv10S638;
        _M0L6_2atmpS1732 = _M0Lm7removedS629;
        _M0Lm7removedS629 = _M0L6_2atmpS1732 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS610) {
      int32_t _M0L6_2atmpS1746 = _M0Lm16lastRemovedDigitS630;
      if (_M0L6_2atmpS1746 == 5) {
        uint64_t _M0L6_2atmpS1745 = _M0Lm2vrS605;
        uint64_t _M0L6_2atmpS1744 = _M0L6_2atmpS1745 % 2ull;
        _if__result_2739 = _M0L6_2atmpS1744 == 0ull;
      } else {
        _if__result_2739 = 0;
      }
    } else {
      _if__result_2739 = 0;
    }
    if (_if__result_2739) {
      _M0Lm16lastRemovedDigitS630 = 4;
    }
    _M0L6_2atmpS1747 = _M0Lm2vrS605;
    _M0L6_2atmpS1753 = _M0Lm2vrS605;
    _M0L6_2atmpS1754 = _M0Lm2vmS607;
    if (_M0L6_2atmpS1753 == _M0L6_2atmpS1754) {
      if (!_M0L4evenS602) {
        _if__result_2740 = 1;
      } else {
        int32_t _M0L6_2atmpS1752 = _M0Lm17vmIsTrailingZerosS609;
        _if__result_2740 = !_M0L6_2atmpS1752;
      }
    } else {
      _if__result_2740 = 0;
    }
    if (_if__result_2740) {
      _M0L6_2atmpS1750 = 1;
    } else {
      int32_t _M0L6_2atmpS1751 = _M0Lm16lastRemovedDigitS630;
      _M0L6_2atmpS1750 = _M0L6_2atmpS1751 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1749 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1750);
    _M0L6_2atmpS1748 = *(uint64_t*)&_M0L6_2atmpS1749;
    _M0Lm6outputS631 = _M0L6_2atmpS1747 + _M0L6_2atmpS1748;
  } else {
    int32_t _M0Lm7roundUpS644 = 0;
    uint64_t _M0L6_2atmpS1775 = _M0Lm2vpS606;
    uint64_t _M0L8vpDiv100S645 = _M0L6_2atmpS1775 / 100ull;
    uint64_t _M0L6_2atmpS1774 = _M0Lm2vmS607;
    uint64_t _M0L8vmDiv100S646 = _M0L6_2atmpS1774 / 100ull;
    uint64_t _M0L6_2atmpS1769;
    uint64_t _M0L6_2atmpS1772;
    uint64_t _M0L6_2atmpS1773;
    int32_t _M0L6_2atmpS1771;
    uint64_t _M0L6_2atmpS1770;
    if (_M0L8vpDiv100S645 > _M0L8vmDiv100S646) {
      uint64_t _M0L6_2atmpS1760 = _M0Lm2vrS605;
      uint64_t _M0L8vrDiv100S647 = _M0L6_2atmpS1760 / 100ull;
      uint64_t _M0L6_2atmpS1759 = _M0Lm2vrS605;
      int32_t _M0L6_2atmpS1756 = (int32_t)_M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1758 = (int32_t)_M0L8vrDiv100S647;
      int32_t _M0L6_2atmpS1757 = 100 * _M0L6_2atmpS1758;
      int32_t _M0L8vrMod100S648 = _M0L6_2atmpS1756 - _M0L6_2atmpS1757;
      int32_t _M0L6_2atmpS1755;
      _M0Lm7roundUpS644 = _M0L8vrMod100S648 >= 50;
      _M0Lm2vrS605 = _M0L8vrDiv100S647;
      _M0Lm2vpS606 = _M0L8vpDiv100S645;
      _M0Lm2vmS607 = _M0L8vmDiv100S646;
      _M0L6_2atmpS1755 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1755 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1768 = _M0Lm2vpS606;
      uint64_t _M0L7vpDiv10S649 = _M0L6_2atmpS1768 / 10ull;
      uint64_t _M0L6_2atmpS1767 = _M0Lm2vmS607;
      uint64_t _M0L7vmDiv10S650 = _M0L6_2atmpS1767 / 10ull;
      uint64_t _M0L6_2atmpS1766;
      uint64_t _M0L7vrDiv10S652;
      uint64_t _M0L6_2atmpS1765;
      int32_t _M0L6_2atmpS1762;
      int32_t _M0L6_2atmpS1764;
      int32_t _M0L6_2atmpS1763;
      int32_t _M0L7vrMod10S653;
      int32_t _M0L6_2atmpS1761;
      if (_M0L7vpDiv10S649 <= _M0L7vmDiv10S650) {
        break;
      }
      _M0L6_2atmpS1766 = _M0Lm2vrS605;
      _M0L7vrDiv10S652 = _M0L6_2atmpS1766 / 10ull;
      _M0L6_2atmpS1765 = _M0Lm2vrS605;
      _M0L6_2atmpS1762 = (int32_t)_M0L6_2atmpS1765;
      _M0L6_2atmpS1764 = (int32_t)_M0L7vrDiv10S652;
      _M0L6_2atmpS1763 = 10 * _M0L6_2atmpS1764;
      _M0L7vrMod10S653 = _M0L6_2atmpS1762 - _M0L6_2atmpS1763;
      _M0Lm7roundUpS644 = _M0L7vrMod10S653 >= 5;
      _M0Lm2vrS605 = _M0L7vrDiv10S652;
      _M0Lm2vpS606 = _M0L7vpDiv10S649;
      _M0Lm2vmS607 = _M0L7vmDiv10S650;
      _M0L6_2atmpS1761 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1761 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1769 = _M0Lm2vrS605;
    _M0L6_2atmpS1772 = _M0Lm2vrS605;
    _M0L6_2atmpS1773 = _M0Lm2vmS607;
    _M0L6_2atmpS1771
    = _M0L6_2atmpS1772 == _M0L6_2atmpS1773 || _M0Lm7roundUpS644;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1770 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1771);
    _M0Lm6outputS631 = _M0L6_2atmpS1769 + _M0L6_2atmpS1770;
  }
  _M0L6_2atmpS1777 = _M0Lm3e10S608;
  _M0L6_2atmpS1778 = _M0Lm7removedS629;
  _M0L3expS654 = _M0L6_2atmpS1777 + _M0L6_2atmpS1778;
  _M0L6_2atmpS1776 = _M0Lm6outputS631;
  _block_2742
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2742)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2742->$0 = _M0L6_2atmpS1776;
  _block_2742->$1 = _M0L3expS654;
  return _block_2742;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS597) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS597) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS596) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS595) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS595) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS594) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS594 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS594 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS594 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS594 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS594 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS594 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS594 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS594 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS594 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS594 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS594 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS594 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS594 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS594 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS594 >= 100ull) {
    return 3;
  }
  if (_M0L1vS594 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS577) {
  int32_t _M0L6_2atmpS1677;
  int32_t _M0L6_2atmpS1676;
  int32_t _M0L4baseS576;
  int32_t _M0L5base2S578;
  int32_t _M0L6offsetS579;
  int32_t _M0L6_2atmpS1675;
  uint64_t _M0L4mul0S580;
  int32_t _M0L6_2atmpS1674;
  int32_t _M0L6_2atmpS1673;
  uint64_t _M0L4mul1S581;
  uint64_t _M0L1mS582;
  struct _M0TPB7Umul128 _M0L7_2abindS583;
  uint64_t _M0L7_2alow1S584;
  uint64_t _M0L8_2ahigh1S585;
  struct _M0TPB7Umul128 _M0L7_2abindS586;
  uint64_t _M0L7_2alow0S587;
  uint64_t _M0L8_2ahigh0S588;
  uint64_t _M0L3sumS589;
  uint64_t _M0Lm5high1S590;
  int32_t _M0L6_2atmpS1671;
  int32_t _M0L6_2atmpS1672;
  int32_t _M0L5deltaS591;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L6_2atmpS1662;
  int32_t _M0L6_2atmpS1669;
  uint32_t _M0L6_2atmpS1666;
  int32_t _M0L6_2atmpS1668;
  int32_t _M0L6_2atmpS1667;
  uint32_t _M0L6_2atmpS1665;
  uint32_t _M0L6_2atmpS1664;
  uint64_t _M0L6_2atmpS1663;
  uint64_t _M0L1aS592;
  uint64_t _M0L6_2atmpS1661;
  uint64_t _M0L1bS593;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1677 = _M0L1iS577 + 26;
  _M0L6_2atmpS1676 = _M0L6_2atmpS1677 - 1;
  _M0L4baseS576 = _M0L6_2atmpS1676 / 26;
  _M0L5base2S578 = _M0L4baseS576 * 26;
  _M0L6offsetS579 = _M0L5base2S578 - _M0L1iS577;
  _M0L6_2atmpS1675 = _M0L4baseS576 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S580
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1675);
  _M0L6_2atmpS1674 = _M0L4baseS576 * 2;
  _M0L6_2atmpS1673 = _M0L6_2atmpS1674 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1673);
  if (_M0L6offsetS579 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S580, .$1 = _M0L4mul1S581};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS579);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583 = _M0FPB7umul128(_M0L1mS582, _M0L4mul1S581);
  _M0L7_2alow1S584 = _M0L7_2abindS583.$0;
  _M0L8_2ahigh1S585 = _M0L7_2abindS583.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS586 = _M0FPB7umul128(_M0L1mS582, _M0L4mul0S580);
  _M0L7_2alow0S587 = _M0L7_2abindS586.$0;
  _M0L8_2ahigh0S588 = _M0L7_2abindS586.$1;
  _M0L3sumS589 = _M0L8_2ahigh0S588 + _M0L7_2alow1S584;
  _M0Lm5high1S590 = _M0L8_2ahigh1S585;
  if (_M0L3sumS589 < _M0L8_2ahigh0S588) {
    uint64_t _M0L6_2atmpS1660 = _M0Lm5high1S590;
    _M0Lm5high1S590 = _M0L6_2atmpS1660 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1671 = _M0FPB8pow5bits(_M0L5base2S578);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1672 = _M0FPB8pow5bits(_M0L1iS577);
  _M0L5deltaS591 = _M0L6_2atmpS1671 - _M0L6_2atmpS1672;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1670
  = _M0FPB13shiftright128(_M0L7_2alow0S587, _M0L3sumS589, _M0L5deltaS591);
  _M0L6_2atmpS1662 = _M0L6_2atmpS1670 + 1ull;
  _M0L6_2atmpS1669 = _M0L1iS577 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1666
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1669);
  _M0L6_2atmpS1668 = _M0L1iS577 % 16;
  _M0L6_2atmpS1667 = _M0L6_2atmpS1668 << 1;
  _M0L6_2atmpS1665 = _M0L6_2atmpS1666 >> (_M0L6_2atmpS1667 & 31);
  _M0L6_2atmpS1664 = _M0L6_2atmpS1665 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1663 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1664);
  _M0L1aS592 = _M0L6_2atmpS1662 + _M0L6_2atmpS1663;
  _M0L6_2atmpS1661 = _M0Lm5high1S590;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS593
  = _M0FPB13shiftright128(_M0L3sumS589, _M0L6_2atmpS1661, _M0L5deltaS591);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS592, .$1 = _M0L1bS593};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS559) {
  int32_t _M0L4baseS558;
  int32_t _M0L5base2S560;
  int32_t _M0L6offsetS561;
  int32_t _M0L6_2atmpS1659;
  uint64_t _M0L4mul0S562;
  int32_t _M0L6_2atmpS1658;
  int32_t _M0L6_2atmpS1657;
  uint64_t _M0L4mul1S563;
  uint64_t _M0L1mS564;
  struct _M0TPB7Umul128 _M0L7_2abindS565;
  uint64_t _M0L7_2alow1S566;
  uint64_t _M0L8_2ahigh1S567;
  struct _M0TPB7Umul128 _M0L7_2abindS568;
  uint64_t _M0L7_2alow0S569;
  uint64_t _M0L8_2ahigh0S570;
  uint64_t _M0L3sumS571;
  uint64_t _M0Lm5high1S572;
  int32_t _M0L6_2atmpS1655;
  int32_t _M0L6_2atmpS1656;
  int32_t _M0L5deltaS573;
  uint64_t _M0L6_2atmpS1647;
  int32_t _M0L6_2atmpS1654;
  uint32_t _M0L6_2atmpS1651;
  int32_t _M0L6_2atmpS1653;
  int32_t _M0L6_2atmpS1652;
  uint32_t _M0L6_2atmpS1650;
  uint32_t _M0L6_2atmpS1649;
  uint64_t _M0L6_2atmpS1648;
  uint64_t _M0L1aS574;
  uint64_t _M0L6_2atmpS1646;
  uint64_t _M0L1bS575;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS558 = _M0L1iS559 / 26;
  _M0L5base2S560 = _M0L4baseS558 * 26;
  _M0L6offsetS561 = _M0L1iS559 - _M0L5base2S560;
  _M0L6_2atmpS1659 = _M0L4baseS558 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S562
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1659);
  _M0L6_2atmpS1658 = _M0L4baseS558 * 2;
  _M0L6_2atmpS1657 = _M0L6_2atmpS1658 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1657);
  if (_M0L6offsetS561 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S562, .$1 = _M0L4mul1S563};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS561);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS565 = _M0FPB7umul128(_M0L1mS564, _M0L4mul1S563);
  _M0L7_2alow1S566 = _M0L7_2abindS565.$0;
  _M0L8_2ahigh1S567 = _M0L7_2abindS565.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS568 = _M0FPB7umul128(_M0L1mS564, _M0L4mul0S562);
  _M0L7_2alow0S569 = _M0L7_2abindS568.$0;
  _M0L8_2ahigh0S570 = _M0L7_2abindS568.$1;
  _M0L3sumS571 = _M0L8_2ahigh0S570 + _M0L7_2alow1S566;
  _M0Lm5high1S572 = _M0L8_2ahigh1S567;
  if (_M0L3sumS571 < _M0L8_2ahigh0S570) {
    uint64_t _M0L6_2atmpS1645 = _M0Lm5high1S572;
    _M0Lm5high1S572 = _M0L6_2atmpS1645 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1655 = _M0FPB8pow5bits(_M0L1iS559);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1656 = _M0FPB8pow5bits(_M0L5base2S560);
  _M0L5deltaS573 = _M0L6_2atmpS1655 - _M0L6_2atmpS1656;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1647
  = _M0FPB13shiftright128(_M0L7_2alow0S569, _M0L3sumS571, _M0L5deltaS573);
  _M0L6_2atmpS1654 = _M0L1iS559 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1651
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1654);
  _M0L6_2atmpS1653 = _M0L1iS559 % 16;
  _M0L6_2atmpS1652 = _M0L6_2atmpS1653 << 1;
  _M0L6_2atmpS1650 = _M0L6_2atmpS1651 >> (_M0L6_2atmpS1652 & 31);
  _M0L6_2atmpS1649 = _M0L6_2atmpS1650 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1648 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1649);
  _M0L1aS574 = _M0L6_2atmpS1647 + _M0L6_2atmpS1648;
  _M0L6_2atmpS1646 = _M0Lm5high1S572;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS575
  = _M0FPB13shiftright128(_M0L3sumS571, _M0L6_2atmpS1646, _M0L5deltaS573);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS574, .$1 = _M0L1bS575};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS532,
  struct _M0TPB8Pow5Pair _M0L3mulS529,
  int32_t _M0L1jS545,
  int32_t _M0L7mmShiftS547
) {
  uint64_t _M0L7_2amul0S528;
  uint64_t _M0L7_2amul1S530;
  uint64_t _M0L1mS531;
  struct _M0TPB7Umul128 _M0L7_2abindS533;
  uint64_t _M0L5_2aloS534;
  uint64_t _M0L6_2atmpS535;
  struct _M0TPB7Umul128 _M0L7_2abindS536;
  uint64_t _M0L6_2alo2S537;
  uint64_t _M0L6_2ahi2S538;
  uint64_t _M0L3midS539;
  uint64_t _M0L6_2atmpS1644;
  uint64_t _M0L2hiS540;
  uint64_t _M0L3lo2S541;
  uint64_t _M0L6_2atmpS1642;
  uint64_t _M0L6_2atmpS1643;
  uint64_t _M0L4mid2S542;
  uint64_t _M0L6_2atmpS1641;
  uint64_t _M0L3hi2S543;
  int32_t _M0L6_2atmpS1640;
  int32_t _M0L6_2atmpS1639;
  uint64_t _M0L2vpS544;
  uint64_t _M0Lm2vmS546;
  int32_t _M0L6_2atmpS1638;
  int32_t _M0L6_2atmpS1637;
  uint64_t _M0L2vrS557;
  uint64_t _M0L6_2atmpS1636;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S528 = _M0L3mulS529.$0;
  _M0L7_2amul1S530 = _M0L3mulS529.$1;
  _M0L1mS531 = _M0L1mS532 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS533 = _M0FPB7umul128(_M0L1mS531, _M0L7_2amul0S528);
  _M0L5_2aloS534 = _M0L7_2abindS533.$0;
  _M0L6_2atmpS535 = _M0L7_2abindS533.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS536 = _M0FPB7umul128(_M0L1mS531, _M0L7_2amul1S530);
  _M0L6_2alo2S537 = _M0L7_2abindS536.$0;
  _M0L6_2ahi2S538 = _M0L7_2abindS536.$1;
  _M0L3midS539 = _M0L6_2atmpS535 + _M0L6_2alo2S537;
  if (_M0L3midS539 < _M0L6_2atmpS535) {
    _M0L6_2atmpS1644 = 1ull;
  } else {
    _M0L6_2atmpS1644 = 0ull;
  }
  _M0L2hiS540 = _M0L6_2ahi2S538 + _M0L6_2atmpS1644;
  _M0L3lo2S541 = _M0L5_2aloS534 + _M0L7_2amul0S528;
  _M0L6_2atmpS1642 = _M0L3midS539 + _M0L7_2amul1S530;
  if (_M0L3lo2S541 < _M0L5_2aloS534) {
    _M0L6_2atmpS1643 = 1ull;
  } else {
    _M0L6_2atmpS1643 = 0ull;
  }
  _M0L4mid2S542 = _M0L6_2atmpS1642 + _M0L6_2atmpS1643;
  if (_M0L4mid2S542 < _M0L3midS539) {
    _M0L6_2atmpS1641 = 1ull;
  } else {
    _M0L6_2atmpS1641 = 0ull;
  }
  _M0L3hi2S543 = _M0L2hiS540 + _M0L6_2atmpS1641;
  _M0L6_2atmpS1640 = _M0L1jS545 - 64;
  _M0L6_2atmpS1639 = _M0L6_2atmpS1640 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS544
  = _M0FPB13shiftright128(_M0L4mid2S542, _M0L3hi2S543, _M0L6_2atmpS1639);
  _M0Lm2vmS546 = 0ull;
  if (_M0L7mmShiftS547) {
    uint64_t _M0L3lo3S548 = _M0L5_2aloS534 - _M0L7_2amul0S528;
    uint64_t _M0L6_2atmpS1626 = _M0L3midS539 - _M0L7_2amul1S530;
    uint64_t _M0L6_2atmpS1627;
    uint64_t _M0L4mid3S549;
    uint64_t _M0L6_2atmpS1625;
    uint64_t _M0L3hi3S550;
    int32_t _M0L6_2atmpS1624;
    int32_t _M0L6_2atmpS1623;
    if (_M0L5_2aloS534 < _M0L3lo3S548) {
      _M0L6_2atmpS1627 = 1ull;
    } else {
      _M0L6_2atmpS1627 = 0ull;
    }
    _M0L4mid3S549 = _M0L6_2atmpS1626 - _M0L6_2atmpS1627;
    if (_M0L3midS539 < _M0L4mid3S549) {
      _M0L6_2atmpS1625 = 1ull;
    } else {
      _M0L6_2atmpS1625 = 0ull;
    }
    _M0L3hi3S550 = _M0L2hiS540 - _M0L6_2atmpS1625;
    _M0L6_2atmpS1624 = _M0L1jS545 - 64;
    _M0L6_2atmpS1623 = _M0L6_2atmpS1624 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS546
    = _M0FPB13shiftright128(_M0L4mid3S549, _M0L3hi3S550, _M0L6_2atmpS1623);
  } else {
    uint64_t _M0L3lo3S551 = _M0L5_2aloS534 + _M0L5_2aloS534;
    uint64_t _M0L6_2atmpS1634 = _M0L3midS539 + _M0L3midS539;
    uint64_t _M0L6_2atmpS1635;
    uint64_t _M0L4mid3S552;
    uint64_t _M0L6_2atmpS1632;
    uint64_t _M0L6_2atmpS1633;
    uint64_t _M0L3hi3S553;
    uint64_t _M0L3lo4S554;
    uint64_t _M0L6_2atmpS1630;
    uint64_t _M0L6_2atmpS1631;
    uint64_t _M0L4mid4S555;
    uint64_t _M0L6_2atmpS1629;
    uint64_t _M0L3hi4S556;
    int32_t _M0L6_2atmpS1628;
    if (_M0L3lo3S551 < _M0L5_2aloS534) {
      _M0L6_2atmpS1635 = 1ull;
    } else {
      _M0L6_2atmpS1635 = 0ull;
    }
    _M0L4mid3S552 = _M0L6_2atmpS1634 + _M0L6_2atmpS1635;
    _M0L6_2atmpS1632 = _M0L2hiS540 + _M0L2hiS540;
    if (_M0L4mid3S552 < _M0L3midS539) {
      _M0L6_2atmpS1633 = 1ull;
    } else {
      _M0L6_2atmpS1633 = 0ull;
    }
    _M0L3hi3S553 = _M0L6_2atmpS1632 + _M0L6_2atmpS1633;
    _M0L3lo4S554 = _M0L3lo3S551 - _M0L7_2amul0S528;
    _M0L6_2atmpS1630 = _M0L4mid3S552 - _M0L7_2amul1S530;
    if (_M0L3lo3S551 < _M0L3lo4S554) {
      _M0L6_2atmpS1631 = 1ull;
    } else {
      _M0L6_2atmpS1631 = 0ull;
    }
    _M0L4mid4S555 = _M0L6_2atmpS1630 - _M0L6_2atmpS1631;
    if (_M0L4mid3S552 < _M0L4mid4S555) {
      _M0L6_2atmpS1629 = 1ull;
    } else {
      _M0L6_2atmpS1629 = 0ull;
    }
    _M0L3hi4S556 = _M0L3hi3S553 - _M0L6_2atmpS1629;
    _M0L6_2atmpS1628 = _M0L1jS545 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS546
    = _M0FPB13shiftright128(_M0L4mid4S555, _M0L3hi4S556, _M0L6_2atmpS1628);
  }
  _M0L6_2atmpS1638 = _M0L1jS545 - 64;
  _M0L6_2atmpS1637 = _M0L6_2atmpS1638 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS557
  = _M0FPB13shiftright128(_M0L3midS539, _M0L2hiS540, _M0L6_2atmpS1637);
  _M0L6_2atmpS1636 = _M0Lm2vmS546;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS557,
                                                .$1 = _M0L2vpS544,
                                                .$2 = _M0L6_2atmpS1636};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS526,
  int32_t _M0L1pS527
) {
  uint64_t _M0L6_2atmpS1622;
  uint64_t _M0L6_2atmpS1621;
  uint64_t _M0L6_2atmpS1620;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1622 = 1ull << (_M0L1pS527 & 63);
  _M0L6_2atmpS1621 = _M0L6_2atmpS1622 - 1ull;
  _M0L6_2atmpS1620 = _M0L5valueS526 & _M0L6_2atmpS1621;
  return _M0L6_2atmpS1620 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS524,
  int32_t _M0L1pS525
) {
  int32_t _M0L6_2atmpS1619;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1619 = _M0FPB10pow5Factor(_M0L5valueS524);
  return _M0L6_2atmpS1619 >= _M0L1pS525;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS519) {
  uint64_t _M0L6_2atmpS1610;
  uint64_t _M0L6_2atmpS1611;
  uint64_t _M0L6_2atmpS1612;
  uint64_t _M0L6_2atmpS1613;
  uint64_t _M0L6_2atmpS1618;
  int32_t _M0L5countS520;
  uint64_t _M0L1vS521;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1610 = _M0L5valueS519 % 5ull;
  if (_M0L6_2atmpS1610 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1611 = _M0L5valueS519 % 25ull;
  if (_M0L6_2atmpS1611 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1612 = _M0L5valueS519 % 125ull;
  if (_M0L6_2atmpS1612 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1613 = _M0L5valueS519 % 625ull;
  if (_M0L6_2atmpS1613 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1618 = _M0L5valueS519 / 625ull;
  _M0L5countS520 = 4;
  _M0L1vS521 = _M0L6_2atmpS1618;
  while (1) {
    if (_M0L1vS521 > 0ull) {
      uint64_t _M0L6_2atmpS1614 = _M0L1vS521 % 5ull;
      int32_t _M0L6_2atmpS1615;
      uint64_t _M0L6_2atmpS1616;
      if (_M0L6_2atmpS1614 != 0ull) {
        return _M0L5countS520;
      }
      _M0L6_2atmpS1615 = _M0L5countS520 + 1;
      _M0L6_2atmpS1616 = _M0L1vS521 / 5ull;
      _M0L5countS520 = _M0L6_2atmpS1615;
      _M0L1vS521 = _M0L6_2atmpS1616;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS523;
      moonbit_string_t _M0L6_2atmpS1617;
      int32_t _result_2744;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS523
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS523, (moonbit_string_t)moonbit_string_literal_16.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS523, _M0L5valueS519);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1617
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS523);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS523);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2744 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1617);
      moonbit_decref_cycle_free(_M0L6_2atmpS1617);
      return _result_2744;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS518,
  uint64_t _M0L2hiS516,
  int32_t _M0L4distS517
) {
  int32_t _M0L6_2atmpS1609;
  uint64_t _M0L6_2atmpS1607;
  uint64_t _M0L6_2atmpS1608;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1609 = 64 - _M0L4distS517;
  _M0L6_2atmpS1607 = _M0L2hiS516 << (_M0L6_2atmpS1609 & 63);
  _M0L6_2atmpS1608 = _M0L2loS518 >> (_M0L4distS517 & 63);
  return _M0L6_2atmpS1607 | _M0L6_2atmpS1608;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS506,
  uint64_t _M0L1bS509
) {
  uint64_t _M0L3aLoS505;
  uint64_t _M0L3aHiS507;
  uint64_t _M0L3bLoS508;
  uint64_t _M0L3bHiS510;
  uint64_t _M0L1xS511;
  uint64_t _M0L6_2atmpS1605;
  uint64_t _M0L6_2atmpS1606;
  uint64_t _M0L1yS512;
  uint64_t _M0L6_2atmpS1603;
  uint64_t _M0L6_2atmpS1604;
  uint64_t _M0L1zS513;
  uint64_t _M0L6_2atmpS1601;
  uint64_t _M0L6_2atmpS1602;
  uint64_t _M0L6_2atmpS1599;
  uint64_t _M0L6_2atmpS1600;
  uint64_t _M0L1wS514;
  uint64_t _M0L2loS515;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS505 = _M0L1aS506 & 4294967295ull;
  _M0L3aHiS507 = _M0L1aS506 >> 32;
  _M0L3bLoS508 = _M0L1bS509 & 4294967295ull;
  _M0L3bHiS510 = _M0L1bS509 >> 32;
  _M0L1xS511 = _M0L3aLoS505 * _M0L3bLoS508;
  _M0L6_2atmpS1605 = _M0L3aHiS507 * _M0L3bLoS508;
  _M0L6_2atmpS1606 = _M0L1xS511 >> 32;
  _M0L1yS512 = _M0L6_2atmpS1605 + _M0L6_2atmpS1606;
  _M0L6_2atmpS1603 = _M0L3aLoS505 * _M0L3bHiS510;
  _M0L6_2atmpS1604 = _M0L1yS512 & 4294967295ull;
  _M0L1zS513 = _M0L6_2atmpS1603 + _M0L6_2atmpS1604;
  _M0L6_2atmpS1601 = _M0L3aHiS507 * _M0L3bHiS510;
  _M0L6_2atmpS1602 = _M0L1yS512 >> 32;
  _M0L6_2atmpS1599 = _M0L6_2atmpS1601 + _M0L6_2atmpS1602;
  _M0L6_2atmpS1600 = _M0L1zS513 >> 32;
  _M0L1wS514 = _M0L6_2atmpS1599 + _M0L6_2atmpS1600;
  _M0L2loS515 = _M0L1aS506 * _M0L1bS509;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS515, .$1 = _M0L1wS514};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS503,
  int32_t _M0L4fromS500,
  int32_t _M0L2toS499
) {
  int32_t _M0L3lenS498;
  int32_t _M0L6_2atmpS1598;
  uint16_t* _M0L6bufferS501;
  int32_t _M0L1iS502;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS498 = _M0L2toS499 - _M0L4fromS500;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1598 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS501
  = (uint16_t*)moonbit_make_string(_M0L3lenS498, _M0L6_2atmpS1598);
  _M0L1iS502 = 0;
  while (1) {
    if (_M0L1iS502 < _M0L3lenS498) {
      int32_t _M0L6_2atmpS1596 = _M0L4fromS500 + _M0L1iS502;
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1594;
      int32_t _M0L6_2atmpS1597;
      if (
        _M0L6_2atmpS1596 < 0
        || _M0L6_2atmpS1596 >= Moonbit_array_length(_M0L5bytesS503)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1595 = (int32_t)_M0L5bytesS503[_M0L6_2atmpS1596];
      _M0L6_2atmpS1594 = (uint16_t)_M0L6_2atmpS1595;
      if (
        _M0L1iS502 < 0 || _M0L1iS502 >= Moonbit_array_length(_M0L6bufferS501)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS501[_M0L1iS502] = _M0L6_2atmpS1594;
      _M0L6_2atmpS1597 = _M0L1iS502 + 1;
      _M0L1iS502 = _M0L6_2atmpS1597;
      continue;
    }
    break;
  }
  return _M0L6bufferS501;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS497) {
  int32_t _M0L6_2atmpS1593;
  uint32_t _M0L6_2atmpS1592;
  uint32_t _M0L6_2atmpS1591;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1593 = _M0L1eS497 * 78913;
  _M0L6_2atmpS1592 = *(uint32_t*)&_M0L6_2atmpS1593;
  _M0L6_2atmpS1591 = _M0L6_2atmpS1592 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1591;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS496) {
  int32_t _M0L6_2atmpS1590;
  uint32_t _M0L6_2atmpS1589;
  uint32_t _M0L6_2atmpS1588;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1590 = _M0L1eS496 * 732923;
  _M0L6_2atmpS1589 = *(uint32_t*)&_M0L6_2atmpS1590;
  _M0L6_2atmpS1588 = _M0L6_2atmpS1589 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1588;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS494,
  int32_t _M0L8exponentS495,
  int32_t _M0L8mantissaS492
) {
  moonbit_string_t _M0L1sS493;
  moonbit_string_t _result_2747;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS492) {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  }
  if (_M0L4signS494) {
    _M0L1sS493 = (moonbit_string_t)moonbit_string_literal_18.data;
  } else {
    _M0L1sS493 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS495) {
    moonbit_string_t _result_2746;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2746
    = moonbit_add_string(_M0L1sS493, (moonbit_string_t)moonbit_string_literal_19.data);
    moonbit_decref_cycle_free(_M0L1sS493);
    return _result_2746;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2747
  = moonbit_add_string(_M0L1sS493, (moonbit_string_t)moonbit_string_literal_20.data);
  moonbit_decref_cycle_free(_M0L1sS493);
  return _result_2747;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS491) {
  int32_t _M0L6_2atmpS1587;
  uint32_t _M0L6_2atmpS1586;
  uint32_t _M0L6_2atmpS1585;
  int32_t _M0L6_2atmpS1584;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1587 = _M0L1eS491 * 1217359;
  _M0L6_2atmpS1586 = *(uint32_t*)&_M0L6_2atmpS1587;
  _M0L6_2atmpS1585 = _M0L6_2atmpS1586 >> 19;
  _M0L6_2atmpS1584 = *(int32_t*)&_M0L6_2atmpS1585;
  return _M0L6_2atmpS1584 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS490) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS490 != _M0L4selfS490) {
    return 0;
  } else if (_M0L4selfS490 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS490 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS490;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS489) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS489 != _M0L4selfS489) {
    return 0ll;
  } else if (_M0L4selfS489 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS489 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS489;
  }
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS486
) {
  int32_t* _M0L6_2atmpS1581;
  struct _M0TPB5ArrayGiE* _block_2748;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1581 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS486);
  _block_2748
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2748)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _block_2748->$0 = _M0L6_2atmpS1581;
  _block_2748->$1 = _M0L3lenS486;
  return _block_2748;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS487
) {
  float* _M0L6_2atmpS1582;
  struct _M0TPB5ArrayGfE* _block_2749;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1582 = (float*)moonbit_make_float_array_raw(_M0L3lenS487);
  _block_2749
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2749)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 93, 0);
  _block_2749->$0 = _M0L6_2atmpS1582;
  _block_2749->$1 = _M0L3lenS487;
  return _block_2749;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS488
) {
  uint8_t* _M0L6_2atmpS1583;
  struct _M0TPB5ArrayGbE* _block_2750;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1583 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS488);
  _block_2750
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2750)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _block_2750->$0 = _M0L6_2atmpS1583;
  _block_2750->$1 = _M0L3lenS488;
  return _block_2750;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS482,
  int32_t _M0L5indexS483
) {
  uint64_t* _M0L6_2atmpS1579;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1579 = _M0L4selfS482;
  if (
    _M0L5indexS483 < 0
    || _M0L5indexS483 >= Moonbit_array_length(_M0L6_2atmpS1579)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1579[_M0L5indexS483];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS484,
  int32_t _M0L5indexS485
) {
  uint32_t* _M0L6_2atmpS1580;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1580 = _M0L4selfS484;
  if (
    _M0L5indexS485 < 0
    || _M0L5indexS485 >= Moonbit_array_length(_M0L6_2atmpS1580)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1580[_M0L5indexS485];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS481
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS481, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS480) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS480, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS479) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS479;
}

int32_t _M0MPC15array5Array4pushGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS470,
  int32_t _M0L5valueS472
) {
  int32_t _M0L3lenS1558;
  uint8_t* _M0L6_2atmpS1560;
  int32_t _M0L6_2atmpS1559;
  int32_t _M0L6lengthS471;
  uint8_t* _M0L3bufS1563;
  int32_t _M0L6_2atmpS1564;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1558 = _M0L4selfS470->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1560 = _M0MPC15array5Array6bufferGbE(_M0L4selfS470);
  _M0L6_2atmpS1559 = Moonbit_array_length(_M0L6_2atmpS1560);
  moonbit_decref_cycle_free(_M0L6_2atmpS1560);
  if (_M0L3lenS1558 == _M0L6_2atmpS1559) {
    int32_t _M0L3lenS1562 = _M0L4selfS470->$1;
    int32_t _M0L6_2atmpS1561 = _M0L3lenS1562 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGbE(_M0L4selfS470, _M0L6_2atmpS1561);
  }
  _M0L6lengthS471 = _M0L4selfS470->$1;
  _M0L3bufS1563 = _M0L4selfS470->$0;
  _M0L3bufS1563[_M0L6lengthS471] = _M0L5valueS472;
  _M0L6_2atmpS1564 = _M0L6lengthS471 + 1;
  _M0L4selfS470->$1 = _M0L6_2atmpS1564;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS473,
  moonbit_string_t _M0L5valueS475
) {
  int32_t _M0L3lenS1565;
  moonbit_string_t* _M0L6_2atmpS1567;
  int32_t _M0L6_2atmpS1566;
  int32_t _M0L6lengthS474;
  moonbit_string_t* _M0L3bufS1570;
  moonbit_string_t _M0L6_2aoldS2644;
  int32_t _M0L6_2atmpS1571;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1565 = _M0L4selfS473->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1567 = _M0MPC15array5Array6bufferGsE(_M0L4selfS473);
  _M0L6_2atmpS1566 = Moonbit_array_length(_M0L6_2atmpS1567);
  moonbit_decref_cycle_free(_M0L6_2atmpS1567);
  if (_M0L3lenS1565 == _M0L6_2atmpS1566) {
    int32_t _M0L3lenS1569 = _M0L4selfS473->$1;
    int32_t _M0L6_2atmpS1568 = _M0L3lenS1569 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS473, _M0L6_2atmpS1568);
  }
  _M0L6lengthS474 = _M0L4selfS473->$1;
  _M0L3bufS1570 = _M0L4selfS473->$0;
  _M0L6_2aoldS2644 = (moonbit_string_t)_M0L3bufS1570[_M0L6lengthS474];
  moonbit_decref_cycle_free(_M0L6_2aoldS2644);
  _M0L3bufS1570[_M0L6lengthS474] = _M0L5valueS475;
  _M0L6_2atmpS1571 = _M0L6lengthS474 + 1;
  _M0L4selfS473->$1 = _M0L6_2atmpS1571;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS476,
  struct _M0TUsiE* _M0L5valueS478
) {
  int32_t _M0L3lenS1572;
  struct _M0TUsiE** _M0L6_2atmpS1574;
  int32_t _M0L6_2atmpS1573;
  int32_t _M0L6lengthS477;
  struct _M0TUsiE** _M0L3bufS1577;
  struct _M0TUsiE* _M0L6_2aoldS2645;
  int32_t _M0L6_2atmpS1578;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1572 = _M0L4selfS476->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1574 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS476);
  _M0L6_2atmpS1573 = Moonbit_array_length(_M0L6_2atmpS1574);
  moonbit_decref_cycle_free(_M0L6_2atmpS1574);
  if (_M0L3lenS1572 == _M0L6_2atmpS1573) {
    int32_t _M0L3lenS1576 = _M0L4selfS476->$1;
    int32_t _M0L6_2atmpS1575 = _M0L3lenS1576 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS476, _M0L6_2atmpS1575);
  }
  _M0L6lengthS477 = _M0L4selfS476->$1;
  _M0L3bufS1577 = _M0L4selfS476->$0;
  _M0L6_2aoldS2645 = (struct _M0TUsiE*)_M0L3bufS1577[_M0L6lengthS477];
  if (_M0L6_2aoldS2645) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2645);
  }
  _M0L3bufS1577[_M0L6lengthS477] = _M0L5valueS478;
  _M0L6_2atmpS1578 = _M0L6lengthS477 + 1;
  _M0L4selfS476->$1 = _M0L6_2atmpS1578;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS459,
  int32_t _M0L8requiredS461
) {
  int32_t _M0L8old__capS458;
  int32_t _M0L3lenS1555;
  int32_t _M0L8new__capS460;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS458 = _M0MPC15array5Array8capacityGbE(_M0L4selfS459);
  _M0L3lenS1555 = _M0L4selfS459->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS460
  = _M0FPB23array__growth__capacity(_M0L8old__capS458, _M0L3lenS1555, _M0L8requiredS461);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGbE(_M0L4selfS459, _M0L8new__capS460);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS463,
  int32_t _M0L8requiredS465
) {
  int32_t _M0L8old__capS462;
  int32_t _M0L3lenS1556;
  int32_t _M0L8new__capS464;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS462 = _M0MPC15array5Array8capacityGsE(_M0L4selfS463);
  _M0L3lenS1556 = _M0L4selfS463->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS464
  = _M0FPB23array__growth__capacity(_M0L8old__capS462, _M0L3lenS1556, _M0L8requiredS465);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS463, _M0L8new__capS464);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS467,
  int32_t _M0L8requiredS469
) {
  int32_t _M0L8old__capS466;
  int32_t _M0L3lenS1557;
  int32_t _M0L8new__capS468;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS466 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS467);
  _M0L3lenS1557 = _M0L4selfS467->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS468
  = _M0FPB23array__growth__capacity(_M0L8old__capS466, _M0L3lenS1557, _M0L8requiredS469);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS467, _M0L8new__capS468);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS441,
  int32_t _M0L13new__capacityS444
) {
  uint8_t* _M0L8old__bufS440;
  int32_t _M0L3lenS442;
  int32_t _M0L9copy__lenS443;
  uint8_t* _M0L8new__bufS445;
  uint8_t* _M0L6_2aoldS2646;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS440 = _M0L4selfS441->$0;
  _M0L3lenS442 = _M0L4selfS441->$1;
  if (_M0L3lenS442 < _M0L13new__capacityS444) {
    _M0L9copy__lenS443 = _M0L3lenS442;
  } else {
    _M0L9copy__lenS443 = _M0L13new__capacityS444;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS440);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS445
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(_M0L8old__bufS440, _M0L13new__capacityS444, _M0L9copy__lenS443, 0, 0);
  _M0L6_2aoldS2646 = _M0L4selfS441->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2646);
  _M0L4selfS441->$0 = _M0L8new__bufS445;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS447,
  int32_t _M0L13new__capacityS450
) {
  moonbit_string_t* _M0L8old__bufS446;
  int32_t _M0L3lenS448;
  int32_t _M0L9copy__lenS449;
  moonbit_string_t* _M0L8new__bufS451;
  moonbit_string_t* _M0L6_2aoldS2647;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS446 = _M0L4selfS447->$0;
  _M0L3lenS448 = _M0L4selfS447->$1;
  if (_M0L3lenS448 < _M0L13new__capacityS450) {
    _M0L9copy__lenS449 = _M0L3lenS448;
  } else {
    _M0L9copy__lenS449 = _M0L13new__capacityS450;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS446);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS451
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS446, _M0L13new__capacityS450, _M0L9copy__lenS449, 0, 0);
  _M0L6_2aoldS2647 = _M0L4selfS447->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2647);
  _M0L4selfS447->$0 = _M0L8new__bufS451;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS453,
  int32_t _M0L13new__capacityS456
) {
  struct _M0TUsiE** _M0L8old__bufS452;
  int32_t _M0L3lenS454;
  int32_t _M0L9copy__lenS455;
  struct _M0TUsiE** _M0L8new__bufS457;
  struct _M0TUsiE** _M0L6_2aoldS2648;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS452 = _M0L4selfS453->$0;
  _M0L3lenS454 = _M0L4selfS453->$1;
  if (_M0L3lenS454 < _M0L13new__capacityS456) {
    _M0L9copy__lenS455 = _M0L3lenS454;
  } else {
    _M0L9copy__lenS455 = _M0L13new__capacityS456;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS452);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS457
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS452, _M0L13new__capacityS456, _M0L9copy__lenS455, 0, 0);
  _M0L6_2aoldS2648 = _M0L4selfS453->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2648);
  _M0L4selfS453->$0 = _M0L8new__bufS457;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS437
) {
  uint8_t* _M0L6_2atmpS1552;
  int32_t _result_2751;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1552 = _M0MPC15array5Array6bufferGbE(_M0L4selfS437);
  _result_2751 = Moonbit_array_length(_M0L6_2atmpS1552);
  moonbit_decref_cycle_free(_M0L6_2atmpS1552);
  return _result_2751;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS438
) {
  moonbit_string_t* _M0L6_2atmpS1553;
  int32_t _result_2752;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1553 = _M0MPC15array5Array6bufferGsE(_M0L4selfS438);
  _result_2752 = Moonbit_array_length(_M0L6_2atmpS1553);
  moonbit_decref_cycle_free(_M0L6_2atmpS1553);
  return _result_2752;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS439
) {
  struct _M0TUsiE** _M0L6_2atmpS1554;
  int32_t _result_2753;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1554 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS439);
  _result_2753 = Moonbit_array_length(_M0L6_2atmpS1554);
  moonbit_decref_cycle_free(_M0L6_2atmpS1554);
  return _result_2753;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS433,
  int32_t _M0L3lenS431,
  int32_t _M0L8requiredS430
) {
  int32_t _M0L5startS432;
  int32_t _M0L5spaceS434;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS430 < _M0L3lenS431) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  if (_M0L7currentS433 == 0) {
    _M0L5startS432 = 8;
  } else {
    _M0L5startS432 = _M0L7currentS433;
  }
  _M0L5spaceS434 = _M0L5startS432;
  while (1) {
    if (_M0L5spaceS434 < _M0L8requiredS430) {
      int32_t _M0L4nextS435 = _M0L5spaceS434 * 2;
      if (_M0L4nextS435 <= _M0L5spaceS434) {
        return _M0L8requiredS430;
      }
      _M0L5spaceS434 = _M0L4nextS435;
      continue;
    } else {
      return _M0L5spaceS434;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS429) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS429->$1;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS424) {
  uint8_t* _M0L8_2afieldS2649;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2649 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2649);
  return _M0L8_2afieldS2649;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS425) {
  int32_t* _M0L8_2afieldS2650;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2650 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2650);
  return _M0L8_2afieldS2650;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS426) {
  float* _M0L8_2afieldS2651;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2651 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2651);
  return _M0L8_2afieldS2651;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L8_2afieldS2652;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2652 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2652);
  return _M0L8_2afieldS2652;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L8_2afieldS2653;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2653 = _M0L4selfS428->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2653);
  return _M0L8_2afieldS2653;
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
  int32_t _M0L3endS1550;
  int32_t _M0L5startS1551;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1549;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1542;
  int32_t _M0L6_2atmpS1541;
  int32_t _if__result_2755;
  uint16_t* _M0L4dataS1543;
  int32_t _M0L3lenS1544;
  moonbit_string_t _M0L6_2atmpS1545;
  int32_t _M0L6_2atmpS1546;
  int32_t _M0L3lenS1548;
  int32_t _M0L6_2atmpS1547;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1550 = _M0L3strS420.$2;
  _M0L5startS1551 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1550 - _M0L5startS1551;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1549 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1549 + _M0L8str__lenS419;
  _M0L4dataS1542 = _M0L4selfS422->$0;
  _M0L6_2atmpS1541 = Moonbit_array_length(_M0L4dataS1542);
  if (_M0L8requiredS421 > _M0L6_2atmpS1541) {
    _if__result_2755 = 1;
  } else {
    int32_t _M0L3lenS1540 = _M0L4selfS422->$1;
    _if__result_2755 = _M0L8requiredS421 < _M0L3lenS1540;
  }
  if (_if__result_2755) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1543 = _M0L4selfS422->$0;
  _M0L3lenS1544 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1543);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1545 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1546 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1543, _M0L3lenS1544, _M0L6_2atmpS1545, _M0L6_2atmpS1546, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1543);
  moonbit_decref_cycle_free(_M0L6_2atmpS1545);
  _M0L3lenS1548 = _M0L4selfS422->$1;
  _M0L6_2atmpS1547 = _M0L3lenS1548 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1547;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_2756;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1539;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1538;
  moonbit_string_t _result_2757;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1537 = Moonbit_array_length(_M0L3strS416);
    _if__result_2756 = _M0L3endS415 == _M0L6_2atmpS1537;
  } else {
    _if__result_2756 = 0;
  }
  if (_if__result_2756) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1539 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1539, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1538 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2757
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1538, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1538);
  return _result_2757;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_2758;
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
      int32_t _M0L6_2atmpS1536 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_2758 = _M0L6_2atmpS1536 <= _M0L3lenS408;
    } else {
      _if__result_2758 = 0;
    }
  } else {
    _if__result_2758 = 0;
  }
  if (_if__result_2758) {
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
  int32_t _M0L6_2atmpS1535;
  int32_t _M0L6_2atmpS1534;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1533;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1535 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1534 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1535;
  _M0L2e1S394 = _M0L6_2atmpS1534 - 1;
  _M0L6_2atmpS1533 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1533 - 1;
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
        int32_t _M0L6_2atmpS1530 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1529 = (int32_t)_M0L6_2atmpS1530;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1529;
        uint32_t _M0L6_2atmpS1525 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1524;
        int32_t _M0L6_2atmpS1526;
        uint32_t _M0L6_2atmpS1528;
        int32_t _M0L6_2atmpS1527;
        int32_t _M0L6_2atmpS1531;
        int32_t _M0L6_2atmpS1532;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1524 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1525);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1524;
        _M0L6_2atmpS1526 = _M0L1jS405 + 1;
        _M0L6_2atmpS1528 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1527 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1528);
        if (
          _M0L6_2atmpS1526 < 0
          || _M0L6_2atmpS1526 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1526] = _M0L6_2atmpS1527;
        _M0L6_2atmpS1531 = _M0L1iS404 + 1;
        _M0L6_2atmpS1532 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1531;
        _M0L1jS405 = _M0L6_2atmpS1532;
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
  int32_t _M0L6_2atmpS1523;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1523 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1523 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS385 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1522 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1522;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1519;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1519 = 1;
      } else {
        _M0L6_2atmpS1519 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1519;
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
      int32_t _M0L6_2atmpS1520;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1520 = 1;
      } else {
        _M0L6_2atmpS1520 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1520;
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
      int32_t _M0L6_2atmpS1521;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1521 = 1;
      } else {
        _M0L6_2atmpS1521 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1521;
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
  int32_t _M0L6_2atmpS1518;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1518 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1518;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1495 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1495;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1494 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1493 = 48 + _M0L6_2atmpS1494;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1493;
      int32_t _M0L6_2atmpS1492 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1491 = 48 + _M0L6_2atmpS1492;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1491;
      int32_t _M0L6_2atmpS1490 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1489 = 48 + _M0L6_2atmpS1490;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1489;
      int32_t _M0L6_2atmpS1488 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1487 = 48 + _M0L6_2atmpS1488;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1487;
      int32_t _M0L6_2atmpS1479 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1478 = _M0L6_2atmpS1479 - 4;
      int32_t _M0L6_2atmpS1481;
      int32_t _M0L6_2atmpS1480;
      int32_t _M0L6_2atmpS1483;
      int32_t _M0L6_2atmpS1482;
      int32_t _M0L6_2atmpS1485;
      int32_t _M0L6_2atmpS1484;
      int32_t _M0L6_2atmpS1486;
      _M0L6bufferS353[_M0L6_2atmpS1478] = _M0L6d1__hiS349;
      _M0L6_2atmpS1481 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1480 = _M0L6_2atmpS1481 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1480] = _M0L6d1__loS350;
      _M0L6_2atmpS1483 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1482 = _M0L6_2atmpS1483 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1482] = _M0L6d2__hiS351;
      _M0L6_2atmpS1485 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1484 = _M0L6_2atmpS1485 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1484] = _M0L6d2__loS352;
      _M0L6_2atmpS1486 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1486;
      continue;
    } else {
      int32_t _M0L6_2atmpS1517 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1517;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1504 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1503 = 48 + _M0L6_2atmpS1504;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1503;
          int32_t _M0L6_2atmpS1502 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1501 = 48 + _M0L6_2atmpS1502;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1501;
          int32_t _M0L6_2atmpS1497 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1496 = _M0L6_2atmpS1497 - 2;
          int32_t _M0L6_2atmpS1499;
          int32_t _M0L6_2atmpS1498;
          int32_t _M0L6_2atmpS1500;
          _M0L6bufferS353[_M0L6_2atmpS1496] = _M0L5d__hiS360;
          _M0L6_2atmpS1499 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1498 = _M0L6_2atmpS1499 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1498] = _M0L5d__loS361;
          _M0L6_2atmpS1500 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1500;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1512 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1511 = 48 + _M0L6_2atmpS1512;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1511;
          int32_t _M0L6_2atmpS1510 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1509 = 48 + _M0L6_2atmpS1510;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1509;
          int32_t _M0L6_2atmpS1506 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1505 = _M0L6_2atmpS1506 - 2;
          int32_t _M0L6_2atmpS1508;
          int32_t _M0L6_2atmpS1507;
          _M0L6bufferS353[_M0L6_2atmpS1505] = _M0L5d__hiS363;
          _M0L6_2atmpS1508 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1507 = _M0L6_2atmpS1508 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1507] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1516 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1513 = _M0L6_2atmpS1516 - 1;
          int32_t _M0L6_2atmpS1515 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1514 = (uint16_t)_M0L6_2atmpS1515;
          _M0L6bufferS353[_M0L6_2atmpS1513] = _M0L6_2atmpS1514;
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
  int32_t _M0L6_2atmpS1463;
  int32_t _M0L6_2atmpS1462;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1463 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1462 = _M0L5radixS327 & _M0L6_2atmpS1463;
  if (_M0L6_2atmpS1462 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1470;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1470 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1470;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1469 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1469;
        int32_t _M0L6_2atmpS1466 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1464 = _M0L6_2atmpS1466 - 1;
        int32_t _M0L6_2atmpS1465 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1467;
        uint64_t _M0L6_2atmpS1468;
        _M0L6bufferS333[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
        _M0L6_2atmpS1467 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1468 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1467;
        _M0L1nS331 = _M0L6_2atmpS1468;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1477 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1477;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1476 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1475 = _M0L1nS339 - _M0L6_2atmpS1476;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1475;
        int32_t _M0L6_2atmpS1473 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1471 = _M0L6_2atmpS1473 - 1;
        int32_t _M0L6_2atmpS1472 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1474;
        _M0L6bufferS333[_M0L6_2atmpS1471] = _M0L6_2atmpS1472;
        _M0L6_2atmpS1474 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1474;
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
  int32_t _M0L6_2atmpS1461;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1461 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1461;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1458 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1458;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1452 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1450 = _M0L6_2atmpS1452 - 2;
      int32_t _M0L6_2atmpS1451 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1455;
      int32_t _M0L6_2atmpS1453;
      int32_t _M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1456;
      uint64_t _M0L6_2atmpS1457;
      _M0L6bufferS320[_M0L6_2atmpS1450] = _M0L6_2atmpS1451;
      _M0L6_2atmpS1455 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1453 = _M0L6_2atmpS1455 - 1;
      _M0L6_2atmpS1454
      = ((moonbit_string_t)moonbit_string_literal_23.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1453] = _M0L6_2atmpS1454;
      _M0L6_2atmpS1456 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1457 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1456;
      _M0L1nS316 = _M0L6_2atmpS1457;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1460 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1460;
      int32_t _M0L6_2atmpS1459 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1459;
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
      uint64_t _M0L6_2atmpS1448 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1449 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1448;
      _M0L5countS313 = _M0L6_2atmpS1449;
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
    int32_t _M0L6_2atmpS1447;
    int32_t _M0L6_2atmpS1446;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1447 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1446 = _M0L6_2atmpS1447 / 4;
    return _M0L6_2atmpS1446 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1445 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1445;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1442;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1442 = 1;
      } else {
        _M0L6_2atmpS1442 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1442;
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
      int32_t _M0L6_2atmpS1443;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1443 = 1;
      } else {
        _M0L6_2atmpS1443 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1443;
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
      int32_t _M0L6_2atmpS1444;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1444 = 1;
      } else {
        _M0L6_2atmpS1444 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1444;
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
      uint32_t _M0L6_2atmpS1440 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1441 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1440;
      _M0L5countS287 = _M0L6_2atmpS1441;
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
    int32_t _M0L6_2atmpS1439;
    int32_t _M0L6_2atmpS1438;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1439 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1438 = _M0L6_2atmpS1439 / 4;
    return _M0L6_2atmpS1438 + 1;
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
  int32_t _M0L6_2atmpS1437;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1437 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1437;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1414 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1414;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1413 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1412 = 48 + _M0L6_2atmpS1413;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1412;
      int32_t _M0L6_2atmpS1411 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1410 = 48 + _M0L6_2atmpS1411;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1410;
      int32_t _M0L6_2atmpS1409 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1408 = 48 + _M0L6_2atmpS1409;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1408;
      int32_t _M0L6_2atmpS1407 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1406 = 48 + _M0L6_2atmpS1407;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1406;
      int32_t _M0L6_2atmpS1398 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1397 = _M0L6_2atmpS1398 - 4;
      int32_t _M0L6_2atmpS1400;
      int32_t _M0L6_2atmpS1399;
      int32_t _M0L6_2atmpS1402;
      int32_t _M0L6_2atmpS1401;
      int32_t _M0L6_2atmpS1404;
      int32_t _M0L6_2atmpS1403;
      int32_t _M0L6_2atmpS1405;
      _M0L6bufferS266[_M0L6_2atmpS1397] = _M0L6d1__hiS262;
      _M0L6_2atmpS1400 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1399 = _M0L6_2atmpS1400 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1399] = _M0L6d1__loS263;
      _M0L6_2atmpS1402 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1401 = _M0L6_2atmpS1402 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1401] = _M0L6d2__hiS264;
      _M0L6_2atmpS1404 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1403 = _M0L6_2atmpS1404 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1403] = _M0L6d2__loS265;
      _M0L6_2atmpS1405 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1405;
      continue;
    } else {
      int32_t _M0L6_2atmpS1436 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1436;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1423 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1422 = 48 + _M0L6_2atmpS1423;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1422;
          int32_t _M0L6_2atmpS1421 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1420 = 48 + _M0L6_2atmpS1421;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1420;
          int32_t _M0L6_2atmpS1416 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1415 = _M0L6_2atmpS1416 - 2;
          int32_t _M0L6_2atmpS1418;
          int32_t _M0L6_2atmpS1417;
          int32_t _M0L6_2atmpS1419;
          _M0L6bufferS266[_M0L6_2atmpS1415] = _M0L5d__hiS273;
          _M0L6_2atmpS1418 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1417 = _M0L6_2atmpS1418 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1417] = _M0L5d__loS274;
          _M0L6_2atmpS1419 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1419;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1431 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1430 = 48 + _M0L6_2atmpS1431;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1430;
          int32_t _M0L6_2atmpS1429 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1428 = 48 + _M0L6_2atmpS1429;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1428;
          int32_t _M0L6_2atmpS1425 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1424 = _M0L6_2atmpS1425 - 2;
          int32_t _M0L6_2atmpS1427;
          int32_t _M0L6_2atmpS1426;
          _M0L6bufferS266[_M0L6_2atmpS1424] = _M0L5d__hiS276;
          _M0L6_2atmpS1427 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1426 = _M0L6_2atmpS1427 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1426] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1435 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1432 = _M0L6_2atmpS1435 - 1;
          int32_t _M0L6_2atmpS1434 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1433 = (uint16_t)_M0L6_2atmpS1434;
          _M0L6bufferS266[_M0L6_2atmpS1432] = _M0L6_2atmpS1433;
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
  int32_t _M0L6_2atmpS1382;
  int32_t _M0L6_2atmpS1381;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1382 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1381 = _M0L5radixS240 & _M0L6_2atmpS1382;
  if (_M0L6_2atmpS1381 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1389;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1389 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1389;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1388 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1388;
        int32_t _M0L6_2atmpS1385 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1383 = _M0L6_2atmpS1385 - 1;
        int32_t _M0L6_2atmpS1384 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1386;
        uint32_t _M0L6_2atmpS1387;
        _M0L6bufferS246[_M0L6_2atmpS1383] = _M0L6_2atmpS1384;
        _M0L6_2atmpS1386 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1387 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1386;
        _M0L1nS244 = _M0L6_2atmpS1387;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1396 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1396;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1395 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1394 = _M0L1nS252 - _M0L6_2atmpS1395;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1394;
        int32_t _M0L6_2atmpS1392 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1390 = _M0L6_2atmpS1392 - 1;
        int32_t _M0L6_2atmpS1391 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1393;
        _M0L6bufferS246[_M0L6_2atmpS1390] = _M0L6_2atmpS1391;
        _M0L6_2atmpS1393 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1393;
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
  int32_t _M0L6_2atmpS1380;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1380 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1380;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1377 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1377;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1371 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1369 = _M0L6_2atmpS1371 - 2;
      int32_t _M0L6_2atmpS1370 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1374;
      int32_t _M0L6_2atmpS1372;
      int32_t _M0L6_2atmpS1373;
      int32_t _M0L6_2atmpS1375;
      uint32_t _M0L6_2atmpS1376;
      _M0L6bufferS233[_M0L6_2atmpS1369] = _M0L6_2atmpS1370;
      _M0L6_2atmpS1374 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1372 = _M0L6_2atmpS1374 - 1;
      _M0L6_2atmpS1373
      = ((moonbit_string_t)moonbit_string_literal_23.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1372] = _M0L6_2atmpS1373;
      _M0L6_2atmpS1375 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1376 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1375;
      _M0L1nS229 = _M0L6_2atmpS1376;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1379 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1379;
      int32_t _M0L6_2atmpS1378 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1378;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1368;
  moonbit_string_t _result_2772;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1368
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1368);
  if (_M0L6_2atmpS1368.$1) {
    moonbit_decref(_M0L6_2atmpS1368.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2772 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_2772;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1365;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1365 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1365);
  moonbit_decref_cycle_free(_M0L6_2atmpS1365);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1366;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1366 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1366);
  moonbit_decref_cycle_free(_M0L6_2atmpS1366);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1367;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1367 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1367);
  moonbit_decref_cycle_free(_M0L6_2atmpS1367);
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
  moonbit_string_t _M0L8_2afieldS2654;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2654 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2654);
  return _M0L8_2afieldS2654;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1364;
  int64_t _M0L6_2atmpS1363;
  struct _M0TPC16string10StringView _M0L6_2atmpS1362;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1364 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1363 = (int64_t)_M0L6_2atmpS1364;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1362
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1363);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1362);
  moonbit_decref_cycle_free(_M0L6_2atmpS1362.$0);
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
  int32_t _M0L6_2atmpS1346;
  int32_t _if__result_2773;
  int32_t _M0L6_2atmpS1354;
  int32_t _if__result_2774;
  int32_t _M0L6_2atmpS1356;
  int32_t _M0L6_2atmpS1357;
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
  _M0L6_2atmpS1346 = _M0Lm2loS208;
  if (_M0L6_2atmpS1346 > 0) {
    int32_t _M0L6_2atmpS1345 = _M0Lm2loS208;
    if (_M0L6_2atmpS1345 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1344 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1343 = _M0L4selfS207[_M0L6_2atmpS1344];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1343)) {
        int32_t _M0L6_2atmpS1342 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1341 = _M0L6_2atmpS1342 - 1;
        int32_t _M0L6_2atmpS1340 = _M0L4selfS207[_M0L6_2atmpS1341];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2773
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1340);
      } else {
        _if__result_2773 = 0;
      }
    } else {
      _if__result_2773 = 0;
    }
  } else {
    _if__result_2773 = 0;
  }
  if (_if__result_2773) {
    int32_t _M0L6_2atmpS1347 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1347 + 1;
  }
  _M0L6_2atmpS1354 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1354 > 0) {
    int32_t _M0L6_2atmpS1353 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1353 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1352 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1351 = _M0L4selfS207[_M0L6_2atmpS1352];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1351)) {
        int32_t _M0L6_2atmpS1350 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1349 = _M0L6_2atmpS1350 - 1;
        int32_t _M0L6_2atmpS1348 = _M0L4selfS207[_M0L6_2atmpS1349];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2774
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1348);
      } else {
        _if__result_2774 = 0;
      }
    } else {
      _if__result_2774 = 0;
    }
  } else {
    _if__result_2774 = 0;
  }
  if (_if__result_2774) {
    int32_t _M0L6_2atmpS1355 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1355 - 1;
  }
  _M0L6_2atmpS1356 = _M0Lm2loS208;
  _M0L6_2atmpS1357 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1356 >= _M0L6_2atmpS1357) {
    int32_t _M0L6_2atmpS1358 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1359 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1358,
                                                 .$2 = _M0L6_2atmpS1359};
  } else {
    int32_t _M0L6_2atmpS1360 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1361 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1360,
                                                 .$2 = _M0L6_2atmpS1361};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1339;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1339
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1339);
  if (_M0L6_2atmpS1339.$1) {
    moonbit_decref(_M0L6_2atmpS1339.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1338;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1338
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1338);
  if (_M0L6_2atmpS1338.$1) {
    moonbit_decref(_M0L6_2atmpS1338.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1337;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1337 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1337;
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
  int32_t _M0L6_2atmpS1336;
  struct _M0TPC16string10StringView _M0L6_2atmpS1334;
  struct _M0TPB6Logger _M0L6_2atmpS1335;
  moonbit_string_t _result_2775;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1336 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1334
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1336
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1335
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1334, _M0L6_2atmpS1335, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1334.$0);
  if (_M0L6_2atmpS1335.$1) {
    moonbit_decref(_M0L6_2atmpS1335.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2775 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_2775;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1332;
  int32_t _M0L5startS1333;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1332 = _M0L4selfS190.$2;
  _M0L5startS1333 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1332 - _M0L5startS1333;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 103, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1329;
    int32_t _M0L5startS1331;
    int32_t _M0L6_2atmpS1330;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1313;
    int32_t _M0L6_2atmpS1314;
    int32_t _M0L6_2atmpS1315;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1329 = _M0L4selfS190.$0;
    _M0L5startS1331 = _M0L4selfS190.$1;
    _M0L6_2atmpS1330 = _M0L5startS1331 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1329[_M0L6_2atmpS1330];
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
        int32_t _M0L6_2atmpS1316;
        int32_t _M0L6_2atmpS1317;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1316 = _M0L1iS192 + 1;
        _M0L6_2atmpS1317 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1316;
        _M0L3segS193 = _M0L6_2atmpS1317;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1318;
        int32_t _M0L6_2atmpS1319;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1318 = _M0L1iS192 + 1;
        _M0L6_2atmpS1319 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1318;
        _M0L3segS193 = _M0L6_2atmpS1319;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1320;
        int32_t _M0L6_2atmpS1321;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1320 = _M0L1iS192 + 1;
        _M0L6_2atmpS1321 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1320;
        _M0L3segS193 = _M0L6_2atmpS1321;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1322;
        int32_t _M0L6_2atmpS1323;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1322 = _M0L1iS192 + 1;
        _M0L6_2atmpS1323 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1322;
        _M0L3segS193 = _M0L6_2atmpS1323;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1325;
          moonbit_string_t _M0L6_2atmpS1324;
          int32_t _M0L6_2atmpS1326;
          int32_t _M0L6_2atmpS1327;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_28.data);
          _M0L6_2atmpS1325 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1324 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1325);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1324);
          moonbit_decref_cycle_free(_M0L6_2atmpS1324);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1326 = _M0L1iS192 + 1;
          _M0L6_2atmpS1327 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1326;
          _M0L3segS193 = _M0L6_2atmpS1327;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1328 = _M0L1iS192 + 1;
          int32_t _tmp_2778 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1328;
          _M0L3segS193 = _tmp_2778;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_2777;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1313 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1313);
    _M0L6_2atmpS1314 = _M0L1iS192 + 1;
    _M0L6_2atmpS1315 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1314;
    _M0L3segS193 = _M0L6_2atmpS1315;
    continue;
    joinlet_2777:;
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
    int64_t _M0L6_2atmpS1312 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1311;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1311
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1312);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1311);
    moonbit_decref_cycle_free(_M0L6_2atmpS1311.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1309;
  int32_t _M0L5startS1310;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1287;
  int32_t _if__result_2779;
  int32_t _M0L6_2atmpS1297;
  int32_t _if__result_2780;
  int32_t _M0L6_2atmpS1299;
  int32_t _M0L6_2atmpS1300;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1309 = _M0L4selfS173.$2;
  _M0L5startS1310 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1309 - _M0L5startS1310;
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
  _M0L6_2atmpS1287 = _M0Lm2loS174;
  if (_M0L6_2atmpS1287 > 0) {
    int32_t _M0L6_2atmpS1286 = _M0Lm2loS174;
    if (_M0L6_2atmpS1286 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1285 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1284 = _M0L4baseS181 + _M0L6_2atmpS1285;
      int32_t _M0L6_2atmpS1283 = _M0L3strS180[_M0L6_2atmpS1284];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1283)) {
        int32_t _M0L6_2atmpS1282 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1281 = _M0L4baseS181 + _M0L6_2atmpS1282;
        int32_t _M0L6_2atmpS1280 = _M0L6_2atmpS1281 - 1;
        int32_t _M0L6_2atmpS1279 = _M0L3strS180[_M0L6_2atmpS1280];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2779
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1279);
      } else {
        _if__result_2779 = 0;
      }
    } else {
      _if__result_2779 = 0;
    }
  } else {
    _if__result_2779 = 0;
  }
  if (_if__result_2779) {
    int32_t _M0L6_2atmpS1288 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1288 + 1;
  }
  _M0L6_2atmpS1297 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1297 > 0) {
    int32_t _M0L6_2atmpS1296 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1296 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1295 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1294 = _M0L4baseS181 + _M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1293 = _M0L3strS180[_M0L6_2atmpS1294];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1293)) {
        int32_t _M0L6_2atmpS1292 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1291 = _M0L4baseS181 + _M0L6_2atmpS1292;
        int32_t _M0L6_2atmpS1290 = _M0L6_2atmpS1291 - 1;
        int32_t _M0L6_2atmpS1289 = _M0L3strS180[_M0L6_2atmpS1290];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2780
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1289);
      } else {
        _if__result_2780 = 0;
      }
    } else {
      _if__result_2780 = 0;
    }
  } else {
    _if__result_2780 = 0;
  }
  if (_if__result_2780) {
    int32_t _M0L6_2atmpS1298 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1298 - 1;
  }
  _M0L6_2atmpS1299 = _M0Lm2loS174;
  _M0L6_2atmpS1300 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1299 >= _M0L6_2atmpS1300) {
    int32_t _M0L6_2atmpS1304 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1301 = _M0L4baseS181 + _M0L6_2atmpS1304;
    int32_t _M0L6_2atmpS1303 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1302 = _M0L4baseS181 + _M0L6_2atmpS1303;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1301,
                                                 .$2 = _M0L6_2atmpS1302};
  } else {
    int32_t _M0L6_2atmpS1308 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1305 = _M0L4baseS181 + _M0L6_2atmpS1308;
    int32_t _M0L6_2atmpS1307 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1306 = _M0L4baseS181 + _M0L6_2atmpS1307;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1305,
                                                 .$2 = _M0L6_2atmpS1306};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1276;
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L6_2atmpS1278;
  int32_t _M0L6_2atmpS1277;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1274;
  moonbit_string_t _result_2781;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1276 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1275
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1276);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1275);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1278 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1277
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1278);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1277);
  _M0L6_2atmpS1274 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2781 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1274);
  moonbit_decref_cycle_free(_M0L6_2atmpS1274);
  return _result_2781;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1271;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1271 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1271);
  } else {
    int32_t _M0L6_2atmpS1273;
    int32_t _M0L6_2atmpS1272;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1273 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1272 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1273, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1272);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1269;
  int32_t _M0L6_2atmpS1270;
  int32_t _M0L6_2atmpS1268;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1269 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1270 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1268 = _M0L6_2atmpS1269 - _M0L6_2atmpS1270;
  return _M0L6_2atmpS1268 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1266;
  int32_t _M0L6_2atmpS1267;
  int32_t _M0L6_2atmpS1265;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1266 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1267 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1265 = _M0L6_2atmpS1266 % _M0L6_2atmpS1267;
  return _M0L6_2atmpS1265 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1263;
  int32_t _M0L6_2atmpS1264;
  int32_t _M0L6_2atmpS1262;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1263 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1264 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1262 = _M0L6_2atmpS1263 / _M0L6_2atmpS1264;
  return _M0L6_2atmpS1262 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1260;
  int32_t _M0L6_2atmpS1261;
  int32_t _M0L6_2atmpS1259;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1260 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1261 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1259 = _M0L6_2atmpS1260 + _M0L6_2atmpS1261;
  return _M0L6_2atmpS1259 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1258;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1258 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1258;
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
  int32_t _M0L3lenS1257;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1252;
  int32_t _M0L6_2atmpS1251;
  int32_t _if__result_2782;
  uint16_t* _M0L4dataS1253;
  int32_t _M0L3lenS1254;
  int32_t _M0L3lenS1256;
  int32_t _M0L6_2atmpS1255;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1257 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1257 + _M0L8str__lenS154;
  _M0L4dataS1252 = _M0L4selfS157->$0;
  _M0L6_2atmpS1251 = Moonbit_array_length(_M0L4dataS1252);
  if (_M0L8requiredS156 > _M0L6_2atmpS1251) {
    _if__result_2782 = 1;
  } else {
    int32_t _M0L3lenS1250 = _M0L4selfS157->$1;
    _if__result_2782 = _M0L8requiredS156 < _M0L3lenS1250;
  }
  if (_if__result_2782) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1253 = _M0L4selfS157->$0;
  _M0L3lenS1254 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1253);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1253, _M0L3lenS1254, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1253);
  _M0L3lenS1256 = _M0L4selfS157->$1;
  _M0L6_2atmpS1255 = _M0L3lenS1256 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1255;
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
      int32_t _M0L6_2atmpS1247 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L6_2atmpS1249;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1247;
      _M0L6_2atmpS1248 = _M0L1iS148 + 1;
      _M0L6_2atmpS1249 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1248;
      _M0L1jS149 = _M0L6_2atmpS1249;
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
    int32_t _M0L3lenS1218 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1220 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1219 = Moonbit_array_length(_M0L4dataS1220);
    uint16_t* _M0L4dataS1223;
    int32_t _M0L3lenS1224;
    int32_t _M0L6_2atmpS1225;
    int32_t _M0L3lenS1227;
    int32_t _M0L6_2atmpS1226;
    if (_M0L3lenS1218 >= _M0L6_2atmpS1219) {
      int32_t _M0L3lenS1222 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1221 = _M0L3lenS1222 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1221);
    }
    _M0L4dataS1223 = _M0L4selfS143->$0;
    _M0L3lenS1224 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1223);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1225 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1224 < 0
      || _M0L3lenS1224 >= Moonbit_array_length(_M0L4dataS1223)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1223[_M0L3lenS1224] = _M0L6_2atmpS1225;
    moonbit_decref_cycle_free(_M0L4dataS1223);
    _M0L3lenS1227 = _M0L4selfS143->$1;
    _M0L6_2atmpS1226 = _M0L3lenS1227 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1226;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1231 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1229 = Moonbit_array_length(_M0L4dataS1231);
    int32_t _M0L3lenS1230 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1228 = _M0L6_2atmpS1229 - _M0L3lenS1230;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1234;
    int32_t _M0L3lenS1235;
    uint32_t _M0L6_2atmpS1238;
    uint32_t _M0L6_2atmpS1237;
    int32_t _M0L6_2atmpS1236;
    uint16_t* _M0L4dataS1239;
    int32_t _M0L3lenS1244;
    int32_t _M0L6_2atmpS1240;
    uint32_t _M0L6_2atmpS1243;
    uint32_t _M0L6_2atmpS1242;
    int32_t _M0L6_2atmpS1241;
    int32_t _M0L3lenS1246;
    int32_t _M0L6_2atmpS1245;
    if (_M0L6_2atmpS1228 < 2) {
      int32_t _M0L3lenS1233 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1232 = _M0L3lenS1233 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1232);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1234 = _M0L4selfS143->$0;
    _M0L3lenS1235 = _M0L4selfS143->$1;
    _M0L6_2atmpS1238 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1237 = 55296u + _M0L6_2atmpS1238;
    moonbit_incref_cycle_free(_M0L4dataS1234);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1236 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1237);
    if (
      _M0L3lenS1235 < 0
      || _M0L3lenS1235 >= Moonbit_array_length(_M0L4dataS1234)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1234[_M0L3lenS1235] = _M0L6_2atmpS1236;
    moonbit_decref_cycle_free(_M0L4dataS1234);
    _M0L4dataS1239 = _M0L4selfS143->$0;
    _M0L3lenS1244 = _M0L4selfS143->$1;
    _M0L6_2atmpS1240 = _M0L3lenS1244 + 1;
    _M0L6_2atmpS1243 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1242 = 56320u + _M0L6_2atmpS1243;
    moonbit_incref_cycle_free(_M0L4dataS1239);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1241 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1242);
    if (
      _M0L6_2atmpS1240 < 0
      || _M0L6_2atmpS1240 >= Moonbit_array_length(_M0L4dataS1239)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1239[_M0L6_2atmpS1240] = _M0L6_2atmpS1241;
    moonbit_decref_cycle_free(_M0L4dataS1239);
    _M0L3lenS1246 = _M0L4selfS143->$1;
    _M0L6_2atmpS1245 = _M0L3lenS1246 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1245;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1217;
  int32_t _M0L6_2atmpS1215;
  int32_t _M0L3lenS1216;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1212;
  int32_t _M0L6_2atmpS1213;
  int32_t _M0L3lenS1214;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS2655;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1217 = _M0L4selfS138->$0;
  _M0L6_2atmpS1215 = Moonbit_array_length(_M0L4dataS1217);
  _M0L3lenS1216 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1215, _M0L3lenS1216, _M0L8requiredS139);
  _M0L4dataS1212 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1212);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1213 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1214 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1212, _M0L13new__capacityS137, _M0L6_2atmpS1213, _M0L3lenS1214, 0, 0);
  _M0L6_2aoldS2655 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2655);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
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
  int32_t _M0L6_2atmpS1211;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1211 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1211;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1210;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1210 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1210;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1201;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1201 = _M0L4selfS127->$1;
  if (_M0L3lenS1201 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1202 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1204 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1203 = Moonbit_array_length(_M0L4dataS1204);
    if (_M0L3lenS1202 == _M0L6_2atmpS1203) {
      uint16_t* _M0L4dataS1205 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1205);
      return _M0L4dataS1205;
    } else {
      uint16_t* _M0L4dataS1206 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1207 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1208;
      int32_t _M0L3lenS1209;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1206);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1208 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1209 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1206, _M0L3lenS1207, _M0L6_2atmpS1208, _M0L3lenS1209, 0, 0);
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
  int32_t _if__result_2785;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1197 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1198 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1197 <= _M0L6_2atmpS1198) {
            int32_t _M0L6_2atmpS1196 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2785 = _M0L6_2atmpS1196 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2785 = 0;
          }
        } else {
          _if__result_2785 = 0;
        }
      } else {
        _if__result_2785 = 0;
      }
    } else {
      _if__result_2785 = 0;
    }
  } else {
    _if__result_2785 = 0;
  }
  if (_if__result_2785) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1200;
    moonbit_string_t _M0L6_2atmpS1199;
    uint16_t* _result_2786;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_35.data);
    _M0L6_2atmpS1200 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1200);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1199
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2786 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1199);
    moonbit_decref_cycle_free(_M0L6_2atmpS1199);
    return _result_2786;
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
  struct _M0TPB13StringBuilder* _block_2787;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS1195 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS1195 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_2787
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2787)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 108, 0);
  _block_2787->$0 = _M0L4dataS112;
  _block_2787->$1 = 0;
  return _block_2787;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS1194;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1194 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS1194;
}

uint8_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(
  uint8_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_2788;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS1180 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS1181;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1181
          = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
          if (_M0L6_2atmpS1180 <= _M0L6_2atmpS1181) {
            int32_t _M0L6_2atmpS1179 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_2788 = _M0L6_2atmpS1179 <= _M0L13allocate__lenS91;
          } else {
            _if__result_2788 = 0;
          }
        } else {
          _if__result_2788 = 0;
        }
      } else {
        _if__result_2788 = 0;
      }
    } else {
      _if__result_2788 = 0;
    }
  } else {
    _if__result_2788 = 0;
  }
  if (_if__result_2788) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGbE(_M0L3srcS95, _M0L13allocate__lenS91, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS1183;
    moonbit_string_t _M0L6_2atmpS1182;
    uint8_t* _result_2789;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1183 = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS1183);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1182
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2789
    = _M0FPC15abort5abortGRPB18UninitializedArrayGbEE(_M0L6_2atmpS1182);
    moonbit_decref_cycle_free(_M0L6_2atmpS1182);
    return _result_2789;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_2790;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS1185 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS1186;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1186
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
          if (_M0L6_2atmpS1185 <= _M0L6_2atmpS1186) {
            int32_t _M0L6_2atmpS1184 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_2790 = _M0L6_2atmpS1184 <= _M0L13allocate__lenS97;
          } else {
            _if__result_2790 = 0;
          }
        } else {
          _if__result_2790 = 0;
        }
      } else {
        _if__result_2790 = 0;
      }
    } else {
      _if__result_2790 = 0;
    }
  } else {
    _if__result_2790 = 0;
  }
  if (_if__result_2790) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS1188;
    moonbit_string_t _M0L6_2atmpS1187;
    moonbit_string_t* _result_2791;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1188 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS1188);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1187
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2791
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1187);
    moonbit_decref_cycle_free(_M0L6_2atmpS1187);
    return _result_2791;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_2792;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS1190 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS1191;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1191
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
          if (_M0L6_2atmpS1190 <= _M0L6_2atmpS1191) {
            int32_t _M0L6_2atmpS1189 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_2792 = _M0L6_2atmpS1189 <= _M0L13allocate__lenS103;
          } else {
            _if__result_2792 = 0;
          }
        } else {
          _if__result_2792 = 0;
        }
      } else {
        _if__result_2792 = 0;
      }
    } else {
      _if__result_2792 = 0;
    }
  } else {
    _if__result_2792 = 0;
  }
  if (_if__result_2792) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS103, 0, _M0L3srcS107, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS1193;
    moonbit_string_t _M0L6_2atmpS1192;
    struct _M0TUsiE** _result_2793;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1193 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS1193);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1192
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2793
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1192);
    moonbit_decref_cycle_free(_M0L6_2atmpS1192);
    return _result_2793;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS1176;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS1176
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS1176);
  if (_M0L6_2atmpS1176.$1) {
    moonbit_decref(_M0L6_2atmpS1176.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS1177;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS1177
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS1177);
  if (_M0L6_2atmpS1177.$1) {
    moonbit_decref(_M0L6_2atmpS1177.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS1178;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS1178
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS1178);
  if (_M0L6_2atmpS1178.$1) {
    moonbit_decref(_M0L6_2atmpS1178.$1);
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
        int32_t _M0L6_2atmpS1140 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS1142 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS1141;
        int32_t _M0L6_2atmpS1143;
        if (
          _M0L6_2atmpS1142 < 0
          || _M0L6_2atmpS1142 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1141 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1142];
        if (
          _M0L6_2atmpS1140 < 0
          || _M0L6_2atmpS1140 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1140] = _M0L6_2atmpS1141;
        _M0L6_2atmpS1143 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS1143;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1148 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS1148;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS1144 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS1146 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS1145;
        int32_t _M0L6_2atmpS1147;
        if (
          _M0L6_2atmpS1146 < 0
          || _M0L6_2atmpS1146 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1145 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1146];
        if (
          _M0L6_2atmpS1144 < 0
          || _M0L6_2atmpS1144 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1144] = _M0L6_2atmpS1145;
        _M0L6_2atmpS1147 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS1147;
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
        int32_t _M0L6_2atmpS1149 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS1151 = _M0L11src__offsetS28 + _M0L1iS29;
        int32_t _M0L6_2atmpS1150;
        int32_t _M0L6_2atmpS1152;
        if (
          _M0L6_2atmpS1151 < 0
          || _M0L6_2atmpS1151 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1150 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1151];
        if (
          _M0L6_2atmpS1149 < 0
          || _M0L6_2atmpS1149 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1149] = _M0L6_2atmpS1150;
        _M0L6_2atmpS1152 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS1152;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1157 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS1157;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS1153 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS1155 = _M0L11src__offsetS28 + _M0L1iS32;
        int32_t _M0L6_2atmpS1154;
        int32_t _M0L6_2atmpS1156;
        if (
          _M0L6_2atmpS1155 < 0
          || _M0L6_2atmpS1155 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1154 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1155];
        if (
          _M0L6_2atmpS1153 < 0
          || _M0L6_2atmpS1153 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1153] = _M0L6_2atmpS1154;
        _M0L6_2atmpS1156 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS1156;
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
        int32_t _M0L6_2atmpS1158 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS1160 = _M0L11src__offsetS37 + _M0L1iS38;
        moonbit_string_t _M0L6_2atmpS1159;
        moonbit_string_t _M0L6_2aoldS2656;
        int32_t _M0L6_2atmpS1161;
        if (
          _M0L6_2atmpS1160 < 0
          || _M0L6_2atmpS1160 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1159 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1160];
        if (
          _M0L6_2atmpS1158 < 0
          || _M0L6_2atmpS1158 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2656 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1158];
        moonbit_incref_cycle_free(_M0L6_2atmpS1159);
        moonbit_decref_cycle_free(_M0L6_2aoldS2656);
        _M0L3dstS34[_M0L6_2atmpS1158] = _M0L6_2atmpS1159;
        _M0L6_2atmpS1161 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS1161;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1166 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS1166;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS1162 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS1164 = _M0L11src__offsetS37 + _M0L1iS41;
        moonbit_string_t _M0L6_2atmpS1163;
        moonbit_string_t _M0L6_2aoldS2657;
        int32_t _M0L6_2atmpS1165;
        if (
          _M0L6_2atmpS1164 < 0
          || _M0L6_2atmpS1164 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1163 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1164];
        if (
          _M0L6_2atmpS1162 < 0
          || _M0L6_2atmpS1162 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2657 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1162];
        moonbit_incref_cycle_free(_M0L6_2atmpS1163);
        moonbit_decref_cycle_free(_M0L6_2aoldS2657);
        _M0L3dstS34[_M0L6_2atmpS1162] = _M0L6_2atmpS1163;
        _M0L6_2atmpS1165 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS1165;
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
        int32_t _M0L6_2atmpS1167 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS1169 = _M0L11src__offsetS46 + _M0L1iS47;
        struct _M0TUsiE* _M0L6_2atmpS1168;
        struct _M0TUsiE* _M0L6_2aoldS2658;
        int32_t _M0L6_2atmpS1170;
        if (
          _M0L6_2atmpS1169 < 0
          || _M0L6_2atmpS1169 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1168 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1169];
        if (
          _M0L6_2atmpS1167 < 0
          || _M0L6_2atmpS1167 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2658 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1167];
        if (_M0L6_2atmpS1168) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1168);
        }
        if (_M0L6_2aoldS2658) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2658);
        }
        _M0L3dstS43[_M0L6_2atmpS1167] = _M0L6_2atmpS1168;
        _M0L6_2atmpS1170 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS1170;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1175 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS1175;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS1171 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS1173 = _M0L11src__offsetS46 + _M0L1iS50;
        struct _M0TUsiE* _M0L6_2atmpS1172;
        struct _M0TUsiE* _M0L6_2aoldS2659;
        int32_t _M0L6_2atmpS1174;
        if (
          _M0L6_2atmpS1173 < 0
          || _M0L6_2atmpS1173 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1172 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1173];
        if (
          _M0L6_2atmpS1171 < 0
          || _M0L6_2atmpS1171 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2659 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1171];
        if (_M0L6_2atmpS1172) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1172);
        }
        if (_M0L6_2aoldS2659) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2659);
        }
        _M0L3dstS43[_M0L6_2atmpS1171] = _M0L6_2atmpS1172;
        _M0L6_2atmpS1174 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS1174;
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_36.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_37.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1111) {
  switch (Moonbit_object_tag(_M0L4_2aeS1111)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1111);
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_41.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1135,
  struct _M0TPB4Show _M0L8_2aparamS1134
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1133 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1135;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1133, _M0L8_2aparamS1134);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1132,
  struct _M0TPB4Show _M0L8_2aparamS1131
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1130 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1132;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1130, _M0L8_2aparamS1131);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1129,
  int32_t _M0L8_2aparamS1128
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1127 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1129;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1127, _M0L8_2aparamS1128);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1126,
  struct _M0TPC16string10StringView _M0L8_2aparamS1125
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1124 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1126;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1124, _M0L8_2aparamS1125);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1123,
  moonbit_string_t _M0L8_2aparamS1120,
  int32_t _M0L8_2aparamS1121,
  int32_t _M0L8_2aparamS1122
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1119 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1123;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1119, _M0L8_2aparamS1120, _M0L8_2aparamS1121, _M0L8_2aparamS1122);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1118,
  moonbit_string_t _M0L8_2aparamS1117
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1116 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1118;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1116, _M0L8_2aparamS1117);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1139;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1104;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1105;
  int32_t _M0L7_2abindS1106;
  struct _M0TUsiE** _M0L7_2abindS1107;
  int32_t _M0L6_2acntS2664;
  int32_t _M0L2__S1108;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1139
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1104
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1104)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 111, 0);
  _M0L12async__testsS1104->$0 = _M0L6_2atmpS1139;
  _M0L12async__testsS1104->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1105
  = _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1106 = _M0L7_2abindS1105->$1;
  _M0L7_2abindS1107 = _M0L7_2abindS1105->$0;
  _M0L6_2acntS2664
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1105));
  if (_M0L6_2acntS2664 > 1) {
    int32_t _M0L11_2anew__cntS2665 = _M0L6_2acntS2664 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1105), _M0L11_2anew__cntS2665);
    moonbit_incref_cycle_free(_M0L7_2abindS1107);
  } else if (_M0L6_2acntS2664 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1105);
  }
  _M0L2__S1108 = 0;
  while (1) {
    if (_M0L2__S1108 < _M0L7_2abindS1106) {
      struct _M0TUsiE* _M0L3argS1109 =
        (struct _M0TUsiE*)_M0L7_2abindS1107[_M0L2__S1108];
      moonbit_string_t _M0L6_2atmpS1136 = _M0L3argS1109->$0;
      int32_t _M0L6_2atmpS1137 = _M0L3argS1109->$1;
      int32_t _M0L6_2atmpS1138;
      moonbit_incref_cycle_free(_M0L6_2atmpS1136);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples31tripod__current__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1104, _M0L6_2atmpS1136, _M0L6_2atmpS1137);
      moonbit_decref_cycle_free(_M0L6_2atmpS1136);
      _M0L6_2atmpS1138 = _M0L2__S1108 + 1;
      _M0L2__S1108 = _M0L6_2atmpS1138;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1107);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_current\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__current__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__current__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1104);
  moonbit_decref_cycle_free(_M0L12async__testsS1104);
  moonbit_flush_cycles();
  return 0;
}