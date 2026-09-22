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

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TUdiE;

struct _M0TP26RiantR8snn__mbt9IstdpRate;

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt8Dendrite;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt6Tripod;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TP26RiantR8snn__mbt14IstdpPotential;

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry;

struct _M0TUddE;

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406;

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

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
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

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
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

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  struct _M0TP26RiantR8snn__mbt6Tripod* $1;
  moonbit_string_t $2;
  moonbit_string_t $3;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGiE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
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

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* $3;
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
  void* $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1413(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1406(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1401(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1378(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1371(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  moonbit_string_t,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry3new(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential*
);

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential*
);

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0MP26RiantR8snn__mbt14IstdpPotential3new(
  
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

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry3new(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt9IstdpRate*
);

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt9IstdpRate*
);

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0MP26RiantR8snn__mbt9IstdpRate3new(
  
);

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IFParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
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

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t,
  int32_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t,
  int32_t,
  float,
  float,
  float,
  int32_t,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

double _M0FP26RiantR8snn__mbt9next__f64(
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

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

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

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
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

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

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

double cos(double);

moonbit_string_t* moonbit_rt_get_cli_args();

double sin(double);

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
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    110, 101, 116, 119, 111, 114, 107, 95, 98, 108, 97, 99, 107, 98, 
    111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    110, 101, 116, 119, 111, 114, 107, 95, 98, 108, 97, 99, 107, 98, 
    111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 
    112, 84, 101, 115, 116, 0
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
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

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
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 50, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 49, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 115, 111, 
    109, 97, 0
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
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
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
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1413$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1413
  };

uint32_t const moonbit_layout_table_data[143] =
  {
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401, $1)
    / 4
    * 2,
    sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406)
    / 4, 1,
    offsetof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod) / 4, 
    10,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $6)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $7)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $8)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $9)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry) / 4, 
    3,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables) / 4, 
    2,
    offsetof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables, $1)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables, $1) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRPB5ArrayGfEE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2hz = 0x1.0624dd2f1a9fcp-10f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS3417
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1434,
  moonbit_string_t _M0L8filenameS1403,
  int32_t _M0L5indexS1405
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401* _closure_3461;
  struct _M0TWEu* _M0L13handle__startS1401;
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406* _closure_3462;
  struct _M0TWssbEu* _M0L14handle__resultS1406;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1413;
  void* _M0L11_2atry__errS1428;
  struct moonbit_result_0 _tmp_3464;
  int32_t _handle__error__result_3465;
  int32_t _M0L6_2atmpS3405;
  void* _M0L3errS1429;
  moonbit_string_t _M0L4nameS1431;
  struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1432;
  moonbit_string_t _M0L7_2anameS1433;
  int32_t _M0L6_2acntS3455;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1403);
  _closure_3461
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401));
  Moonbit_object_header(_closure_3461)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_3461->code
  = &_M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1401;
  _closure_3461->$0 = _M0L5indexS1405;
  _closure_3461->$1 = _M0L8filenameS1403;
  _M0L13handle__startS1401 = (struct _M0TWEu*)_closure_3461;
  moonbit_incref_cycle_free(_M0L8filenameS1403);
  _closure_3462
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406*)moonbit_malloc(sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406));
  Moonbit_object_header(_closure_3462)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_3462->code
  = &_M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1406;
  _closure_3462->$0 = _M0L5indexS1405;
  _closure_3462->$1 = _M0L8filenameS1403;
  _M0L14handle__resultS1406 = (struct _M0TWssbEu*)_closure_3462;
  _M0L17error__to__stringS1413
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1413$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _tmp_3464
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1434, _M0L8filenameS1403, _M0L5indexS1405, _M0L13handle__startS1401, _M0L14handle__resultS1406, _M0L17error__to__stringS1413);
  if (_tmp_3464.tag) {
    int32_t const _M0L5_2aokS3414 = _tmp_3464.data.ok;
    _handle__error__result_3465 = _M0L5_2aokS3414;
  } else {
    void* const _M0L6_2aerrS3415 = _tmp_3464.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
    moonbit_decref_cycle_free(_M0L13handle__startS1401);
    _M0L11_2atry__errS1428 = _M0L6_2aerrS3415;
    goto join_1427;
  }
  if (_handle__error__result_3465) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
    moonbit_decref_cycle_free(_M0L13handle__startS1401);
    _M0L6_2atmpS3405 = 1;
  } else {
    struct moonbit_result_0 _tmp_3466;
    int32_t _handle__error__result_3467;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    _tmp_3466
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1434, _M0L8filenameS1403, _M0L5indexS1405, _M0L13handle__startS1401, _M0L14handle__resultS1406, _M0L17error__to__stringS1413);
    if (_tmp_3466.tag) {
      int32_t const _M0L5_2aokS3412 = _tmp_3466.data.ok;
      _handle__error__result_3467 = _M0L5_2aokS3412;
    } else {
      void* const _M0L6_2aerrS3413 = _tmp_3466.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
      moonbit_decref_cycle_free(_M0L13handle__startS1401);
      _M0L11_2atry__errS1428 = _M0L6_2aerrS3413;
      goto join_1427;
    }
    if (_handle__error__result_3467) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
      moonbit_decref_cycle_free(_M0L13handle__startS1401);
      _M0L6_2atmpS3405 = 1;
    } else {
      struct moonbit_result_0 _tmp_3468;
      int32_t _handle__error__result_3469;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _tmp_3468
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1434, _M0L8filenameS1403, _M0L5indexS1405, _M0L13handle__startS1401, _M0L14handle__resultS1406, _M0L17error__to__stringS1413);
      if (_tmp_3468.tag) {
        int32_t const _M0L5_2aokS3410 = _tmp_3468.data.ok;
        _handle__error__result_3469 = _M0L5_2aokS3410;
      } else {
        void* const _M0L6_2aerrS3411 = _tmp_3468.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
        moonbit_decref_cycle_free(_M0L13handle__startS1401);
        _M0L11_2atry__errS1428 = _M0L6_2aerrS3411;
        goto join_1427;
      }
      if (_handle__error__result_3469) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
        moonbit_decref_cycle_free(_M0L13handle__startS1401);
        _M0L6_2atmpS3405 = 1;
      } else {
        struct moonbit_result_0 _tmp_3470;
        int32_t _handle__error__result_3471;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
        _tmp_3470
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1434, _M0L8filenameS1403, _M0L5indexS1405, _M0L13handle__startS1401, _M0L14handle__resultS1406, _M0L17error__to__stringS1413);
        if (_tmp_3470.tag) {
          int32_t const _M0L5_2aokS3408 = _tmp_3470.data.ok;
          _handle__error__result_3471 = _M0L5_2aokS3408;
        } else {
          void* const _M0L6_2aerrS3409 = _tmp_3470.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
          moonbit_decref_cycle_free(_M0L13handle__startS1401);
          _M0L11_2atry__errS1428 = _M0L6_2aerrS3409;
          goto join_1427;
        }
        if (_handle__error__result_3471) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
          moonbit_decref_cycle_free(_M0L13handle__startS1401);
          _M0L6_2atmpS3405 = 1;
        } else {
          struct moonbit_result_0 _tmp_3472;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
          _tmp_3472
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1434, _M0L8filenameS1403, _M0L5indexS1405, _M0L13handle__startS1401, _M0L14handle__resultS1406, _M0L17error__to__stringS1413);
          moonbit_decref_cycle_free(_M0L13handle__startS1401);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1413);
          if (_tmp_3472.tag) {
            int32_t const _M0L5_2aokS3406 = _tmp_3472.data.ok;
            _M0L6_2atmpS3405 = _M0L5_2aokS3406;
          } else {
            void* const _M0L6_2aerrS3407 = _tmp_3472.data.err;
            _M0L11_2atry__errS1428 = _M0L6_2aerrS3407;
            goto join_1427;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS3405) {
    void* _M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3416 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3416)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3416)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1428
    = _M0L134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3416;
    goto join_1427;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1406);
  }
  goto joinlet_3463;
  join_1427:;
  _M0L3errS1429 = _M0L11_2atry__errS1428;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1432
  = (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1429;
  _M0L7_2anameS1433 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1432->$0;
  _M0L6_2acntS3455
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1432));
  if (_M0L6_2acntS3455 > 1) {
    int32_t _M0L11_2anew__cntS3456 = _M0L6_2acntS3455 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1432), _M0L11_2anew__cntS3456);
    moonbit_incref_cycle_free(_M0L7_2anameS1433);
  } else if (_M0L6_2acntS3455 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1432);
  }
  _M0L4nameS1431 = _M0L7_2anameS1433;
  goto join_1430;
  goto joinlet_3473;
  join_1430:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1406(_M0L14handle__resultS1406, _M0L4nameS1431, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1406);
  moonbit_decref_cycle_free(_M0L4nameS1431);
  joinlet_3473:;
  joinlet_3463:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1413(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS3404,
  void* _M0L3errS1414
) {
  void* _M0L1eS1416;
  moonbit_string_t _M0L1eS1418;
  moonbit_string_t _result_3476;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1414)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1419 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1414;
      moonbit_string_t _M0L4_2aeS1420 = _M0L10_2aFailureS1419->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1420);
      _M0L1eS1418 = _M0L4_2aeS1420;
      goto join_1417;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1421 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1414;
      moonbit_string_t _M0L4_2aeS1422 = _M0L15_2aInspectErrorS1421->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1422);
      _M0L1eS1418 = _M0L4_2aeS1422;
      goto join_1417;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1423 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1414;
      moonbit_string_t _M0L4_2aeS1424 = _M0L16_2aSnapshotErrorS1423->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1424);
      _M0L1eS1418 = _M0L4_2aeS1424;
      goto join_1417;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1425 =
        (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1414;
      moonbit_string_t _M0L4_2aeS1426 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1425->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1426);
      _M0L1eS1418 = _M0L4_2aeS1426;
      goto join_1417;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1414);
      _M0L1eS1416 = _M0L3errS1414;
      goto join_1415;
      break;
    }
  }
  join_1417:;
  return _M0L1eS1418;
  join_1415:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3476 = _M0FP15Error10to__string(_M0L1eS1416);
  moonbit_decref_cycle_free(_M0L1eS1416);
  return _result_3476;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1406(
  struct _M0TWssbEu* _M0L6_2aenvS3401,
  moonbit_string_t _M0L10__testnameS1407,
  moonbit_string_t _M0L7messageS1408,
  int32_t _M0L7skippedS1409
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406* _M0L14_2acasted__envS3402;
  moonbit_string_t _M0L8filenameS1403;
  int32_t _M0L5indexS1405;
  moonbit_string_t _M0L10file__nameS1410;
  moonbit_string_t _M0L7messageS1411;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1412;
  moonbit_string_t _M0L6_2atmpS3403;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3402
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1406*)_M0L6_2aenvS3401;
  _M0L8filenameS1403 = _M0L14_2acasted__envS3402->$1;
  _M0L5indexS1405 = _M0L14_2acasted__envS3402->$0;
  if (!_M0L7skippedS1409 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1410
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1403, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1411
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1408, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1412
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1412, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1412, _M0L10file__nameS1410);
  moonbit_decref_cycle_free(_M0L10file__nameS1410);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1412, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1412, _M0L5indexS1405);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1412, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1412, _M0L7messageS1411);
  moonbit_decref_cycle_free(_M0L7messageS1411);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1412, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3403
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1412);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1412);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3403);
  moonbit_decref_cycle_free(_M0L6_2atmpS3403);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1401(
  struct _M0TWEu* _M0L6_2aenvS3398
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401* _M0L14_2acasted__envS3399;
  moonbit_string_t _M0L8filenameS1403;
  int32_t _M0L5indexS1405;
  moonbit_string_t _M0L10file__nameS1402;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1404;
  moonbit_string_t _M0L6_2atmpS3400;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3399
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2ftripod__network__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1401*)_M0L6_2aenvS3398;
  _M0L8filenameS1403 = _M0L14_2acasted__envS3399->$1;
  _M0L5indexS1405 = _M0L14_2acasted__envS3399->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1402
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1403, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1404
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1404, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1404, _M0L10file__nameS1402);
  moonbit_decref_cycle_free(_M0L10file__nameS1402);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1404, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1404, _M0L5indexS1405);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1404, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3400
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1404);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1404);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3400);
  moonbit_decref_cycle_free(_M0L6_2atmpS3400);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1371;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1378;
  struct _M0TUsiE** _M0L6_2atmpS3397;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1385;
  moonbit_string_t* _M0L9cli__argsS1386;
  moonbit_string_t _M0L6_2atmpS3396;
  moonbit_string_t _M0L6_2atmpS3395;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1387;
  int32_t _M0L7_2abindS1388;
  moonbit_string_t* _M0L7_2abindS1389;
  int32_t _M0L6_2acntS3457;
  int32_t _M0L2__S1390;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1371 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1378 = 0;
  _M0L6_2atmpS3397 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1385
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1385)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1385->$0 = _M0L6_2atmpS3397;
  _M0L16file__and__indexS1385->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1386
  = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1386)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS3396 = (moonbit_string_t)_M0L9cli__argsS1386[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS3396);
  moonbit_decref_cycle_free(_M0L9cli__argsS1386);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3395
  = _M0MP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS3396);
  moonbit_decref_cycle_free(_M0L6_2atmpS3396);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1387
  = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1378(_M0L51moonbit__test__driver__internal__split__mbt__stringS1378, _M0L6_2atmpS3395, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS3395);
  _M0L7_2abindS1388 = _M0L10test__argsS1387->$1;
  _M0L7_2abindS1389 = _M0L10test__argsS1387->$0;
  _M0L6_2acntS3457
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1387));
  if (_M0L6_2acntS3457 > 1) {
    int32_t _M0L11_2anew__cntS3458 = _M0L6_2acntS3457 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1387), _M0L11_2anew__cntS3458);
    moonbit_incref_cycle_free(_M0L7_2abindS1389);
  } else if (_M0L6_2acntS3457 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1387);
  }
  _M0L2__S1390 = 0;
  while (1) {
    if (_M0L2__S1390 < _M0L7_2abindS1388) {
      moonbit_string_t _M0L3argS1391 =
        (moonbit_string_t)_M0L7_2abindS1389[_M0L2__S1390];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1392;
      moonbit_string_t _M0L4fileS1393;
      moonbit_string_t _M0L5rangeS1394;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1395;
      moonbit_string_t _M0L6_2atmpS3393;
      int32_t _M0L5startS1396;
      moonbit_string_t _M0L6_2atmpS3392;
      int32_t _M0L3endS1397;
      int32_t _M0L1iS1398;
      int32_t _M0L6_2atmpS3394;
      moonbit_incref_cycle_free(_M0L3argS1391);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1392
      = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1378(_M0L51moonbit__test__driver__internal__split__mbt__stringS1378, _M0L3argS1391, 58);
      moonbit_decref_cycle_free(_M0L3argS1391);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1393
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1392, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1394
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1392, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1392);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1395
      = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1378(_M0L51moonbit__test__driver__internal__split__mbt__stringS1378, _M0L5rangeS1394, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1394);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3393
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1395, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1396
      = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1371(_M0L45moonbit__test__driver__internal__parse__int__S1371, _M0L6_2atmpS3393);
      moonbit_decref_cycle_free(_M0L6_2atmpS3393);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3392
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1395, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1395);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1397
      = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1371(_M0L45moonbit__test__driver__internal__parse__int__S1371, _M0L6_2atmpS3392);
      moonbit_decref_cycle_free(_M0L6_2atmpS3392);
      _M0L1iS1398 = _M0L5startS1396;
      while (1) {
        if (_M0L1iS1398 < _M0L3endS1397) {
          struct _M0TUsiE* _M0L8_2atupleS3390;
          int32_t _M0L6_2atmpS3391;
          moonbit_incref_cycle_free(_M0L4fileS1393);
          _M0L8_2atupleS3390
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS3390)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS3390->$0 = _M0L4fileS1393;
          _M0L8_2atupleS3390->$1 = _M0L1iS1398;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1385, _M0L8_2atupleS3390);
          _M0L6_2atmpS3391 = _M0L1iS1398 + 1;
          _M0L1iS1398 = _M0L6_2atmpS3391;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1393);
        }
        break;
      }
      _M0L6_2atmpS3394 = _M0L2__S1390 + 1;
      _M0L2__S1390 = _M0L6_2atmpS3394;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1389);
    }
    break;
  }
  return _M0L16file__and__indexS1385;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1378(
  int32_t _M0L6_2aenvS3371,
  moonbit_string_t _M0L1sS1379,
  int32_t _M0L3sepS1380
) {
  moonbit_string_t* _M0L6_2atmpS3389;
  struct _M0TPB5ArrayGsE* _M0L3resS1381;
  struct _M0TPB8MutLocalGiE* _M0L1iS1382;
  struct _M0TPB8MutLocalGiE* _M0L5startS1383;
  int32_t _M0L3valS3384;
  int32_t _M0L6_2atmpS3385;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3389 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1381
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1381)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1381->$0 = _M0L6_2atmpS3389;
  _M0L3resS1381->$1 = 0;
  _M0L1iS1382
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1382)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1382->$0 = 0;
  _M0L5startS1383
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1383)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1383->$0 = 0;
  while (1) {
    int32_t _M0L3valS3372 = _M0L1iS1382->$0;
    int32_t _M0L6_2atmpS3373 = Moonbit_array_length(_M0L1sS1379);
    if (_M0L3valS3372 < _M0L6_2atmpS3373) {
      int32_t _M0L3valS3376 = _M0L1iS1382->$0;
      int32_t _M0L6_2atmpS3375;
      int32_t _M0L6_2atmpS3374;
      int32_t _M0L3valS3383;
      int32_t _M0L6_2atmpS3382;
      if (
        _M0L3valS3376 < 0
        || _M0L3valS3376 >= Moonbit_array_length(_M0L1sS1379)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3375 = _M0L1sS1379[_M0L3valS3376];
      _M0L6_2atmpS3374 = _M0L6_2atmpS3375;
      if (_M0L6_2atmpS3374 == _M0L3sepS1380) {
        int32_t _M0L3valS3378 = _M0L5startS1383->$0;
        int32_t _M0L3valS3379 = _M0L1iS1382->$0;
        moonbit_string_t _M0L6_2atmpS3377;
        int32_t _M0L3valS3381;
        int32_t _M0L6_2atmpS3380;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS3377
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1379, _M0L3valS3378, _M0L3valS3379);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1381, _M0L6_2atmpS3377);
        _M0L3valS3381 = _M0L1iS1382->$0;
        _M0L6_2atmpS3380 = _M0L3valS3381 + 1;
        _M0L5startS1383->$0 = _M0L6_2atmpS3380;
      }
      _M0L3valS3383 = _M0L1iS1382->$0;
      _M0L6_2atmpS3382 = _M0L3valS3383 + 1;
      _M0L1iS1382->$0 = _M0L6_2atmpS3382;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1382);
    }
    break;
  }
  _M0L3valS3384 = _M0L5startS1383->$0;
  _M0L6_2atmpS3385 = Moonbit_array_length(_M0L1sS1379);
  if (_M0L3valS3384 < _M0L6_2atmpS3385) {
    int32_t _M0L3valS3387 = _M0L5startS1383->$0;
    int32_t _M0L6_2atmpS3388;
    moonbit_string_t _M0L6_2atmpS3386;
    moonbit_decref_cycle_free(_M0L5startS1383);
    _M0L6_2atmpS3388 = Moonbit_array_length(_M0L1sS1379);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS3386
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1379, _M0L3valS3387, _M0L6_2atmpS3388);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1381, _M0L6_2atmpS3386);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1383);
  }
  return _M0L3resS1381;
}

int32_t _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1371(
  int32_t _M0L6_2aenvS3364,
  moonbit_string_t _M0L1sS1372
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1373;
  int32_t _M0L3lenS1374;
  int32_t _M0L7_2abindS1375;
  int32_t _M0L1iS1376;
  int32_t _result_3481;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1373
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1373)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1373->$0 = 0;
  _M0L3lenS1374 = Moonbit_array_length(_M0L1sS1372);
  _M0L7_2abindS1375 = 0;
  _M0L1iS1376 = _M0L7_2abindS1375;
  while (1) {
    if (_M0L1iS1376 < _M0L3lenS1374) {
      int32_t _M0L3valS3369 = _M0L3resS1373->$0;
      int32_t _M0L6_2atmpS3366 = _M0L3valS3369 * 10;
      int32_t _M0L6_2atmpS3368;
      int32_t _M0L6_2atmpS3367;
      int32_t _M0L6_2atmpS3365;
      int32_t _M0L6_2atmpS3370;
      if (
        _M0L1iS1376 < 0 || _M0L1iS1376 >= Moonbit_array_length(_M0L1sS1372)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3368 = _M0L1sS1372[_M0L1iS1376];
      _M0L6_2atmpS3367 = _M0L6_2atmpS3368 - 48;
      _M0L6_2atmpS3365 = _M0L6_2atmpS3366 + _M0L6_2atmpS3367;
      _M0L3resS1373->$0 = _M0L6_2atmpS3365;
      _M0L6_2atmpS3370 = _M0L1iS1376 + 1;
      _M0L1iS1376 = _M0L6_2atmpS3370;
      continue;
    }
    break;
  }
  _result_3481 = _M0L3resS1373->$0;
  moonbit_decref_cycle_free(_M0L3resS1373);
  return _result_3481;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31tripod__network__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1370
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1370);
  return _M0L4selfS1370;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1340,
  moonbit_string_t _M0L12_2adiscard__S1341,
  int32_t _M0L12_2adiscard__S1342,
  struct _M0TWEu* _M0L12_2adiscard__S1343,
  struct _M0TWssbEu* _M0L12_2adiscard__S1344,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1345
) {
  struct moonbit_result_0 _result_3482;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3482.tag = 1;
  _result_3482.data.ok = 0;
  return _result_3482;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1346,
  moonbit_string_t _M0L12_2adiscard__S1347,
  int32_t _M0L12_2adiscard__S1348,
  struct _M0TWEu* _M0L12_2adiscard__S1349,
  struct _M0TWssbEu* _M0L12_2adiscard__S1350,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1351
) {
  struct moonbit_result_0 _result_3483;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3483.tag = 1;
  _result_3483.data.ok = 0;
  return _result_3483;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1352,
  moonbit_string_t _M0L12_2adiscard__S1353,
  int32_t _M0L12_2adiscard__S1354,
  struct _M0TWEu* _M0L12_2adiscard__S1355,
  struct _M0TWssbEu* _M0L12_2adiscard__S1356,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1357
) {
  struct moonbit_result_0 _result_3484;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3484.tag = 1;
  _result_3484.data.ok = 0;
  return _result_3484;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1358,
  moonbit_string_t _M0L12_2adiscard__S1359,
  int32_t _M0L12_2adiscard__S1360,
  struct _M0TWEu* _M0L12_2adiscard__S1361,
  struct _M0TWssbEu* _M0L12_2adiscard__S1362,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1363
) {
  struct moonbit_result_0 _result_3485;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3485.tag = 1;
  _result_3485.data.ok = 0;
  return _result_3485;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1364,
  moonbit_string_t _M0L12_2adiscard__S1365,
  int32_t _M0L12_2adiscard__S1366,
  struct _M0TWEu* _M0L12_2adiscard__S1367,
  struct _M0TWssbEu* _M0L12_2adiscard__S1368,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1369
) {
  struct moonbit_result_0 _result_3486;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _result_3486.tag = 1;
  _result_3486.data.ok = 0;
  return _result_3486;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1339
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1273,
  float _M0L6t__nowS1283
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS3363;
  int32_t _M0L6_2atmpS3362;
  int32_t _M0L10use__delayS1272;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3361;
  int32_t _M0L6_2atmpS3360;
  int32_t _M0L8use__rhoS1274;
  #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6delaysS3363 = _M0L1cS1273->$6;
  #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS3362 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS3363);
  _M0L10use__delayS1272 = _M0L6_2atmpS3362 > 0;
  _M0L3rhoS3361 = _M0L1cS1273->$5;
  #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS3360 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3361);
  _M0L8use__rhoS1274 = _M0L6_2atmpS3360 > 0;
  if (_M0L10use__delayS1272) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3317 = _M0L1cS1273->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS3316 = _M0L3preS3317->$5;
    int32_t _M0L6n__preS1275;
    struct _M0TPB8MutLocalGiE* _M0L1jS1276;
    #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    _M0L6n__preS1275 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3316);
    _M0L1jS1276
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1276)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1276->$0 = 0;
    while (1) {
      int32_t _M0L3valS3281 = _M0L1jS1276->$0;
      if (_M0L3valS3281 < _M0L6n__preS1275) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3284 = _M0L1cS1273->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS3282 = _M0L3preS3284->$5;
        int32_t _M0L3valS3283 = _M0L1jS1276->$0;
        int32_t _M0L3valS3315;
        int32_t _M0L6_2atmpS3314;
        #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS3282, _M0L3valS3283)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3313 =
            _M0L1cS1273->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3311 = _M0L6matrixS3313->$2;
          int32_t _M0L3valS3312 = _M0L1jS1276->$0;
          int32_t _M0L5startS1277;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3310;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3307;
          int32_t _M0L3valS3309;
          int32_t _M0L6_2atmpS3308;
          int32_t _M0L3endS1278;
          struct _M0TPB8MutLocalGiE* _M0L1sS1279;
          #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L5startS1277
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3311, _M0L3valS3312);
          _M0L6matrixS3310 = _M0L1cS1273->$4;
          _M0L6rowptrS3307 = _M0L6matrixS3310->$2;
          _M0L3valS3309 = _M0L1jS1276->$0;
          _M0L6_2atmpS3308 = _M0L3valS3309 + 1;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L3endS1278
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3307, _M0L6_2atmpS3308);
          _M0L1sS1279
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1279)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1279->$0 = _M0L5startS1277;
          while (1) {
            int32_t _M0L3valS3285 = _M0L1sS1279->$0;
            if (_M0L3valS3285 < _M0L3endS1278) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3306 =
                _M0L1cS1273->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS3304 = _M0L6matrixS3306->$3;
              int32_t _M0L3valS3305 = _M0L1sS1279->$0;
              int32_t _M0L9post__idxS1280;
              float _M0L1wS1281;
              struct _M0TPB5ArrayGfE* _M0L6delaysS3292;
              int32_t _M0L3valS3293;
              float _M0L1dS1282;
              int32_t _M0L3valS3291;
              int32_t _M0L6_2atmpS3290;
              #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L9post__idxS1280
              = _M0MPC15array5Array2atGiE(_M0L6colptrS3304, _M0L3valS3305);
              if (_M0L8use__rhoS1274) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3300 =
                  _M0L1cS1273->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS3298 = _M0L6matrixS3300->$4;
                int32_t _M0L3valS3299 = _M0L1sS1279->$0;
                float _M0L6_2atmpS3294;
                struct _M0TPB5ArrayGfE* _M0L3rhoS3296;
                int32_t _M0L3valS3297;
                float _M0L6_2atmpS3295;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS3294
                = _M0MPC15array5Array2atGfE(_M0L4valsS3298, _M0L3valS3299);
                _M0L3rhoS3296 = _M0L1cS1273->$5;
                _M0L3valS3297 = _M0L1sS1279->$0;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS3295
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3296, _M0L3valS3297);
                _M0L1wS1281 = _M0L6_2atmpS3294 * _M0L6_2atmpS3295;
              } else {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3303 =
                  _M0L1cS1273->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS3301 = _M0L6matrixS3303->$4;
                int32_t _M0L3valS3302 = _M0L1sS1279->$0;
                #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L1wS1281
                = _M0MPC15array5Array2atGfE(_M0L4valsS3301, _M0L3valS3302);
              }
              _M0L6delaysS3292 = _M0L1cS1273->$6;
              _M0L3valS3293 = _M0L1sS1279->$0;
              #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L1dS1282
              = _M0MPC15array5Array2atGfE(_M0L6delaysS3292, _M0L3valS3293);
              if (_M0L1dS1282 == 0x0p+0f) {
                #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(_M0L1cS1273, _M0L9post__idxS1280, _M0L1wS1281);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS3286 =
                  _M0L1cS1273->$7;
                float _M0L6_2atmpS3287 = _M0L6t__nowS1283 + _M0L1dS1282;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS3288;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3289;
                #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS3286, _M0L6_2atmpS3287);
                _M0L14pending__postsS3288 = _M0L1cS1273->$8;
                #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS3288, _M0L9post__idxS1280);
                _M0L16pending__weightsS3289 = _M0L1cS1273->$9;
                #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS3289, _M0L1wS1281);
              }
              _M0L3valS3291 = _M0L1sS1279->$0;
              _M0L6_2atmpS3290 = _M0L3valS3291 + 1;
              _M0L1sS1279->$0 = _M0L6_2atmpS3290;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1279);
            }
            break;
          }
        }
        _M0L3valS3315 = _M0L1jS1276->$0;
        _M0L6_2atmpS3314 = _M0L3valS3315 + 1;
        _M0L1jS1276->$0 = _M0L6_2atmpS3314;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1276);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS3351 = _M0L1cS1273->$2;
    struct _M0TPB5ArrayGfE* _M0L3bufS1286;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    if (
      _M0L3symS3351 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS3351)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS3351, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3351) * 2)
    ) {
      moonbit_string_t _M0L7_2abindS1287 = _M0L1cS1273->$3;
      if (
        _M0L7_2abindS1287 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L7_2abindS1287) == 4
           && 0
              == memcmp(_M0L7_2abindS1287, (moonbit_string_t)moonbit_string_literal_12.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3352 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3418 = _M0L4postS3352->$13;
        moonbit_incref_cycle_free(_M0L8_2afieldS3418);
        _M0L3bufS1286 = _M0L8_2afieldS3418;
      } else if (
               _M0L7_2abindS1287
               == (moonbit_string_t)moonbit_string_literal_11.data
               || Moonbit_array_length(_M0L7_2abindS1287) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1287, (moonbit_string_t)moonbit_string_literal_11.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3353 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3419 = _M0L4postS3353->$15;
        moonbit_incref_cycle_free(_M0L8_2afieldS3419);
        _M0L3bufS1286 = _M0L8_2afieldS3419;
      } else if (
               _M0L7_2abindS1287
               == (moonbit_string_t)moonbit_string_literal_10.data
               || Moonbit_array_length(_M0L7_2abindS1287) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1287, (moonbit_string_t)moonbit_string_literal_10.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3354 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3420 = _M0L4postS3354->$17;
        moonbit_incref_cycle_free(_M0L8_2afieldS3420);
        _M0L3bufS1286 = _M0L8_2afieldS3420;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3355 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3421 = _M0L4postS3355->$13;
        moonbit_incref_cycle_free(_M0L8_2afieldS3421);
        _M0L3bufS1286 = _M0L8_2afieldS3421;
      }
    } else {
      moonbit_string_t _M0L7_2abindS1288 = _M0L1cS1273->$3;
      if (
        _M0L7_2abindS1288 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L7_2abindS1288) == 4
           && 0
              == memcmp(_M0L7_2abindS1288, (moonbit_string_t)moonbit_string_literal_12.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3356 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3422 = _M0L4postS3356->$14;
        moonbit_incref_cycle_free(_M0L8_2afieldS3422);
        _M0L3bufS1286 = _M0L8_2afieldS3422;
      } else if (
               _M0L7_2abindS1288
               == (moonbit_string_t)moonbit_string_literal_11.data
               || Moonbit_array_length(_M0L7_2abindS1288) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1288, (moonbit_string_t)moonbit_string_literal_11.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3357 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3423 = _M0L4postS3357->$16;
        moonbit_incref_cycle_free(_M0L8_2afieldS3423);
        _M0L3bufS1286 = _M0L8_2afieldS3423;
      } else if (
               _M0L7_2abindS1288
               == (moonbit_string_t)moonbit_string_literal_10.data
               || Moonbit_array_length(_M0L7_2abindS1288) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1288, (moonbit_string_t)moonbit_string_literal_10.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3358 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3424 = _M0L4postS3358->$18;
        moonbit_incref_cycle_free(_M0L8_2afieldS3424);
        _M0L3bufS1286 = _M0L8_2afieldS3424;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3359 =
          _M0L1cS1273->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3425 = _M0L4postS3359->$14;
        moonbit_incref_cycle_free(_M0L8_2afieldS3425);
        _M0L3bufS1286 = _M0L8_2afieldS3425;
      }
    }
    if (_M0L8use__rhoS1274) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3347 = _M0L1cS1273->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3346 = _M0L3preS3347->$5;
      int32_t _M0L6n__preS1289;
      struct _M0TPB8MutLocalGiE* _M0L1jS1290;
      #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0L6n__preS1289 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3346);
      _M0L1jS1290
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1290)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1290->$0 = 0;
      while (1) {
        int32_t _M0L3valS3318 = _M0L1jS1290->$0;
        if (_M0L3valS3318 < _M0L6n__preS1289) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3321 = _M0L1cS1273->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS3319 = _M0L3preS3321->$5;
          int32_t _M0L3valS3320 = _M0L1jS1290->$0;
          int32_t _M0L3valS3345;
          int32_t _M0L6_2atmpS3344;
          #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS3319, _M0L3valS3320)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3343 =
              _M0L1cS1273->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3341 = _M0L6matrixS3343->$2;
            int32_t _M0L3valS3342 = _M0L1jS1290->$0;
            int32_t _M0L5startS1291;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3340;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3337;
            int32_t _M0L3valS3339;
            int32_t _M0L6_2atmpS3338;
            int32_t _M0L3endS1292;
            struct _M0TPB8MutLocalGiE* _M0L1sS1293;
            #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L5startS1291
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3341, _M0L3valS3342);
            _M0L6matrixS3340 = _M0L1cS1273->$4;
            _M0L6rowptrS3337 = _M0L6matrixS3340->$2;
            _M0L3valS3339 = _M0L1jS1290->$0;
            _M0L6_2atmpS3338 = _M0L3valS3339 + 1;
            #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L3endS1292
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3337, _M0L6_2atmpS3338);
            _M0L1sS1293
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1293)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1293->$0 = _M0L5startS1291;
            while (1) {
              int32_t _M0L3valS3322 = _M0L1sS1293->$0;
              if (_M0L3valS3322 < _M0L3endS1292) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3336 =
                  _M0L1cS1273->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS3334 =
                  _M0L6matrixS3336->$3;
                int32_t _M0L3valS3335 = _M0L1sS1293->$0;
                int32_t _M0L9post__idxS1294;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3333;
                struct _M0TPB5ArrayGfE* _M0L4valsS3331;
                int32_t _M0L3valS3332;
                float _M0L6_2atmpS3327;
                struct _M0TPB5ArrayGfE* _M0L3rhoS3329;
                int32_t _M0L3valS3330;
                float _M0L6_2atmpS3328;
                float _M0L9w__scaledS1295;
                float _M0L6_2atmpS3324;
                float _M0L6_2atmpS3323;
                int32_t _M0L3valS3326;
                int32_t _M0L6_2atmpS3325;
                #line 284 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L9post__idxS1294
                = _M0MPC15array5Array2atGiE(_M0L6colptrS3334, _M0L3valS3335);
                _M0L6matrixS3333 = _M0L1cS1273->$4;
                _M0L4valsS3331 = _M0L6matrixS3333->$4;
                _M0L3valS3332 = _M0L1sS1293->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS3327
                = _M0MPC15array5Array2atGfE(_M0L4valsS3331, _M0L3valS3332);
                _M0L3rhoS3329 = _M0L1cS1273->$5;
                _M0L3valS3330 = _M0L1sS1293->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS3328
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3329, _M0L3valS3330);
                _M0L9w__scaledS1295 = _M0L6_2atmpS3327 * _M0L6_2atmpS3328;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS3324
                = _M0MPC15array5Array2atGfE(_M0L3bufS1286, _M0L9post__idxS1294);
                _M0L6_2atmpS3323 = _M0L6_2atmpS3324 + _M0L9w__scaledS1295;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array3setGfE(_M0L3bufS1286, _M0L9post__idxS1294, _M0L6_2atmpS3323);
                _M0L3valS3326 = _M0L1sS1293->$0;
                _M0L6_2atmpS3325 = _M0L3valS3326 + 1;
                _M0L1sS1293->$0 = _M0L6_2atmpS3325;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1293);
              }
              break;
            }
          }
          _M0L3valS3345 = _M0L1jS1290->$0;
          _M0L6_2atmpS3344 = _M0L3valS3345 + 1;
          _M0L1jS1290->$0 = _M0L6_2atmpS3344;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1290);
          moonbit_decref_cycle_free(_M0L3bufS1286);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3348 =
        _M0L1cS1273->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3350 = _M0L1cS1273->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3349 = _M0L3preS3350->$5;
      #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS3348, _M0L4fireS3349, _M0L3bufS1286);
      moonbit_decref_cycle_free(_M0L3bufS1286);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1267,
  int32_t _M0L9post__idxS1270,
  float _M0L1wS1271
) {
  moonbit_string_t _M0L3symS3280;
  int32_t _M0L6is__geS1266;
  moonbit_string_t _M0L7_2abindS1269;
  struct _M0TPB5ArrayGfE* _M0L3bufS1268;
  float _M0L6_2atmpS3272;
  float _M0L6_2atmpS3271;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L3symS3280 = _M0L1cS1267->$2;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6is__geS1266
  = _M0L3symS3280 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3280)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3280, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3280) * 2);
  _M0L7_2abindS1269 = _M0L1cS1267->$3;
  if (
    _M0L7_2abindS1269 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS1269) == 4
       && 0
          == memcmp(_M0L7_2abindS1269, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (_M0L6is__geS1266 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3274 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3426 = _M0L4postS3274->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS3426);
      _M0L3bufS1268 = _M0L8_2afieldS3426;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3273 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3427 = _M0L4postS3273->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS3427);
      _M0L3bufS1268 = _M0L8_2afieldS3427;
    }
  } else if (
           _M0L7_2abindS1269
           == (moonbit_string_t)moonbit_string_literal_11.data
           || Moonbit_array_length(_M0L7_2abindS1269) == 2
              && 0
                 == memcmp(_M0L7_2abindS1269, (moonbit_string_t)moonbit_string_literal_11.data, 4)
         ) {
    if (_M0L6is__geS1266 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3276 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3428 = _M0L4postS3276->$15;
      moonbit_incref_cycle_free(_M0L8_2afieldS3428);
      _M0L3bufS1268 = _M0L8_2afieldS3428;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3275 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3429 = _M0L4postS3275->$16;
      moonbit_incref_cycle_free(_M0L8_2afieldS3429);
      _M0L3bufS1268 = _M0L8_2afieldS3429;
    }
  } else if (
           _M0L7_2abindS1269
           == (moonbit_string_t)moonbit_string_literal_10.data
           || Moonbit_array_length(_M0L7_2abindS1269) == 2
              && 0
                 == memcmp(_M0L7_2abindS1269, (moonbit_string_t)moonbit_string_literal_10.data, 4)
         ) {
    if (_M0L6is__geS1266 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3278 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3430 = _M0L4postS3278->$17;
      moonbit_incref_cycle_free(_M0L8_2afieldS3430);
      _M0L3bufS1268 = _M0L8_2afieldS3430;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3277 = _M0L1cS1267->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3431 = _M0L4postS3277->$18;
      moonbit_incref_cycle_free(_M0L8_2afieldS3431);
      _M0L3bufS1268 = _M0L8_2afieldS3431;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3279 = _M0L1cS1267->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS3432 = _M0L4postS3279->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS3432);
    _M0L3bufS1268 = _M0L8_2afieldS3432;
  }
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS3272
  = _M0MPC15array5Array2atGfE(_M0L3bufS1268, _M0L9post__idxS1270);
  _M0L6_2atmpS3271 = _M0L6_2atmpS3272 + _M0L1wS1271;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0MPC15array5Array3setGfE(_M0L3bufS1268, _M0L9post__idxS1270, _M0L6_2atmpS3271);
  moonbit_decref_cycle_free(_M0L3bufS1268);
  return 0;
}

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1258,
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS1259,
  moonbit_string_t _M0L3symS1264,
  moonbit_string_t _M0L6targetS1265,
  float _M0L2muS1260,
  float _M0L5sigmaS1261,
  float _M0L1pS1262,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1263
) {
  int32_t _M0L1nS3269;
  int32_t _M0L1nS3270;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1257;
  float* _M0L6_2atmpS3268;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3259;
  float* _M0L6_2atmpS3267;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3260;
  float* _M0L6_2atmpS3266;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3261;
  int32_t* _M0L6_2atmpS3265;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS3262;
  float* _M0L6_2atmpS3264;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3263;
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _block_3491;
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1nS3269 = _M0L3preS1258->$2;
  _M0L1nS3270 = _M0L4postS1259->$25;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1mS1257
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS3269, _M0L1nS3270, _M0L2muS1260, _M0L5sigmaS1261, _M0L1pS1262, _M0L3rngS1263);
  _M0L6_2atmpS3268 = moonbit_empty_float_array;
  _M0L6_2atmpS3259
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3259)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3259->$0 = _M0L6_2atmpS3268;
  _M0L6_2atmpS3259->$1 = 0;
  _M0L6_2atmpS3267 = moonbit_empty_float_array;
  _M0L6_2atmpS3260
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3260)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3260->$0 = _M0L6_2atmpS3267;
  _M0L6_2atmpS3260->$1 = 0;
  _M0L6_2atmpS3266 = moonbit_empty_float_array;
  _M0L6_2atmpS3261
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3261)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3261->$0 = _M0L6_2atmpS3266;
  _M0L6_2atmpS3261->$1 = 0;
  _M0L6_2atmpS3265 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS3262
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS3262)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS3262->$0 = _M0L6_2atmpS3265;
  _M0L6_2atmpS3262->$1 = 0;
  _M0L6_2atmpS3264 = moonbit_empty_float_array;
  _M0L6_2atmpS3263
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3263)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3263->$0 = _M0L6_2atmpS3264;
  _M0L6_2atmpS3263->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1258);
  moonbit_incref_cycle_free(_M0L4postS1259);
  moonbit_incref_cycle_free(_M0L3symS1264);
  moonbit_incref_cycle_free(_M0L6targetS1265);
  _block_3491
  = (struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod));
  Moonbit_object_header(_block_3491)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_3491->$0 = _M0L3preS1258;
  _block_3491->$1 = _M0L4postS1259;
  _block_3491->$2 = _M0L3symS1264;
  _block_3491->$3 = _M0L6targetS1265;
  _block_3491->$4 = _M0L1mS1257;
  _block_3491->$5 = _M0L6_2atmpS3259;
  _block_3491->$6 = _M0L6_2atmpS3260;
  _block_3491->$7 = _M0L6_2atmpS3261;
  _block_3491->$8 = _M0L6_2atmpS3262;
  _block_3491->$9 = _M0L6_2atmpS3263;
  return _block_3491;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1253,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1230,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1232,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1249,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1243,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1240,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1236,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1234,
  float _M0L6t__nowS1228,
  float _M0L2dtS1237
) {
  int32_t _M0L6n__preS1229;
  int32_t _M0L7n__postS1231;
  float _M0L6tau__yS3258;
  float _M0L11inv__tau__yS1233;
  struct _M0TPB8MutLocalGiE* _M0L1jS1235;
  struct _M0TPB8MutLocalGiE* _M0L1iS1239;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1229 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1230);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1231 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1232);
  _M0L6tau__yS3258 = _M0L5paramS1234->$2;
  _M0L11inv__tau__yS1233 = 0x1p+0f / _M0L6tau__yS3258;
  _M0L1jS1235
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1235)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1235->$0 = 0;
  while (1) {
    int32_t _M0L3valS3175 = _M0L1jS1235->$0;
    if (_M0L3valS3175 < _M0L6n__preS1229) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3176 = _M0L4varsS1236->$0;
      int32_t _M0L3valS3177 = _M0L1jS1235->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3186 = _M0L4varsS1236->$0;
      int32_t _M0L3valS3187 = _M0L1jS1235->$0;
      float _M0L6_2atmpS3179;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3184;
      int32_t _M0L3valS3185;
      float _M0L6_2atmpS3183;
      float _M0L6_2atmpS3182;
      float _M0L6_2atmpS3181;
      float _M0L6_2atmpS3180;
      float _M0L6_2atmpS3178;
      int32_t _M0L3valS3188;
      int32_t _M0L3valS3196;
      int32_t _M0L6_2atmpS3195;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3179
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3186, _M0L3valS3187);
      _M0L4tpreS3184 = _M0L4varsS1236->$0;
      _M0L3valS3185 = _M0L1jS1235->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3183
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3184, _M0L3valS3185);
      _M0L6_2atmpS3182 = -_M0L6_2atmpS3183;
      _M0L6_2atmpS3181 = _M0L2dtS1237 * _M0L6_2atmpS3182;
      _M0L6_2atmpS3180 = _M0L6_2atmpS3181 * _M0L11inv__tau__yS1233;
      _M0L6_2atmpS3178 = _M0L6_2atmpS3179 + _M0L6_2atmpS3180;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3176, _M0L3valS3177, _M0L6_2atmpS3178);
      _M0L3valS3188 = _M0L1jS1235->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1230, _M0L3valS3188)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3189 = _M0L4varsS1236->$0;
        int32_t _M0L3valS3190 = _M0L1jS1235->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3193 = _M0L4varsS1236->$0;
        int32_t _M0L3valS3194 = _M0L1jS1235->$0;
        float _M0L6_2atmpS3192;
        float _M0L6_2atmpS3191;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS3192
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3193, _M0L3valS3194);
        _M0L6_2atmpS3191 = _M0L6_2atmpS3192 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3189, _M0L3valS3190, _M0L6_2atmpS3191);
      }
      _M0L3valS3196 = _M0L1jS1235->$0;
      _M0L6_2atmpS3195 = _M0L3valS3196 + 1;
      _M0L1jS1235->$0 = _M0L6_2atmpS3195;
      continue;
    }
    break;
  }
  _M0L1iS1239
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1239->$0 = 0;
  while (1) {
    int32_t _M0L3valS3197 = _M0L1iS1239->$0;
    if (_M0L3valS3197 < _M0L7n__postS1231) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3198 = _M0L4varsS1236->$1;
      int32_t _M0L3valS3199 = _M0L1iS1239->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3211 = _M0L4varsS1236->$1;
      int32_t _M0L3valS3212 = _M0L1iS1239->$0;
      float _M0L6_2atmpS3201;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3209;
      int32_t _M0L3valS3210;
      float _M0L6_2atmpS3206;
      int32_t _M0L3valS3208;
      float _M0L6_2atmpS3207;
      float _M0L6_2atmpS3205;
      float _M0L6_2atmpS3204;
      float _M0L6_2atmpS3203;
      float _M0L6_2atmpS3202;
      float _M0L6_2atmpS3200;
      int32_t _M0L3valS3213;
      int32_t _M0L3valS3221;
      int32_t _M0L6_2atmpS3220;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3201
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3211, _M0L3valS3212);
      _M0L5tpostS3209 = _M0L4varsS1236->$1;
      _M0L3valS3210 = _M0L1iS1239->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3206
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3209, _M0L3valS3210);
      _M0L3valS3208 = _M0L1iS1239->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3207
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1240, _M0L3valS3208);
      _M0L6_2atmpS3205 = _M0L6_2atmpS3206 - _M0L6_2atmpS3207;
      _M0L6_2atmpS3204 = -_M0L6_2atmpS3205;
      _M0L6_2atmpS3203 = _M0L2dtS1237 * _M0L6_2atmpS3204;
      _M0L6_2atmpS3202 = _M0L6_2atmpS3203 * _M0L11inv__tau__yS1233;
      _M0L6_2atmpS3200 = _M0L6_2atmpS3201 + _M0L6_2atmpS3202;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3198, _M0L3valS3199, _M0L6_2atmpS3200);
      _M0L3valS3213 = _M0L1iS1239->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1232, _M0L3valS3213)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3214 = _M0L4varsS1236->$1;
        int32_t _M0L3valS3215 = _M0L1iS1239->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3218 = _M0L4varsS1236->$1;
        int32_t _M0L3valS3219 = _M0L1iS1239->$0;
        float _M0L6_2atmpS3217;
        float _M0L6_2atmpS3216;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS3217
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3218, _M0L3valS3219);
        _M0L6_2atmpS3216 = _M0L6_2atmpS3217 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3214, _M0L3valS3215, _M0L6_2atmpS3216);
      }
      _M0L3valS3221 = _M0L1iS1239->$0;
      _M0L6_2atmpS3220 = _M0L3valS3221 + 1;
      _M0L1iS1239->$0 = _M0L6_2atmpS3220;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1239);
    }
    break;
  }
  _M0L1jS1235->$0 = 0;
  while (1) {
    int32_t _M0L3valS3222 = _M0L1jS1235->$0;
    if (_M0L3valS3222 < _M0L6n__preS1229) {
      int32_t _M0L3valS3257 = _M0L1jS1235->$0;
      int32_t _M0L5startS1242;
      int32_t _M0L3valS3256;
      int32_t _M0L6_2atmpS3255;
      int32_t _M0L3endS1244;
      int32_t _M0L3valS3254;
      int32_t _M0L10pre__firedS1245;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3252;
      int32_t _M0L3valS3253;
      float _M0L7tpre__jS1246;
      struct _M0TPB8MutLocalGiE* _M0L1sS1247;
      int32_t _M0L3valS3251;
      int32_t _M0L6_2atmpS3250;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1242
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1243, _M0L3valS3257);
      _M0L3valS3256 = _M0L1jS1235->$0;
      _M0L6_2atmpS3255 = _M0L3valS3256 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1244
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1243, _M0L6_2atmpS3255);
      _M0L3valS3254 = _M0L1jS1235->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1245
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1230, _M0L3valS3254);
      _M0L4tpreS3252 = _M0L4varsS1236->$0;
      _M0L3valS3253 = _M0L1jS1235->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1246
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3252, _M0L3valS3253);
      _M0L1sS1247
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1247)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1247->$0 = _M0L5startS1242;
      while (1) {
        int32_t _M0L3valS3223 = _M0L1sS1247->$0;
        if (_M0L3valS3223 < _M0L3endS1244) {
          int32_t _M0L3valS3249 = _M0L1sS1247->$0;
          int32_t _M0L9post__idxS1248;
          int32_t _M0L11post__firedS1250;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3248;
          float _M0L8tpost__iS1251;
          int32_t _M0L3valS3238;
          float _M0L6_2atmpS3236;
          float _M0L6w__minS3237;
          int32_t _M0L3valS3243;
          float _M0L6_2atmpS3241;
          float _M0L6w__maxS3242;
          int32_t _M0L3valS3247;
          int32_t _M0L6_2atmpS3246;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1248
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1249, _M0L3valS3249);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1250
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1232, _M0L9post__idxS1248);
          _M0L5tpostS3248 = _M0L4varsS1236->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1251
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3248, _M0L9post__idxS1248);
          if (_M0L10pre__firedS1245) {
            float _M0L3etaS3228 = _M0L5paramS1234->$0;
            float _M0L2v0S3230 = _M0L5paramS1234->$1;
            float _M0L6_2atmpS3229 = _M0L8tpost__iS1251 - _M0L2v0S3230;
            float _M0L2dwS1252 = _M0L3etaS3228 * _M0L6_2atmpS3229;
            int32_t _M0L3valS3224 = _M0L1sS1247->$0;
            int32_t _M0L3valS3227 = _M0L1sS1247->$0;
            float _M0L6_2atmpS3226;
            float _M0L6_2atmpS3225;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS3226
            = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3227);
            _M0L6_2atmpS3225 = _M0L6_2atmpS3226 + _M0L2dwS1252;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3224, _M0L6_2atmpS3225);
          }
          if (_M0L11post__firedS1250) {
            float _M0L3etaS3235 = _M0L5paramS1234->$0;
            float _M0L2dwS1254 = _M0L3etaS3235 * _M0L7tpre__jS1246;
            int32_t _M0L3valS3231 = _M0L1sS1247->$0;
            int32_t _M0L3valS3234 = _M0L1sS1247->$0;
            float _M0L6_2atmpS3233;
            float _M0L6_2atmpS3232;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS3233
            = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3234);
            _M0L6_2atmpS3232 = _M0L6_2atmpS3233 + _M0L2dwS1254;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3231, _M0L6_2atmpS3232);
          }
          _M0L3valS3238 = _M0L1sS1247->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS3236
          = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3238);
          _M0L6w__minS3237 = _M0L5paramS1234->$4;
          if (_M0L6_2atmpS3236 < _M0L6w__minS3237) {
            int32_t _M0L3valS3239 = _M0L1sS1247->$0;
            float _M0L6w__minS3240 = _M0L5paramS1234->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3239, _M0L6w__minS3240);
          }
          _M0L3valS3243 = _M0L1sS1247->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS3241
          = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3243);
          _M0L6w__maxS3242 = _M0L5paramS1234->$3;
          if (_M0L6_2atmpS3241 > _M0L6w__maxS3242) {
            int32_t _M0L3valS3244 = _M0L1sS1247->$0;
            float _M0L6w__maxS3245 = _M0L5paramS1234->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3244, _M0L6w__maxS3245);
          }
          _M0L3valS3247 = _M0L1sS1247->$0;
          _M0L6_2atmpS3246 = _M0L3valS3247 + 1;
          _M0L1sS1247->$0 = _M0L6_2atmpS3246;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1247);
        }
        break;
      }
      _M0L3valS3251 = _M0L1jS1235->$0;
      _M0L6_2atmpS3250 = _M0L3valS3251 + 1;
      _M0L1jS1235->$0 = _M0L6_2atmpS3250;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1235);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry3new(
  int32_t _M0L11conn__indexS1225,
  int32_t _M0L6n__preS1226,
  int32_t _M0L7n__postS1227,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L11param_2eoptS1223
) {
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1222;
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _result_3496;
  if (_M0L11param_2eoptS1223 == 0) {
    #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
    _M0L5paramS1222 = _M0MP26RiantR8snn__mbt14IstdpPotential3new();
  } else {
    struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L7_2aSomeS1224 =
      _M0L11param_2eoptS1223;
    if (_M0L7_2aSomeS1224) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1224);
    }
    _M0L5paramS1222 = _M0L7_2aSomeS1224;
  }
  _result_3496
  = _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(_M0L11conn__indexS1225, _M0L6n__preS1226, _M0L7n__postS1227, _M0L5paramS1222);
  moonbit_decref_cycle_free(_M0L5paramS1222);
  return _result_3496;
}

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(
  int32_t _M0L11conn__indexS1218,
  int32_t _M0L6n__preS1219,
  int32_t _M0L7n__postS1220,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1221
) {
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L6_2atmpS3172;
  float* _M0L6_2atmpS3174;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3173;
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _block_3497;
  #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3172
  = _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(_M0L6n__preS1219, _M0L7n__postS1220);
  _M0L6_2atmpS3174 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3174[0] = 0x0p+0f;
  _M0L6_2atmpS3173
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3173)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3173->$0 = _M0L6_2atmpS3174;
  _M0L6_2atmpS3173->$1 = 1;
  moonbit_incref_cycle_free(_M0L5paramS1221);
  _block_3497
  = (struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry));
  Moonbit_object_header(_block_3497)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_3497->$0 = _M0L11conn__indexS1218;
  _block_3497->$1 = _M0L6n__preS1219;
  _block_3497->$2 = _M0L7n__postS1220;
  _block_3497->$3 = _M0L5paramS1221;
  _block_3497->$4 = _M0L6_2atmpS3172;
  _block_3497->$5 = _M0L6_2atmpS3173;
  return _block_3497;
}

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(
  int32_t _M0L6n__preS1216,
  int32_t _M0L7n__postS1217
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3170;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3171;
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _block_3498;
  #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3170 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1216, 0x0p+0f);
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3171 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1217, 0x0p+0f);
  _block_3498
  = (struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables));
  Moonbit_object_header(_block_3498)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _block_3498->$0 = _M0L6_2atmpS3170;
  _block_3498->$1 = _M0L6_2atmpS3171;
  return _block_3498;
}

struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0MP26RiantR8snn__mbt14IstdpPotential3new(
  
) {
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _block_3499;
  #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _block_3499
  = (struct _M0TP26RiantR8snn__mbt14IstdpPotential*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14IstdpPotential));
  Moonbit_object_header(_block_3499)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3499->$0 = 0x1.0624dd2f1a9fcp-10f;
  _block_3499->$1 = -0x1.9p+5f;
  _block_3499->$2 = 0x1.9p+7f;
  _block_3499->$3 = 0x1.e6p+7f;
  _block_3499->$4 = 0x1.47ae147ae147bp-7f;
  return _block_3499;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1212,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1190,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1192,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1208,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1202,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1196,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1194,
  float _M0L6t__nowS1188,
  float _M0L2dtS1197
) {
  int32_t _M0L6n__preS1189;
  int32_t _M0L7n__postS1191;
  float _M0L6tau__yS3169;
  float _M0L11inv__tau__yS1193;
  struct _M0TPB8MutLocalGiE* _M0L1jS1195;
  struct _M0TPB8MutLocalGiE* _M0L1iS1199;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1189 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1190);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1191 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1192);
  _M0L6tau__yS3169 = _M0L5paramS1194->$2;
  _M0L11inv__tau__yS1193 = 0x1p+0f / _M0L6tau__yS3169;
  _M0L1jS1195
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1195)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1195->$0 = 0;
  while (1) {
    int32_t _M0L3valS3086 = _M0L1jS1195->$0;
    if (_M0L3valS3086 < _M0L6n__preS1189) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3087 = _M0L4varsS1196->$0;
      int32_t _M0L3valS3088 = _M0L1jS1195->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3097 = _M0L4varsS1196->$0;
      int32_t _M0L3valS3098 = _M0L1jS1195->$0;
      float _M0L6_2atmpS3090;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3095;
      int32_t _M0L3valS3096;
      float _M0L6_2atmpS3094;
      float _M0L6_2atmpS3093;
      float _M0L6_2atmpS3092;
      float _M0L6_2atmpS3091;
      float _M0L6_2atmpS3089;
      int32_t _M0L3valS3099;
      int32_t _M0L3valS3107;
      int32_t _M0L6_2atmpS3106;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3090
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3097, _M0L3valS3098);
      _M0L4tpreS3095 = _M0L4varsS1196->$0;
      _M0L3valS3096 = _M0L1jS1195->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3094
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3095, _M0L3valS3096);
      _M0L6_2atmpS3093 = -_M0L6_2atmpS3094;
      _M0L6_2atmpS3092 = _M0L2dtS1197 * _M0L6_2atmpS3093;
      _M0L6_2atmpS3091 = _M0L6_2atmpS3092 * _M0L11inv__tau__yS1193;
      _M0L6_2atmpS3089 = _M0L6_2atmpS3090 + _M0L6_2atmpS3091;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3087, _M0L3valS3088, _M0L6_2atmpS3089);
      _M0L3valS3099 = _M0L1jS1195->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1190, _M0L3valS3099)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3100 = _M0L4varsS1196->$0;
        int32_t _M0L3valS3101 = _M0L1jS1195->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3104 = _M0L4varsS1196->$0;
        int32_t _M0L3valS3105 = _M0L1jS1195->$0;
        float _M0L6_2atmpS3103;
        float _M0L6_2atmpS3102;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS3103
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3104, _M0L3valS3105);
        _M0L6_2atmpS3102 = _M0L6_2atmpS3103 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3100, _M0L3valS3101, _M0L6_2atmpS3102);
      }
      _M0L3valS3107 = _M0L1jS1195->$0;
      _M0L6_2atmpS3106 = _M0L3valS3107 + 1;
      _M0L1jS1195->$0 = _M0L6_2atmpS3106;
      continue;
    }
    break;
  }
  _M0L1iS1199
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1199)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1199->$0 = 0;
  while (1) {
    int32_t _M0L3valS3108 = _M0L1iS1199->$0;
    if (_M0L3valS3108 < _M0L7n__postS1191) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3109 = _M0L4varsS1196->$1;
      int32_t _M0L3valS3110 = _M0L1iS1199->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3119 = _M0L4varsS1196->$1;
      int32_t _M0L3valS3120 = _M0L1iS1199->$0;
      float _M0L6_2atmpS3112;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3117;
      int32_t _M0L3valS3118;
      float _M0L6_2atmpS3116;
      float _M0L6_2atmpS3115;
      float _M0L6_2atmpS3114;
      float _M0L6_2atmpS3113;
      float _M0L6_2atmpS3111;
      int32_t _M0L3valS3121;
      int32_t _M0L3valS3129;
      int32_t _M0L6_2atmpS3128;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3112
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3119, _M0L3valS3120);
      _M0L5tpostS3117 = _M0L4varsS1196->$1;
      _M0L3valS3118 = _M0L1iS1199->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS3116
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3117, _M0L3valS3118);
      _M0L6_2atmpS3115 = -_M0L6_2atmpS3116;
      _M0L6_2atmpS3114 = _M0L2dtS1197 * _M0L6_2atmpS3115;
      _M0L6_2atmpS3113 = _M0L6_2atmpS3114 * _M0L11inv__tau__yS1193;
      _M0L6_2atmpS3111 = _M0L6_2atmpS3112 + _M0L6_2atmpS3113;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3109, _M0L3valS3110, _M0L6_2atmpS3111);
      _M0L3valS3121 = _M0L1iS1199->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1192, _M0L3valS3121)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3122 = _M0L4varsS1196->$1;
        int32_t _M0L3valS3123 = _M0L1iS1199->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3126 = _M0L4varsS1196->$1;
        int32_t _M0L3valS3127 = _M0L1iS1199->$0;
        float _M0L6_2atmpS3125;
        float _M0L6_2atmpS3124;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS3125
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3126, _M0L3valS3127);
        _M0L6_2atmpS3124 = _M0L6_2atmpS3125 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3122, _M0L3valS3123, _M0L6_2atmpS3124);
      }
      _M0L3valS3129 = _M0L1iS1199->$0;
      _M0L6_2atmpS3128 = _M0L3valS3129 + 1;
      _M0L1iS1199->$0 = _M0L6_2atmpS3128;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1199);
    }
    break;
  }
  _M0L1jS1195->$0 = 0;
  while (1) {
    int32_t _M0L3valS3130 = _M0L1jS1195->$0;
    if (_M0L3valS3130 < _M0L6n__preS1189) {
      int32_t _M0L3valS3168 = _M0L1jS1195->$0;
      int32_t _M0L5startS1201;
      int32_t _M0L3valS3167;
      int32_t _M0L6_2atmpS3166;
      int32_t _M0L3endS1203;
      int32_t _M0L3valS3165;
      int32_t _M0L10pre__firedS1204;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3163;
      int32_t _M0L3valS3164;
      float _M0L7tpre__jS1205;
      struct _M0TPB8MutLocalGiE* _M0L1sS1206;
      int32_t _M0L3valS3162;
      int32_t _M0L6_2atmpS3161;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1201
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1202, _M0L3valS3168);
      _M0L3valS3167 = _M0L1jS1195->$0;
      _M0L6_2atmpS3166 = _M0L3valS3167 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1203
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1202, _M0L6_2atmpS3166);
      _M0L3valS3165 = _M0L1jS1195->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1204
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1190, _M0L3valS3165);
      _M0L4tpreS3163 = _M0L4varsS1196->$0;
      _M0L3valS3164 = _M0L1jS1195->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1205
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3163, _M0L3valS3164);
      _M0L1sS1206
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1206)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1206->$0 = _M0L5startS1201;
      while (1) {
        int32_t _M0L3valS3131 = _M0L1sS1206->$0;
        if (_M0L3valS3131 < _M0L3endS1203) {
          int32_t _M0L3valS3160 = _M0L1sS1206->$0;
          int32_t _M0L9post__idxS1207;
          int32_t _M0L11post__firedS1209;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3159;
          float _M0L8tpost__iS1210;
          int32_t _M0L3valS3149;
          float _M0L6_2atmpS3147;
          float _M0L6w__minS3148;
          int32_t _M0L3valS3154;
          float _M0L6_2atmpS3152;
          float _M0L6w__maxS3153;
          int32_t _M0L3valS3158;
          int32_t _M0L6_2atmpS3157;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1207
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1208, _M0L3valS3160);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1209
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1192, _M0L9post__idxS1207);
          _M0L5tpostS3159 = _M0L4varsS1196->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1210
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3159, _M0L9post__idxS1207);
          if (_M0L10pre__firedS1204) {
            float _M0L3etaS3136 = _M0L5paramS1194->$0;
            float _M0L1rS3141 = _M0L5paramS1194->$1;
            float _M0L6_2atmpS3139 = 0x1p+1f * _M0L1rS3141;
            float _M0L6tau__yS3140 = _M0L5paramS1194->$2;
            float _M0L6_2atmpS3138 = _M0L6_2atmpS3139 * _M0L6tau__yS3140;
            float _M0L6_2atmpS3137 = _M0L8tpost__iS1210 - _M0L6_2atmpS3138;
            float _M0L2dwS1211 = _M0L3etaS3136 * _M0L6_2atmpS3137;
            int32_t _M0L3valS3132 = _M0L1sS1206->$0;
            int32_t _M0L3valS3135 = _M0L1sS1206->$0;
            float _M0L6_2atmpS3134;
            float _M0L6_2atmpS3133;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS3134
            = _M0MPC15array5Array2atGfE(_M0L1wS1212, _M0L3valS3135);
            _M0L6_2atmpS3133 = _M0L6_2atmpS3134 + _M0L2dwS1211;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1212, _M0L3valS3132, _M0L6_2atmpS3133);
          }
          if (_M0L11post__firedS1209) {
            float _M0L3etaS3146 = _M0L5paramS1194->$0;
            float _M0L2dwS1213 = _M0L3etaS3146 * _M0L7tpre__jS1205;
            int32_t _M0L3valS3142 = _M0L1sS1206->$0;
            int32_t _M0L3valS3145 = _M0L1sS1206->$0;
            float _M0L6_2atmpS3144;
            float _M0L6_2atmpS3143;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS3144
            = _M0MPC15array5Array2atGfE(_M0L1wS1212, _M0L3valS3145);
            _M0L6_2atmpS3143 = _M0L6_2atmpS3144 + _M0L2dwS1213;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1212, _M0L3valS3142, _M0L6_2atmpS3143);
          }
          _M0L3valS3149 = _M0L1sS1206->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS3147
          = _M0MPC15array5Array2atGfE(_M0L1wS1212, _M0L3valS3149);
          _M0L6w__minS3148 = _M0L5paramS1194->$4;
          if (_M0L6_2atmpS3147 < _M0L6w__minS3148) {
            int32_t _M0L3valS3150 = _M0L1sS1206->$0;
            float _M0L6w__minS3151 = _M0L5paramS1194->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1212, _M0L3valS3150, _M0L6w__minS3151);
          }
          _M0L3valS3154 = _M0L1sS1206->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS3152
          = _M0MPC15array5Array2atGfE(_M0L1wS1212, _M0L3valS3154);
          _M0L6w__maxS3153 = _M0L5paramS1194->$3;
          if (_M0L6_2atmpS3152 > _M0L6w__maxS3153) {
            int32_t _M0L3valS3155 = _M0L1sS1206->$0;
            float _M0L6w__maxS3156 = _M0L5paramS1194->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1212, _M0L3valS3155, _M0L6w__maxS3156);
          }
          _M0L3valS3158 = _M0L1sS1206->$0;
          _M0L6_2atmpS3157 = _M0L3valS3158 + 1;
          _M0L1sS1206->$0 = _M0L6_2atmpS3157;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1206);
        }
        break;
      }
      _M0L3valS3162 = _M0L1jS1195->$0;
      _M0L6_2atmpS3161 = _M0L3valS3162 + 1;
      _M0L1jS1195->$0 = _M0L6_2atmpS3161;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1195);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry3new(
  int32_t _M0L11conn__indexS1185,
  int32_t _M0L6n__preS1186,
  int32_t _M0L7n__postS1187,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L11param_2eoptS1183
) {
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1182;
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _result_3504;
  if (_M0L11param_2eoptS1183 == 0) {
    #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
    _M0L5paramS1182 = _M0MP26RiantR8snn__mbt9IstdpRate3new();
  } else {
    struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L7_2aSomeS1184 =
      _M0L11param_2eoptS1183;
    if (_M0L7_2aSomeS1184) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1184);
    }
    _M0L5paramS1182 = _M0L7_2aSomeS1184;
  }
  _result_3504
  = _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(_M0L11conn__indexS1185, _M0L6n__preS1186, _M0L7n__postS1187, _M0L5paramS1182);
  moonbit_decref_cycle_free(_M0L5paramS1182);
  return _result_3504;
}

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(
  int32_t _M0L11conn__indexS1178,
  int32_t _M0L6n__preS1179,
  int32_t _M0L7n__postS1180,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1181
) {
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L6_2atmpS3083;
  float* _M0L6_2atmpS3085;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3084;
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _block_3505;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3083
  = _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(_M0L6n__preS1179, _M0L7n__postS1180);
  _M0L6_2atmpS3085 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3085[0] = 0x0p+0f;
  _M0L6_2atmpS3084
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3084)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS3084->$0 = _M0L6_2atmpS3085;
  _M0L6_2atmpS3084->$1 = 1;
  moonbit_incref_cycle_free(_M0L5paramS1181);
  _block_3505
  = (struct _M0TP26RiantR8snn__mbt14IstdpRateEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry));
  Moonbit_object_header(_block_3505)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_3505->$0 = _M0L11conn__indexS1178;
  _block_3505->$1 = _M0L6n__preS1179;
  _block_3505->$2 = _M0L7n__postS1180;
  _block_3505->$3 = _M0L5paramS1181;
  _block_3505->$4 = _M0L6_2atmpS3083;
  _block_3505->$5 = _M0L6_2atmpS3084;
  return _block_3505;
}

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(
  int32_t _M0L6n__preS1176,
  int32_t _M0L7n__postS1177
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3081;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3082;
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _block_3506;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3081 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1176, 0x0p+0f);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3082 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1177, 0x0p+0f);
  _block_3506
  = (struct _M0TP26RiantR8snn__mbt18IstdpRateVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables));
  Moonbit_object_header(_block_3506)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 50, 0);
  _block_3506->$0 = _M0L6_2atmpS3081;
  _block_3506->$1 = _M0L6_2atmpS3082;
  return _block_3506;
}

struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0MP26RiantR8snn__mbt9IstdpRate3new(
  
) {
  float _M0L6_2atmpS3080;
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _block_3507;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS3080 = 0x1.8p+1f * _M0FP26RiantR8snn__mbt2hz;
  _block_3507
  = (struct _M0TP26RiantR8snn__mbt9IstdpRate*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9IstdpRate));
  Moonbit_object_header(_block_3507)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3507->$0 = 0x1.47ae147ae147bp-7f;
  _block_3507->$1 = _M0L6_2atmpS3080;
  _block_3507->$2 = 0x1.9p+5f;
  _block_3507->$3 = 0x1.e6p+7f;
  _block_3507->$4 = 0x1.47ae147ae147bp-7f;
  return _block_3507;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS1171,
  float _M0L2vtS1172,
  float _M0L2vrS1173,
  float _M0L2elS1174,
  float _M0L1rS1175
) {
  float _M0L1cS1169;
  float _M0L2glS1170;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_3508;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1169 = -0x1p+0f;
  _M0L2glS1170 = -0x1p+0f;
  _block_3508
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_3508)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3508->$0 = _M0L1cS1169;
  _block_3508->$1 = _M0L2glS1170;
  _block_3508->$2 = _M0L2tmS1171;
  _block_3508->$3 = _M0L2vtS1172;
  _block_3508->$4 = _M0L2vrS1173;
  _block_3508->$5 = _M0L2elS1174;
  _block_3508->$6 = _M0L1rS1175;
  _block_3508->$7 = 0x1p+1f;
  _block_3508->$8 = 0x0p+0f;
  _block_3508->$9 = 0x0p+0f;
  _block_3508->$10 = 0x0p+0f;
  return _block_3508;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1143,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1145,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1148
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1142;
  float _M0L2vtS3078;
  float _M0L2vrS3079;
  float _M0L6spreadS1144;
  int32_t _M0L7_2abindS1146;
  int32_t _M0L1kS1147;
  struct _M0TPB5ArrayGfE* _M0L1wS1150;
  struct _M0TPB5ArrayGbE* _M0L4fireS1151;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1152;
  struct _M0TPB5ArrayGfE* _M0L1iS1153;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1154;
  struct _M0TPB5ArrayGfE* _M0L2geS1155;
  struct _M0TPB5ArrayGfE* _M0L2giS1156;
  struct _M0TPB5ArrayGfE* _M0L2heS1157;
  struct _M0TPB5ArrayGfE* _M0L2hiS1158;
  struct _M0TPB5ArrayGfE* _M0L3gluS1159;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1160;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1161;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1162;
  float _M0L4e__eS1163;
  float _M0L4e__iS1164;
  float _M0L3treS1165;
  float _M0L3tdeS1166;
  float _M0L3triS1167;
  float _M0L3tdiS1168;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS3077;
  struct _M0TP26RiantR8snn__mbt2IF* _block_3510;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1142 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  _M0L2vtS3078 = _M0L5paramS1145->$3;
  _M0L2vrS3079 = _M0L5paramS1145->$4;
  _M0L6spreadS1144 = _M0L2vtS3078 - _M0L2vrS3079;
  _M0L7_2abindS1146 = 0;
  _M0L1kS1147 = _M0L7_2abindS1146;
  while (1) {
    if (_M0L1kS1147 < _M0L1nS1143) {
      float _M0L2vrS3073 = _M0L5paramS1145->$4;
      float _M0L6_2atmpS3075;
      float _M0L6_2atmpS3074;
      float _M0L6_2atmpS3072;
      int32_t _M0L6_2atmpS3076;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3075 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1148);
      _M0L6_2atmpS3074 = _M0L6_2atmpS3075 * _M0L6spreadS1144;
      _M0L6_2atmpS3072 = _M0L2vrS3073 + _M0L6_2atmpS3074;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1142, _M0L1kS1147, _M0L6_2atmpS3072);
      _M0L6_2atmpS3076 = _M0L1kS1147 + 1;
      _M0L1kS1147 = _M0L6_2atmpS3076;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1150 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1151 = _M0MPC15array5Array4makeGbE(_M0L1nS1143, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1152 = _M0MPC15array5Array4makeGiE(_M0L1nS1143, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1153 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1154 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1155 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1156 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1157 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1158 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1159 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1160 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1161 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1162 = _M0MPC15array5Array4makeGfE(_M0L1nS1143, 0x1p+0f);
  _M0L4e__eS1163 = 0x0p+0f;
  _M0L4e__iS1164 = -0x1.2cp+6f;
  _M0L3treS1165 = 0x1p+0f;
  _M0L3tdeS1166 = 0x1.8p+2f;
  _M0L3triS1167 = 0x1p-1f;
  _M0L3tdiS1168 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS3077 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1145);
  _block_3510
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_3510)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_3510->$0 = _M0L5paramS1145;
  _block_3510->$1 = _M0L6_2atmpS3077;
  _block_3510->$2 = _M0L1nS1143;
  _block_3510->$3 = _M0L1vS1142;
  _block_3510->$4 = _M0L1wS1150;
  _block_3510->$5 = _M0L4fireS1151;
  _block_3510->$6 = _M0L4tabsS1152;
  _block_3510->$7 = _M0L1iS1153;
  _block_3510->$8 = _M0L9syn__currS1154;
  _block_3510->$9 = _M0L2geS1155;
  _block_3510->$10 = _M0L2giS1156;
  _block_3510->$11 = _M0L2heS1157;
  _block_3510->$12 = _M0L2hiS1158;
  _block_3510->$13 = _M0L3gluS1159;
  _block_3510->$14 = _M0L4gabaS1160;
  _block_3510->$15 = _M0L7gsyn__eS1161;
  _block_3510->$16 = _M0L7gsyn__iS1162;
  _block_3510->$17 = _M0L4e__eS1163;
  _block_3510->$18 = _M0L4e__iS1164;
  _block_3510->$19 = _M0L3treS1165;
  _block_3510->$20 = _M0L3tdeS1166;
  _block_3510->$21 = _M0L3triS1167;
  _block_3510->$22 = _M0L3tdiS1168;
  return _block_3510;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_3511;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_3511
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_3511)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3511->$0 = 0x1p+1f;
  return _block_3511;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS1138;
  float _M0L2glS1139;
  float _M0L2tmS1140;
  float _M0L1rS1141;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_3512;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS1138 = 0x1.19p+8f;
  _M0L2glS1139 = 0x1.4p+5f;
  _M0L2tmS1140 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS1141 = 0x1p+0f / 0x1.4p+5f;
  _block_3512
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_3512)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3512->$0 = _M0L1cS1138;
  _block_3512->$1 = _M0L2glS1139;
  _block_3512->$2 = -0x1.9p+5f;
  _block_3512->$3 = -0x1.1a66666666666p+6f;
  _block_3512->$4 = -0x1.1a66666666666p+6f;
  _block_3512->$5 = _M0L2tmS1140;
  _block_3512->$6 = _M0L1rS1141;
  _block_3512->$7 = 0x1p+1f;
  _block_3512->$8 = 0x1.2p+7f;
  _block_3512->$9 = 0x1p+2f;
  _block_3512->$10 = 0x1.42p+6f;
  return _block_3512;
}

int32_t _M0FP26RiantR8snn__mbt12step__tripod(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1117,
  float _M0L2dtS1128
) {
  int32_t _M0L1nS1116;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1118;
  float _M0L2vtS1119;
  float _M0L2vrS1120;
  float _M0L1bS1121;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3071;
  float _M0L2atS1122;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3070;
  float _M0L6tau__aS1123;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3069;
  float _M0L11tabs__constS1124;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3068;
  float _M0L2upS1125;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3067;
  float _M0L12ap__membraneS1126;
  float _M0L6_2atmpS3066;
  float _M0L6_2atmpS3065;
  int32_t _M0L11tabs__stepsS1127;
  int32_t _M0L7_2abindS1129;
  int32_t _M0L7_2abindS1130;
  int32_t _M0L1iS1131;
  int32_t _M0L7_2abindS1133;
  int32_t _M0L1kS1134;
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1116 = _M0L1pS1117->$25;
  _M0L3p__S1118 = _M0L1pS1117->$0;
  _M0L2vtS1119 = _M0L3p__S1118->$2;
  _M0L2vrS1120 = _M0L3p__S1118->$3;
  _M0L1bS1121 = _M0L3p__S1118->$10;
  _M0L11soma__spikeS3071 = _M0L1pS1117->$1;
  _M0L2atS1122 = _M0L11soma__spikeS3071->$0;
  _M0L11soma__spikeS3070 = _M0L1pS1117->$1;
  _M0L6tau__aS1123 = _M0L11soma__spikeS3070->$1;
  _M0L11soma__spikeS3069 = _M0L1pS1117->$1;
  _M0L11tabs__constS1124 = _M0L11soma__spikeS3069->$3;
  _M0L11soma__spikeS3068 = _M0L1pS1117->$1;
  _M0L2upS1125 = _M0L11soma__spikeS3068->$4;
  _M0L11soma__spikeS3067 = _M0L1pS1117->$1;
  _M0L12ap__membraneS1126 = _M0L11soma__spikeS3067->$2;
  _M0L6_2atmpS3066 = _M0L2upS1125 + _M0L11tabs__constS1124;
  _M0L6_2atmpS3065 = _M0L6_2atmpS3066 / _M0L2dtS1128;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L11tabs__stepsS1127 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3065);
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(_M0L1pS1117, _M0L2dtS1128);
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(_M0L1pS1117, _M0L2dtS1128);
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(_M0L1pS1117);
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(_M0L1pS1117);
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS1117, _M0L2dtS1128, 0);
  _M0L7_2abindS1129 = 0;
  _M0L7_2abindS1130 = _M0L1nS1116 * 4;
  _M0L1iS1131 = _M0L7_2abindS1129;
  while (1) {
    if (_M0L1iS1131 < _M0L7_2abindS1130) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2926 = _M0L1pS1117->$34;
      struct _M0TPB5ArrayGfE* _M0L2dvS2928 = _M0L1pS1117->$33;
      float _M0L6_2atmpS2927;
      int32_t _M0L6_2atmpS2929;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2927 = _M0MPC15array5Array2atGfE(_M0L2dvS2928, _M0L1iS1131);
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2926, _M0L1iS1131, _M0L6_2atmpS2927);
      _M0L6_2atmpS2929 = _M0L1iS1131 + 1;
      _M0L1iS1131 = _M0L6_2atmpS2929;
      continue;
    }
    break;
  }
  #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS1117, _M0L2dtS1128, 1);
  _M0L7_2abindS1133 = 0;
  _M0L1kS1134 = _M0L7_2abindS1133;
  while (1) {
    if (_M0L1kS1134 < _M0L1nS1116) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2931 = _M0L1pS1117->$32;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2934 = _M0L1pS1117->$32;
      int32_t _M0L6_2atmpS2933;
      int32_t _M0L6_2atmpS2932;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2935;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2943;
      float _M0L6_2atmpS2937;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2942;
      float _M0L6_2atmpS2941;
      float _M0L6_2atmpS2940;
      float _M0L6_2atmpS2939;
      float _M0L6_2atmpS2938;
      float _M0L6_2atmpS2936;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2945;
      int32_t _M0L6_2atmpS2944;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3064;
      float _M0L6_2atmpS3059;
      struct _M0TPB5ArrayGfE* _M0L2dvS3062;
      int32_t _M0L6_2atmpS3063;
      float _M0L6_2atmpS3061;
      float _M0L6_2atmpS3060;
      float _M0L10v__s__predS1137;
      struct _M0TPB5ArrayGbE* _M0L4fireS2983;
      int32_t _M0L6_2atmpS2984;
      struct _M0TPB5ArrayGbE* _M0L4fireS2985;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3001;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3013;
      float _M0L6_2atmpS3003;
      float _M0L6_2atmpS3005;
      struct _M0TPB5ArrayGfE* _M0L2dvS3011;
      int32_t _M0L6_2atmpS3012;
      float _M0L6_2atmpS3007;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3009;
      int32_t _M0L6_2atmpS3010;
      float _M0L6_2atmpS3008;
      float _M0L6_2atmpS3006;
      float _M0L6_2atmpS3004;
      float _M0L6_2atmpS3002;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S3014;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S3028;
      float _M0L6_2atmpS3016;
      float _M0L6_2atmpS3018;
      struct _M0TPB5ArrayGfE* _M0L2dvS3025;
      int32_t _M0L6_2atmpS3027;
      int32_t _M0L6_2atmpS3026;
      float _M0L6_2atmpS3020;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3022;
      int32_t _M0L6_2atmpS3024;
      int32_t _M0L6_2atmpS3023;
      float _M0L6_2atmpS3021;
      float _M0L6_2atmpS3019;
      float _M0L6_2atmpS3017;
      float _M0L6_2atmpS3015;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S3029;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S3043;
      float _M0L6_2atmpS3031;
      float _M0L6_2atmpS3033;
      struct _M0TPB5ArrayGfE* _M0L2dvS3040;
      int32_t _M0L6_2atmpS3042;
      int32_t _M0L6_2atmpS3041;
      float _M0L6_2atmpS3035;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3037;
      int32_t _M0L6_2atmpS3039;
      int32_t _M0L6_2atmpS3038;
      float _M0L6_2atmpS3036;
      float _M0L6_2atmpS3034;
      float _M0L6_2atmpS3032;
      float _M0L6_2atmpS3030;
      struct _M0TPB5ArrayGfE* _M0L4w__sS3044;
      struct _M0TPB5ArrayGfE* _M0L4w__sS3058;
      float _M0L6_2atmpS3046;
      float _M0L6_2atmpS3048;
      struct _M0TPB5ArrayGfE* _M0L2dvS3055;
      int32_t _M0L6_2atmpS3057;
      int32_t _M0L6_2atmpS3056;
      float _M0L6_2atmpS3050;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3052;
      int32_t _M0L6_2atmpS3054;
      int32_t _M0L6_2atmpS3053;
      float _M0L6_2atmpS3051;
      float _M0L6_2atmpS3049;
      float _M0L6_2atmpS3047;
      float _M0L6_2atmpS3045;
      int32_t _M0L6_2atmpS2930;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2933
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2934, _M0L1kS1134);
      _M0L6_2atmpS2932 = _M0L6_2atmpS2933 - 1;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2931, _M0L1kS1134, _M0L6_2atmpS2932);
      _M0L9thresholdS2935 = _M0L1pS1117->$31;
      _M0L9thresholdS2943 = _M0L1pS1117->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2937
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2943, _M0L1kS1134);
      _M0L9thresholdS2942 = _M0L1pS1117->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2941
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2942, _M0L1kS1134);
      _M0L6_2atmpS2940 = _M0L2vtS1119 - _M0L6_2atmpS2941;
      _M0L6_2atmpS2939 = _M0L2dtS1128 * _M0L6_2atmpS2940;
      _M0L6_2atmpS2938 = _M0L6_2atmpS2939 / _M0L6tau__aS1123;
      _M0L6_2atmpS2936 = _M0L6_2atmpS2937 + _M0L6_2atmpS2938;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2935, _M0L1kS1134, _M0L6_2atmpS2936);
      _M0L4tabsS2945 = _M0L1pS1117->$32;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2944
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2945, _M0L1kS1134);
      if (_M0L6_2atmpS2944 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2946 = _M0L1pS1117->$26;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2947;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2964;
        float _M0L6_2atmpS2949;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2963;
        float _M0L6_2atmpS2960;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2962;
        float _M0L6_2atmpS2961;
        float _M0L6_2atmpS2959;
        float _M0L6_2atmpS2955;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2958;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2957;
        float _M0L6_2atmpS2956;
        float _M0L6_2atmpS2951;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2954;
        struct _M0TPB5ArrayGfE* _M0L1cS2953;
        float _M0L6_2atmpS2952;
        float _M0L6_2atmpS2950;
        float _M0L6_2atmpS2948;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2965;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2982;
        float _M0L6_2atmpS2967;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2981;
        float _M0L6_2atmpS2978;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2980;
        float _M0L6_2atmpS2979;
        float _M0L6_2atmpS2977;
        float _M0L6_2atmpS2973;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2976;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2975;
        float _M0L6_2atmpS2974;
        float _M0L6_2atmpS2969;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2972;
        struct _M0TPB5ArrayGfE* _M0L1cS2971;
        float _M0L6_2atmpS2970;
        float _M0L6_2atmpS2968;
        float _M0L6_2atmpS2966;
        #line 324 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2946, _M0L1kS1134, _M0L2vrS1120);
        _M0L5v__d1S2947 = _M0L1pS1117->$28;
        _M0L5v__d1S2964 = _M0L1pS1117->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2949
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2964, _M0L1kS1134);
        _M0L4v__sS2963 = _M0L1pS1117->$26;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2960
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2963, _M0L1kS1134);
        _M0L5v__d1S2962 = _M0L1pS1117->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2961
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2962, _M0L1kS1134);
        _M0L6_2atmpS2959 = _M0L6_2atmpS2960 - _M0L6_2atmpS2961;
        _M0L6_2atmpS2955 = _M0L2dtS1128 * _M0L6_2atmpS2959;
        _M0L2d1S2958 = _M0L1pS1117->$2;
        _M0L3gaxS2957 = _M0L2d1S2958->$3;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2956
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2957, _M0L1kS1134);
        _M0L6_2atmpS2951 = _M0L6_2atmpS2955 * _M0L6_2atmpS2956;
        _M0L2d1S2954 = _M0L1pS1117->$2;
        _M0L1cS2953 = _M0L2d1S2954->$2;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2952
        = _M0MPC15array5Array2atGfE(_M0L1cS2953, _M0L1kS1134);
        _M0L6_2atmpS2950 = _M0L6_2atmpS2951 / _M0L6_2atmpS2952;
        _M0L6_2atmpS2948 = _M0L6_2atmpS2949 + _M0L6_2atmpS2950;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2947, _M0L1kS1134, _M0L6_2atmpS2948);
        _M0L5v__d2S2965 = _M0L1pS1117->$29;
        _M0L5v__d2S2982 = _M0L1pS1117->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2967
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2982, _M0L1kS1134);
        _M0L4v__sS2981 = _M0L1pS1117->$26;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2978
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2981, _M0L1kS1134);
        _M0L5v__d2S2980 = _M0L1pS1117->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2979
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2980, _M0L1kS1134);
        _M0L6_2atmpS2977 = _M0L6_2atmpS2978 - _M0L6_2atmpS2979;
        _M0L6_2atmpS2973 = _M0L2dtS1128 * _M0L6_2atmpS2977;
        _M0L2d2S2976 = _M0L1pS1117->$3;
        _M0L3gaxS2975 = _M0L2d2S2976->$3;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2974
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2975, _M0L1kS1134);
        _M0L6_2atmpS2969 = _M0L6_2atmpS2973 * _M0L6_2atmpS2974;
        _M0L2d2S2972 = _M0L1pS1117->$3;
        _M0L1cS2971 = _M0L2d2S2972->$2;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2970
        = _M0MPC15array5Array2atGfE(_M0L1cS2971, _M0L1kS1134);
        _M0L6_2atmpS2968 = _M0L6_2atmpS2969 / _M0L6_2atmpS2970;
        _M0L6_2atmpS2966 = _M0L6_2atmpS2967 + _M0L6_2atmpS2968;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2965, _M0L1kS1134, _M0L6_2atmpS2966);
        goto join_1135;
      }
      _M0L4v__sS3064 = _M0L1pS1117->$26;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3059
      = _M0MPC15array5Array2atGfE(_M0L4v__sS3064, _M0L1kS1134);
      _M0L2dvS3062 = _M0L1pS1117->$33;
      _M0L6_2atmpS3063 = _M0L1kS1134 * 4;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3061
      = _M0MPC15array5Array2atGfE(_M0L2dvS3062, _M0L6_2atmpS3063);
      _M0L6_2atmpS3060 = _M0L6_2atmpS3061 * _M0L2dtS1128;
      _M0L10v__s__predS1137 = _M0L6_2atmpS3059 + _M0L6_2atmpS3060;
      _M0L4fireS2983 = _M0L1pS1117->$30;
      _M0L6_2atmpS2984 = _M0L10v__s__predS1137 >= -0x1.4p+3f;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2983, _M0L1kS1134, _M0L6_2atmpS2984);
      _M0L4fireS2985 = _M0L1pS1117->$30;
      #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2985, _M0L1kS1134)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2986 = _M0L1pS1117->$33;
        int32_t _M0L6_2atmpS2987 = _M0L1kS1134 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2990 = _M0L1pS1117->$26;
        float _M0L6_2atmpS2989;
        float _M0L6_2atmpS2988;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2991;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2992;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2995;
        float _M0L6_2atmpS2994;
        float _M0L6_2atmpS2993;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2996;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2999;
        float _M0L6_2atmpS2998;
        float _M0L6_2atmpS2997;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3000;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2989
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2990, _M0L1kS1134);
        _M0L6_2atmpS2988 = _M0L12ap__membraneS1126 - _M0L6_2atmpS2989;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2986, _M0L6_2atmpS2987, _M0L6_2atmpS2988);
        _M0L4v__sS2991 = _M0L1pS1117->$26;
        #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2991, _M0L1kS1134, _M0L12ap__membraneS1126);
        _M0L4w__sS2992 = _M0L1pS1117->$27;
        _M0L4w__sS2995 = _M0L1pS1117->$27;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2994
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2995, _M0L1kS1134);
        _M0L6_2atmpS2993 = _M0L6_2atmpS2994 + _M0L1bS1121;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2992, _M0L1kS1134, _M0L6_2atmpS2993);
        _M0L9thresholdS2996 = _M0L1pS1117->$31;
        _M0L9thresholdS2999 = _M0L1pS1117->$31;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2998
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2999, _M0L1kS1134);
        _M0L6_2atmpS2997 = _M0L6_2atmpS2998 + _M0L2atS1122;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2996, _M0L1kS1134, _M0L6_2atmpS2997);
        _M0L4tabsS3000 = _M0L1pS1117->$32;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS3000, _M0L1kS1134, _M0L11tabs__stepsS1127);
        goto join_1135;
      }
      _M0L4v__sS3001 = _M0L1pS1117->$26;
      _M0L4v__sS3013 = _M0L1pS1117->$26;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3003
      = _M0MPC15array5Array2atGfE(_M0L4v__sS3013, _M0L1kS1134);
      _M0L6_2atmpS3005 = 0x1p-1f * _M0L2dtS1128;
      _M0L2dvS3011 = _M0L1pS1117->$33;
      _M0L6_2atmpS3012 = _M0L1kS1134 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3007
      = _M0MPC15array5Array2atGfE(_M0L2dvS3011, _M0L6_2atmpS3012);
      _M0L8dv__tempS3009 = _M0L1pS1117->$34;
      _M0L6_2atmpS3010 = _M0L1kS1134 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3008
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3009, _M0L6_2atmpS3010);
      _M0L6_2atmpS3006 = _M0L6_2atmpS3007 + _M0L6_2atmpS3008;
      _M0L6_2atmpS3004 = _M0L6_2atmpS3005 * _M0L6_2atmpS3006;
      _M0L6_2atmpS3002 = _M0L6_2atmpS3003 + _M0L6_2atmpS3004;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS3001, _M0L1kS1134, _M0L6_2atmpS3002);
      _M0L5v__d1S3014 = _M0L1pS1117->$28;
      _M0L5v__d1S3028 = _M0L1pS1117->$28;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3016
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S3028, _M0L1kS1134);
      _M0L6_2atmpS3018 = 0x1p-1f * _M0L2dtS1128;
      _M0L2dvS3025 = _M0L1pS1117->$33;
      _M0L6_2atmpS3027 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3026 = _M0L6_2atmpS3027 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3020
      = _M0MPC15array5Array2atGfE(_M0L2dvS3025, _M0L6_2atmpS3026);
      _M0L8dv__tempS3022 = _M0L1pS1117->$34;
      _M0L6_2atmpS3024 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3023 = _M0L6_2atmpS3024 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3021
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3022, _M0L6_2atmpS3023);
      _M0L6_2atmpS3019 = _M0L6_2atmpS3020 + _M0L6_2atmpS3021;
      _M0L6_2atmpS3017 = _M0L6_2atmpS3018 * _M0L6_2atmpS3019;
      _M0L6_2atmpS3015 = _M0L6_2atmpS3016 + _M0L6_2atmpS3017;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S3014, _M0L1kS1134, _M0L6_2atmpS3015);
      _M0L5v__d2S3029 = _M0L1pS1117->$29;
      _M0L5v__d2S3043 = _M0L1pS1117->$29;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3031
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S3043, _M0L1kS1134);
      _M0L6_2atmpS3033 = 0x1p-1f * _M0L2dtS1128;
      _M0L2dvS3040 = _M0L1pS1117->$33;
      _M0L6_2atmpS3042 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3041 = _M0L6_2atmpS3042 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3035
      = _M0MPC15array5Array2atGfE(_M0L2dvS3040, _M0L6_2atmpS3041);
      _M0L8dv__tempS3037 = _M0L1pS1117->$34;
      _M0L6_2atmpS3039 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3038 = _M0L6_2atmpS3039 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3036
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3037, _M0L6_2atmpS3038);
      _M0L6_2atmpS3034 = _M0L6_2atmpS3035 + _M0L6_2atmpS3036;
      _M0L6_2atmpS3032 = _M0L6_2atmpS3033 * _M0L6_2atmpS3034;
      _M0L6_2atmpS3030 = _M0L6_2atmpS3031 + _M0L6_2atmpS3032;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S3029, _M0L1kS1134, _M0L6_2atmpS3030);
      _M0L4w__sS3044 = _M0L1pS1117->$27;
      _M0L4w__sS3058 = _M0L1pS1117->$27;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3046
      = _M0MPC15array5Array2atGfE(_M0L4w__sS3058, _M0L1kS1134);
      _M0L6_2atmpS3048 = 0x1p-1f * _M0L2dtS1128;
      _M0L2dvS3055 = _M0L1pS1117->$33;
      _M0L6_2atmpS3057 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3056 = _M0L6_2atmpS3057 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3050
      = _M0MPC15array5Array2atGfE(_M0L2dvS3055, _M0L6_2atmpS3056);
      _M0L8dv__tempS3052 = _M0L1pS1117->$34;
      _M0L6_2atmpS3054 = _M0L1kS1134 * 4;
      _M0L6_2atmpS3053 = _M0L6_2atmpS3054 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS3051
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3052, _M0L6_2atmpS3053);
      _M0L6_2atmpS3049 = _M0L6_2atmpS3050 + _M0L6_2atmpS3051;
      _M0L6_2atmpS3047 = _M0L6_2atmpS3048 * _M0L6_2atmpS3049;
      _M0L6_2atmpS3045 = _M0L6_2atmpS3046 + _M0L6_2atmpS3047;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS3044, _M0L1kS1134, _M0L6_2atmpS3045);
      goto join_1135;
      goto joinlet_3515;
      join_1135:;
      _M0L6_2atmpS2930 = _M0L1kS1134 + 1;
      _M0L1kS1134 = _M0L6_2atmpS2930;
      continue;
      joinlet_3515:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18tripod__heun__step(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1093,
  float _M0L2dtS1104,
  int32_t _M0L11store__tempS1103
) {
  int32_t _M0L1nS1092;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1094;
  float _M0L1cS1095;
  float _M0L2glS1096;
  float _M0L2elS1097;
  float _M0L9dt__slopeS1098;
  float _M0L2twS1099;
  float _M0L1aS1100;
  struct _M0TPB8MutLocalGiE* _M0L1kS1101;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1092 = _M0L1pS1093->$25;
  _M0L3p__S1094 = _M0L1pS1093->$0;
  _M0L1cS1095 = _M0L3p__S1094->$0;
  _M0L2glS1096 = _M0L3p__S1094->$1;
  _M0L2elS1097 = _M0L3p__S1094->$4;
  _M0L9dt__slopeS1098 = _M0L3p__S1094->$7;
  _M0L2twS1099 = _M0L3p__S1094->$8;
  _M0L1aS1100 = _M0L3p__S1094->$9;
  _M0L1kS1101
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1101)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1101->$0 = 0;
  while (1) {
    int32_t _M0L3valS2758 = _M0L1kS1101->$0;
    if (_M0L3valS2758 < _M0L1nS1092) {
      float _M0L2dsS1102;
      float _M0L3dd1S1105;
      float _M0L3dd2S1106;
      float _M0L2dwS1107;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2906;
      int32_t _M0L3valS2907;
      float _M0L6_2atmpS2905;
      float _M0L6_2atmpS2901;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2903;
      int32_t _M0L3valS2904;
      float _M0L6_2atmpS2902;
      float _M0L6_2atmpS2900;
      float _M0L6_2atmpS2899;
      float _M0L6_2atmpS2894;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2898;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2896;
      int32_t _M0L3valS2897;
      float _M0L6_2atmpS2895;
      float _M0L3ic1S1108;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2892;
      int32_t _M0L3valS2893;
      float _M0L6_2atmpS2891;
      float _M0L6_2atmpS2887;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2889;
      int32_t _M0L3valS2890;
      float _M0L6_2atmpS2888;
      float _M0L6_2atmpS2886;
      float _M0L6_2atmpS2885;
      float _M0L6_2atmpS2880;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2884;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2882;
      int32_t _M0L3valS2883;
      float _M0L6_2atmpS2881;
      float _M0L3ic2S1109;
      float _M0L9exp__termS1110;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2868;
      int32_t _M0L3valS2869;
      float _M0L6_2atmpS2867;
      float _M0L6_2atmpS2866;
      float _M0L6_2atmpS2865;
      float _M0L6_2atmpS2864;
      float _M0L6_2atmpS2860;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2862;
      int32_t _M0L3valS2863;
      float _M0L6_2atmpS2861;
      float _M0L6_2atmpS2859;
      float _M0L6_2atmpS2855;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2857;
      int32_t _M0L3valS2858;
      float _M0L6_2atmpS2856;
      float _M0L6_2atmpS2853;
      float _M0L6_2atmpS2854;
      float _M0L6_2atmpS2849;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2851;
      int32_t _M0L3valS2852;
      float _M0L6_2atmpS2850;
      float _M0L6_2atmpS2848;
      float _M0L10dv__s__valS1111;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2846;
      int32_t _M0L3valS2847;
      float _M0L6_2atmpS2845;
      float _M0L6_2atmpS2844;
      float _M0L6_2atmpS2839;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2843;
      struct _M0TPB5ArrayGfE* _M0L2gmS2841;
      int32_t _M0L3valS2842;
      float _M0L6_2atmpS2840;
      float _M0L6_2atmpS2835;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2837;
      int32_t _M0L3valS2838;
      float _M0L6_2atmpS2836;
      float _M0L6_2atmpS2834;
      float _M0L6_2atmpS2830;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2832;
      int32_t _M0L3valS2833;
      float _M0L6_2atmpS2831;
      float _M0L6_2atmpS2825;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2829;
      struct _M0TPB5ArrayGfE* _M0L1cS2827;
      int32_t _M0L3valS2828;
      float _M0L6_2atmpS2826;
      float _M0L11dv__d1__valS1112;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2823;
      int32_t _M0L3valS2824;
      float _M0L6_2atmpS2822;
      float _M0L6_2atmpS2821;
      float _M0L6_2atmpS2816;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2820;
      struct _M0TPB5ArrayGfE* _M0L2gmS2818;
      int32_t _M0L3valS2819;
      float _M0L6_2atmpS2817;
      float _M0L6_2atmpS2812;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2814;
      int32_t _M0L3valS2815;
      float _M0L6_2atmpS2813;
      float _M0L6_2atmpS2811;
      float _M0L6_2atmpS2807;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2809;
      int32_t _M0L3valS2810;
      float _M0L6_2atmpS2808;
      float _M0L6_2atmpS2802;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2806;
      struct _M0TPB5ArrayGfE* _M0L1cS2804;
      int32_t _M0L3valS2805;
      float _M0L6_2atmpS2803;
      float _M0L11dv__d2__valS1113;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2800;
      int32_t _M0L3valS2801;
      float _M0L6_2atmpS2799;
      float _M0L6_2atmpS2798;
      float _M0L6_2atmpS2797;
      float _M0L6_2atmpS2792;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2795;
      int32_t _M0L3valS2796;
      float _M0L6_2atmpS2794;
      float _M0L6_2atmpS2793;
      float _M0L6_2atmpS2791;
      float _M0L7dw__valS1114;
      int32_t _M0L3valS2790;
      int32_t _M0L6_2atmpS2789;
      if (_M0L11store__tempS1103) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2923 = _M0L1pS1093->$34;
        int32_t _M0L3valS2925 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2924 = _M0L3valS2925 * 4;
        float _M0L6_2atmpS2922;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2922
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2923, _M0L6_2atmpS2924);
        _M0L2dsS1102 = _M0L6_2atmpS2922 * _M0L2dtS1104;
      } else {
        _M0L2dsS1102 = 0x0p+0f;
      }
      if (_M0L11store__tempS1103) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2918 = _M0L1pS1093->$34;
        int32_t _M0L3valS2921 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2920 = _M0L3valS2921 * 4;
        int32_t _M0L6_2atmpS2919 = _M0L6_2atmpS2920 + 1;
        float _M0L6_2atmpS2917;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2917
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2918, _M0L6_2atmpS2919);
        _M0L3dd1S1105 = _M0L6_2atmpS2917 * _M0L2dtS1104;
      } else {
        _M0L3dd1S1105 = 0x0p+0f;
      }
      if (_M0L11store__tempS1103) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2913 = _M0L1pS1093->$34;
        int32_t _M0L3valS2916 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2915 = _M0L3valS2916 * 4;
        int32_t _M0L6_2atmpS2914 = _M0L6_2atmpS2915 + 2;
        float _M0L6_2atmpS2912;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2912
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2913, _M0L6_2atmpS2914);
        _M0L3dd2S1106 = _M0L6_2atmpS2912 * _M0L2dtS1104;
      } else {
        _M0L3dd2S1106 = 0x0p+0f;
      }
      if (_M0L11store__tempS1103) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2908 = _M0L1pS1093->$34;
        int32_t _M0L3valS2911 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2910 = _M0L3valS2911 * 4;
        int32_t _M0L6_2atmpS2909 = _M0L6_2atmpS2910 + 3;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L2dwS1107
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2908, _M0L6_2atmpS2909);
      } else {
        _M0L2dwS1107 = 0x0p+0f;
      }
      _M0L5v__d1S2906 = _M0L1pS1093->$28;
      _M0L3valS2907 = _M0L1kS1101->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2905
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2906, _M0L3valS2907);
      _M0L6_2atmpS2901 = _M0L6_2atmpS2905 + _M0L3dd1S1105;
      _M0L4v__sS2903 = _M0L1pS1093->$26;
      _M0L3valS2904 = _M0L1kS1101->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2902
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2903, _M0L3valS2904);
      _M0L6_2atmpS2900 = _M0L6_2atmpS2901 - _M0L6_2atmpS2902;
      _M0L6_2atmpS2899 = _M0L6_2atmpS2900 - _M0L2dsS1102;
      _M0L6_2atmpS2894 = -_M0L6_2atmpS2899;
      _M0L2d1S2898 = _M0L1pS1093->$2;
      _M0L3gaxS2896 = _M0L2d1S2898->$3;
      _M0L3valS2897 = _M0L1kS1101->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2895
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2896, _M0L3valS2897);
      _M0L3ic1S1108 = _M0L6_2atmpS2894 * _M0L6_2atmpS2895;
      _M0L5v__d2S2892 = _M0L1pS1093->$29;
      _M0L3valS2893 = _M0L1kS1101->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2891
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2892, _M0L3valS2893);
      _M0L6_2atmpS2887 = _M0L6_2atmpS2891 + _M0L3dd2S1106;
      _M0L4v__sS2889 = _M0L1pS1093->$26;
      _M0L3valS2890 = _M0L1kS1101->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2888
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2889, _M0L3valS2890);
      _M0L6_2atmpS2886 = _M0L6_2atmpS2887 - _M0L6_2atmpS2888;
      _M0L6_2atmpS2885 = _M0L6_2atmpS2886 - _M0L2dsS1102;
      _M0L6_2atmpS2880 = -_M0L6_2atmpS2885;
      _M0L2d2S2884 = _M0L1pS1093->$3;
      _M0L3gaxS2882 = _M0L2d2S2884->$3;
      _M0L3valS2883 = _M0L1kS1101->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2881
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2882, _M0L3valS2883);
      _M0L3ic2S1109 = _M0L6_2atmpS2880 * _M0L6_2atmpS2881;
      if (_M0L9dt__slopeS1098 < 0x0p+0f) {
        _M0L9exp__termS1110 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2878 = _M0L1pS1093->$26;
        int32_t _M0L3valS2879 = _M0L1kS1101->$0;
        float _M0L6_2atmpS2877;
        float _M0L6_2atmpS2873;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2875;
        int32_t _M0L3valS2876;
        float _M0L6_2atmpS2874;
        float _M0L6_2atmpS2872;
        float _M0L6_2atmpS2871;
        float _M0L6_2atmpS2870;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2877
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2878, _M0L3valS2879);
        _M0L6_2atmpS2873 = _M0L6_2atmpS2877 + _M0L2dsS1102;
        _M0L9thresholdS2875 = _M0L1pS1093->$31;
        _M0L3valS2876 = _M0L1kS1101->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2874
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2875, _M0L3valS2876);
        _M0L6_2atmpS2872 = _M0L6_2atmpS2873 - _M0L6_2atmpS2874;
        _M0L6_2atmpS2871 = _M0L6_2atmpS2872 / _M0L9dt__slopeS1098;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2870 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2871);
        _M0L9exp__termS1110 = _M0L9dt__slopeS1098 * _M0L6_2atmpS2870;
      }
      _M0L4v__sS2868 = _M0L1pS1093->$26;
      _M0L3valS2869 = _M0L1kS1101->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2867
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2868, _M0L3valS2869);
      _M0L6_2atmpS2866 = _M0L2elS1097 - _M0L6_2atmpS2867;
      _M0L6_2atmpS2865 = _M0L6_2atmpS2866 - _M0L2dsS1102;
      _M0L6_2atmpS2864 = _M0L2glS1096 * _M0L6_2atmpS2865;
      _M0L6_2atmpS2860 = _M0L6_2atmpS2864 + _M0L9exp__termS1110;
      _M0L4w__sS2862 = _M0L1pS1093->$27;
      _M0L3valS2863 = _M0L1kS1101->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2861
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2862, _M0L3valS2863);
      _M0L6_2atmpS2859 = _M0L6_2atmpS2860 - _M0L6_2atmpS2861;
      _M0L6_2atmpS2855 = _M0L6_2atmpS2859 - _M0L2dwS1107;
      _M0L12syn__curr__sS2857 = _M0L1pS1093->$35;
      _M0L3valS2858 = _M0L1kS1101->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2856
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2857, _M0L3valS2858);
      _M0L6_2atmpS2853 = _M0L6_2atmpS2855 - _M0L6_2atmpS2856;
      _M0L6_2atmpS2854 = _M0L3ic1S1108 + _M0L3ic2S1109;
      _M0L6_2atmpS2849 = _M0L6_2atmpS2853 - _M0L6_2atmpS2854;
      _M0L4i__sS2851 = _M0L1pS1093->$4;
      _M0L3valS2852 = _M0L1kS1101->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2850
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2851, _M0L3valS2852);
      _M0L6_2atmpS2848 = _M0L6_2atmpS2849 + _M0L6_2atmpS2850;
      _M0L10dv__s__valS1111 = _M0L6_2atmpS2848 / _M0L1cS1095;
      _M0L5v__d1S2846 = _M0L1pS1093->$28;
      _M0L3valS2847 = _M0L1kS1101->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2845
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2846, _M0L3valS2847);
      _M0L6_2atmpS2844 = _M0L2elS1097 - _M0L6_2atmpS2845;
      _M0L6_2atmpS2839 = _M0L6_2atmpS2844 - _M0L3dd1S1105;
      _M0L2d1S2843 = _M0L1pS1093->$2;
      _M0L2gmS2841 = _M0L2d1S2843->$4;
      _M0L3valS2842 = _M0L1kS1101->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2840
      = _M0MPC15array5Array2atGfE(_M0L2gmS2841, _M0L3valS2842);
      _M0L6_2atmpS2835 = _M0L6_2atmpS2839 * _M0L6_2atmpS2840;
      _M0L13syn__curr__d1S2837 = _M0L1pS1093->$36;
      _M0L3valS2838 = _M0L1kS1101->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2836
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2837, _M0L3valS2838);
      _M0L6_2atmpS2834 = _M0L6_2atmpS2835 - _M0L6_2atmpS2836;
      _M0L6_2atmpS2830 = _M0L6_2atmpS2834 + _M0L3ic1S1108;
      _M0L5i__d1S2832 = _M0L1pS1093->$5;
      _M0L3valS2833 = _M0L1kS1101->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2831
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2832, _M0L3valS2833);
      _M0L6_2atmpS2825 = _M0L6_2atmpS2830 + _M0L6_2atmpS2831;
      _M0L2d1S2829 = _M0L1pS1093->$2;
      _M0L1cS2827 = _M0L2d1S2829->$2;
      _M0L3valS2828 = _M0L1kS1101->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2826
      = _M0MPC15array5Array2atGfE(_M0L1cS2827, _M0L3valS2828);
      _M0L11dv__d1__valS1112 = _M0L6_2atmpS2825 / _M0L6_2atmpS2826;
      _M0L5v__d2S2823 = _M0L1pS1093->$29;
      _M0L3valS2824 = _M0L1kS1101->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2822
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2823, _M0L3valS2824);
      _M0L6_2atmpS2821 = _M0L2elS1097 - _M0L6_2atmpS2822;
      _M0L6_2atmpS2816 = _M0L6_2atmpS2821 - _M0L3dd2S1106;
      _M0L2d2S2820 = _M0L1pS1093->$3;
      _M0L2gmS2818 = _M0L2d2S2820->$4;
      _M0L3valS2819 = _M0L1kS1101->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2817
      = _M0MPC15array5Array2atGfE(_M0L2gmS2818, _M0L3valS2819);
      _M0L6_2atmpS2812 = _M0L6_2atmpS2816 * _M0L6_2atmpS2817;
      _M0L13syn__curr__d2S2814 = _M0L1pS1093->$37;
      _M0L3valS2815 = _M0L1kS1101->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2813
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2814, _M0L3valS2815);
      _M0L6_2atmpS2811 = _M0L6_2atmpS2812 - _M0L6_2atmpS2813;
      _M0L6_2atmpS2807 = _M0L6_2atmpS2811 + _M0L3ic2S1109;
      _M0L5i__d2S2809 = _M0L1pS1093->$6;
      _M0L3valS2810 = _M0L1kS1101->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2808
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2809, _M0L3valS2810);
      _M0L6_2atmpS2802 = _M0L6_2atmpS2807 + _M0L6_2atmpS2808;
      _M0L2d2S2806 = _M0L1pS1093->$3;
      _M0L1cS2804 = _M0L2d2S2806->$2;
      _M0L3valS2805 = _M0L1kS1101->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2803
      = _M0MPC15array5Array2atGfE(_M0L1cS2804, _M0L3valS2805);
      _M0L11dv__d2__valS1113 = _M0L6_2atmpS2802 / _M0L6_2atmpS2803;
      _M0L4v__sS2800 = _M0L1pS1093->$26;
      _M0L3valS2801 = _M0L1kS1101->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2799
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2800, _M0L3valS2801);
      _M0L6_2atmpS2798 = _M0L6_2atmpS2799 + _M0L2dsS1102;
      _M0L6_2atmpS2797 = _M0L6_2atmpS2798 - _M0L2elS1097;
      _M0L6_2atmpS2792 = _M0L1aS1100 * _M0L6_2atmpS2797;
      _M0L4w__sS2795 = _M0L1pS1093->$27;
      _M0L3valS2796 = _M0L1kS1101->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2794
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2795, _M0L3valS2796);
      _M0L6_2atmpS2793 = _M0L6_2atmpS2794 + _M0L2dwS1107;
      _M0L6_2atmpS2791 = _M0L6_2atmpS2792 - _M0L6_2atmpS2793;
      _M0L7dw__valS1114 = _M0L6_2atmpS2791 / _M0L2twS1099;
      if (_M0L11store__tempS1103) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2759 = _M0L1pS1093->$34;
        int32_t _M0L3valS2761 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2760 = _M0L3valS2761 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2762;
        int32_t _M0L3valS2765;
        int32_t _M0L6_2atmpS2764;
        int32_t _M0L6_2atmpS2763;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2766;
        int32_t _M0L3valS2769;
        int32_t _M0L6_2atmpS2768;
        int32_t _M0L6_2atmpS2767;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2770;
        int32_t _M0L3valS2773;
        int32_t _M0L6_2atmpS2772;
        int32_t _M0L6_2atmpS2771;
        #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2759, _M0L6_2atmpS2760, _M0L10dv__s__valS1111);
        _M0L8dv__tempS2762 = _M0L1pS1093->$34;
        _M0L3valS2765 = _M0L1kS1101->$0;
        _M0L6_2atmpS2764 = _M0L3valS2765 * 4;
        _M0L6_2atmpS2763 = _M0L6_2atmpS2764 + 1;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2762, _M0L6_2atmpS2763, _M0L11dv__d1__valS1112);
        _M0L8dv__tempS2766 = _M0L1pS1093->$34;
        _M0L3valS2769 = _M0L1kS1101->$0;
        _M0L6_2atmpS2768 = _M0L3valS2769 * 4;
        _M0L6_2atmpS2767 = _M0L6_2atmpS2768 + 2;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2766, _M0L6_2atmpS2767, _M0L11dv__d2__valS1113);
        _M0L8dv__tempS2770 = _M0L1pS1093->$34;
        _M0L3valS2773 = _M0L1kS1101->$0;
        _M0L6_2atmpS2772 = _M0L3valS2773 * 4;
        _M0L6_2atmpS2771 = _M0L6_2atmpS2772 + 3;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2770, _M0L6_2atmpS2771, _M0L7dw__valS1114);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2774 = _M0L1pS1093->$33;
        int32_t _M0L3valS2776 = _M0L1kS1101->$0;
        int32_t _M0L6_2atmpS2775 = _M0L3valS2776 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2777;
        int32_t _M0L3valS2780;
        int32_t _M0L6_2atmpS2779;
        int32_t _M0L6_2atmpS2778;
        struct _M0TPB5ArrayGfE* _M0L2dvS2781;
        int32_t _M0L3valS2784;
        int32_t _M0L6_2atmpS2783;
        int32_t _M0L6_2atmpS2782;
        struct _M0TPB5ArrayGfE* _M0L2dvS2785;
        int32_t _M0L3valS2788;
        int32_t _M0L6_2atmpS2787;
        int32_t _M0L6_2atmpS2786;
        #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2774, _M0L6_2atmpS2775, _M0L10dv__s__valS1111);
        _M0L2dvS2777 = _M0L1pS1093->$33;
        _M0L3valS2780 = _M0L1kS1101->$0;
        _M0L6_2atmpS2779 = _M0L3valS2780 * 4;
        _M0L6_2atmpS2778 = _M0L6_2atmpS2779 + 1;
        #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2777, _M0L6_2atmpS2778, _M0L11dv__d1__valS1112);
        _M0L2dvS2781 = _M0L1pS1093->$33;
        _M0L3valS2784 = _M0L1kS1101->$0;
        _M0L6_2atmpS2783 = _M0L3valS2784 * 4;
        _M0L6_2atmpS2782 = _M0L6_2atmpS2783 + 2;
        #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2781, _M0L6_2atmpS2782, _M0L11dv__d2__valS1113);
        _M0L2dvS2785 = _M0L1pS1093->$33;
        _M0L3valS2788 = _M0L1kS1101->$0;
        _M0L6_2atmpS2787 = _M0L3valS2788 * 4;
        _M0L6_2atmpS2786 = _M0L6_2atmpS2787 + 3;
        #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2785, _M0L6_2atmpS2786, _M0L7dw__valS1114);
      }
      _M0L3valS2790 = _M0L1kS1101->$0;
      _M0L6_2atmpS2789 = _M0L3valS2790 + 1;
      _M0L1kS1101->$0 = _M0L6_2atmpS2789;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1101);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1088
) {
  int32_t _M0L1nS1087;
  int32_t _M0L7_2abindS1089;
  int32_t _M0L1iS1090;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1087 = _M0L1pS1088->$25;
  _M0L7_2abindS1089 = 0;
  _M0L1iS1090 = _M0L7_2abindS1089;
  while (1) {
    if (_M0L1iS1090 < _M0L1nS1087) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2717 = _M0L1pS1088->$36;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2736 = _M0L1pS1088->$9;
      float _M0L6_2atmpS2731;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2735;
      float _M0L6_2atmpS2733;
      float _M0L4e__eS2734;
      float _M0L6_2atmpS2732;
      float _M0L6_2atmpS2729;
      float _M0L7gsyn__eS2730;
      float _M0L6_2atmpS2719;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2728;
      float _M0L6_2atmpS2723;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2727;
      float _M0L6_2atmpS2725;
      float _M0L4e__iS2726;
      float _M0L6_2atmpS2724;
      float _M0L6_2atmpS2721;
      float _M0L7gsyn__iS2722;
      float _M0L6_2atmpS2720;
      float _M0L6_2atmpS2718;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2737;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2756;
      float _M0L6_2atmpS2751;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2755;
      float _M0L6_2atmpS2753;
      float _M0L4e__eS2754;
      float _M0L6_2atmpS2752;
      float _M0L6_2atmpS2749;
      float _M0L7gsyn__eS2750;
      float _M0L6_2atmpS2739;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2748;
      float _M0L6_2atmpS2743;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2747;
      float _M0L6_2atmpS2745;
      float _M0L4e__iS2746;
      float _M0L6_2atmpS2744;
      float _M0L6_2atmpS2741;
      float _M0L7gsyn__iS2742;
      float _M0L6_2atmpS2740;
      float _M0L6_2atmpS2738;
      int32_t _M0L6_2atmpS2757;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2731
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2736, _M0L1iS1090);
      _M0L5v__d1S2735 = _M0L1pS1088->$28;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2733
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2735, _M0L1iS1090);
      _M0L4e__eS2734 = _M0L1pS1088->$19;
      _M0L6_2atmpS2732 = _M0L6_2atmpS2733 - _M0L4e__eS2734;
      _M0L6_2atmpS2729 = _M0L6_2atmpS2731 * _M0L6_2atmpS2732;
      _M0L7gsyn__eS2730 = _M0L1pS1088->$23;
      _M0L6_2atmpS2719 = _M0L6_2atmpS2729 * _M0L7gsyn__eS2730;
      _M0L6gi__d1S2728 = _M0L1pS1088->$10;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2723
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2728, _M0L1iS1090);
      _M0L5v__d1S2727 = _M0L1pS1088->$28;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2725
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2727, _M0L1iS1090);
      _M0L4e__iS2726 = _M0L1pS1088->$20;
      _M0L6_2atmpS2724 = _M0L6_2atmpS2725 - _M0L4e__iS2726;
      _M0L6_2atmpS2721 = _M0L6_2atmpS2723 * _M0L6_2atmpS2724;
      _M0L7gsyn__iS2722 = _M0L1pS1088->$24;
      _M0L6_2atmpS2720 = _M0L6_2atmpS2721 * _M0L7gsyn__iS2722;
      _M0L6_2atmpS2718 = _M0L6_2atmpS2719 + _M0L6_2atmpS2720;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S2717, _M0L1iS1090, _M0L6_2atmpS2718);
      _M0L13syn__curr__d2S2737 = _M0L1pS1088->$37;
      _M0L6ge__d2S2756 = _M0L1pS1088->$11;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2751
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2756, _M0L1iS1090);
      _M0L5v__d2S2755 = _M0L1pS1088->$29;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2753
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2755, _M0L1iS1090);
      _M0L4e__eS2754 = _M0L1pS1088->$19;
      _M0L6_2atmpS2752 = _M0L6_2atmpS2753 - _M0L4e__eS2754;
      _M0L6_2atmpS2749 = _M0L6_2atmpS2751 * _M0L6_2atmpS2752;
      _M0L7gsyn__eS2750 = _M0L1pS1088->$23;
      _M0L6_2atmpS2739 = _M0L6_2atmpS2749 * _M0L7gsyn__eS2750;
      _M0L6gi__d2S2748 = _M0L1pS1088->$12;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2743
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2748, _M0L1iS1090);
      _M0L5v__d2S2747 = _M0L1pS1088->$29;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2745
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2747, _M0L1iS1090);
      _M0L4e__iS2746 = _M0L1pS1088->$20;
      _M0L6_2atmpS2744 = _M0L6_2atmpS2745 - _M0L4e__iS2746;
      _M0L6_2atmpS2741 = _M0L6_2atmpS2743 * _M0L6_2atmpS2744;
      _M0L7gsyn__iS2742 = _M0L1pS1088->$24;
      _M0L6_2atmpS2740 = _M0L6_2atmpS2741 * _M0L7gsyn__iS2742;
      _M0L6_2atmpS2738 = _M0L6_2atmpS2739 + _M0L6_2atmpS2740;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S2737, _M0L1iS1090, _M0L6_2atmpS2738);
      _M0L6_2atmpS2757 = _M0L1iS1090 + 1;
      _M0L1iS1090 = _M0L6_2atmpS2757;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1083
) {
  int32_t _M0L1nS1082;
  int32_t _M0L7_2abindS1084;
  int32_t _M0L1iS1085;
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1082 = _M0L1pS1083->$25;
  _M0L7_2abindS1084 = 0;
  _M0L1iS1085 = _M0L7_2abindS1084;
  while (1) {
    if (_M0L1iS1085 < _M0L1nS1082) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2696 = _M0L1pS1083->$35;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2715 = _M0L1pS1083->$7;
      float _M0L6_2atmpS2710;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2714;
      float _M0L6_2atmpS2712;
      float _M0L4e__eS2713;
      float _M0L6_2atmpS2711;
      float _M0L6_2atmpS2708;
      float _M0L7gsyn__eS2709;
      float _M0L6_2atmpS2698;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2707;
      float _M0L6_2atmpS2702;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2706;
      float _M0L6_2atmpS2704;
      float _M0L4e__iS2705;
      float _M0L6_2atmpS2703;
      float _M0L6_2atmpS2700;
      float _M0L7gsyn__iS2701;
      float _M0L6_2atmpS2699;
      float _M0L6_2atmpS2697;
      int32_t _M0L6_2atmpS2716;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2710
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2715, _M0L1iS1085);
      _M0L4v__sS2714 = _M0L1pS1083->$26;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2712
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2714, _M0L1iS1085);
      _M0L4e__eS2713 = _M0L1pS1083->$19;
      _M0L6_2atmpS2711 = _M0L6_2atmpS2712 - _M0L4e__eS2713;
      _M0L6_2atmpS2708 = _M0L6_2atmpS2710 * _M0L6_2atmpS2711;
      _M0L7gsyn__eS2709 = _M0L1pS1083->$23;
      _M0L6_2atmpS2698 = _M0L6_2atmpS2708 * _M0L7gsyn__eS2709;
      _M0L5gi__sS2707 = _M0L1pS1083->$8;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2702
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2707, _M0L1iS1085);
      _M0L4v__sS2706 = _M0L1pS1083->$26;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2704
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2706, _M0L1iS1085);
      _M0L4e__iS2705 = _M0L1pS1083->$20;
      _M0L6_2atmpS2703 = _M0L6_2atmpS2704 - _M0L4e__iS2705;
      _M0L6_2atmpS2700 = _M0L6_2atmpS2702 * _M0L6_2atmpS2703;
      _M0L7gsyn__iS2701 = _M0L1pS1083->$24;
      _M0L6_2atmpS2699 = _M0L6_2atmpS2700 * _M0L7gsyn__iS2701;
      _M0L6_2atmpS2697 = _M0L6_2atmpS2698 + _M0L6_2atmpS2699;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2696, _M0L1iS1085, _M0L6_2atmpS2697);
      _M0L6_2atmpS2716 = _M0L1iS1085 + 1;
      _M0L1iS1085 = _M0L6_2atmpS2716;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1068,
  float _M0L2dtS1071
) {
  int32_t _M0L1nS1067;
  int32_t _M0L7_2abindS1069;
  int32_t _M0L1iS1070;
  int32_t _M0L7_2abindS1073;
  int32_t _M0L1iS1074;
  int32_t _M0L7_2abindS1076;
  int32_t _M0L1iS1077;
  int32_t _M0L7_2abindS1079;
  int32_t _M0L1iS1080;
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1067 = _M0L1pS1068->$25;
  _M0L7_2abindS1069 = 0;
  _M0L1iS1070 = _M0L7_2abindS1069;
  while (1) {
    if (_M0L1iS1070 < _M0L1nS1067) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2624 = _M0L1pS1068->$9;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2629 = _M0L1pS1068->$9;
      float _M0L6_2atmpS2626;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2628;
      float _M0L6_2atmpS2627;
      float _M0L6_2atmpS2625;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2630;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2635;
      float _M0L6_2atmpS2632;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2634;
      float _M0L6_2atmpS2633;
      float _M0L6_2atmpS2631;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2636;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2645;
      float _M0L6_2atmpS2638;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2644;
      float _M0L6_2atmpS2643;
      float _M0L6_2atmpS2641;
      float _M0L6tau__eS2642;
      float _M0L6_2atmpS2640;
      float _M0L6_2atmpS2639;
      float _M0L6_2atmpS2637;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2646;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2655;
      float _M0L6_2atmpS2648;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2654;
      float _M0L6_2atmpS2653;
      float _M0L6_2atmpS2651;
      float _M0L6tau__iS2652;
      float _M0L6_2atmpS2650;
      float _M0L6_2atmpS2649;
      float _M0L6_2atmpS2647;
      int32_t _M0L6_2atmpS2656;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2626
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2629, _M0L1iS1070);
      _M0L7glu__d1S2628 = _M0L1pS1068->$15;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2627
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S2628, _M0L1iS1070);
      _M0L6_2atmpS2625 = _M0L6_2atmpS2626 + _M0L6_2atmpS2627;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2624, _M0L1iS1070, _M0L6_2atmpS2625);
      _M0L6gi__d1S2630 = _M0L1pS1068->$10;
      _M0L6gi__d1S2635 = _M0L1pS1068->$10;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2632
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2635, _M0L1iS1070);
      _M0L8gaba__d1S2634 = _M0L1pS1068->$16;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2633
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S2634, _M0L1iS1070);
      _M0L6_2atmpS2631 = _M0L6_2atmpS2632 + _M0L6_2atmpS2633;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2630, _M0L1iS1070, _M0L6_2atmpS2631);
      _M0L6ge__d1S2636 = _M0L1pS1068->$9;
      _M0L6ge__d1S2645 = _M0L1pS1068->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2638
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2645, _M0L1iS1070);
      _M0L6ge__d1S2644 = _M0L1pS1068->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2643
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2644, _M0L1iS1070);
      _M0L6_2atmpS2641 = -_M0L6_2atmpS2643;
      _M0L6tau__eS2642 = _M0L1pS1068->$21;
      _M0L6_2atmpS2640 = _M0L6_2atmpS2641 / _M0L6tau__eS2642;
      _M0L6_2atmpS2639 = _M0L2dtS1071 * _M0L6_2atmpS2640;
      _M0L6_2atmpS2637 = _M0L6_2atmpS2638 + _M0L6_2atmpS2639;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2636, _M0L1iS1070, _M0L6_2atmpS2637);
      _M0L6gi__d1S2646 = _M0L1pS1068->$10;
      _M0L6gi__d1S2655 = _M0L1pS1068->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2648
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2655, _M0L1iS1070);
      _M0L6gi__d1S2654 = _M0L1pS1068->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2653
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2654, _M0L1iS1070);
      _M0L6_2atmpS2651 = -_M0L6_2atmpS2653;
      _M0L6tau__iS2652 = _M0L1pS1068->$22;
      _M0L6_2atmpS2650 = _M0L6_2atmpS2651 / _M0L6tau__iS2652;
      _M0L6_2atmpS2649 = _M0L2dtS1071 * _M0L6_2atmpS2650;
      _M0L6_2atmpS2647 = _M0L6_2atmpS2648 + _M0L6_2atmpS2649;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2646, _M0L1iS1070, _M0L6_2atmpS2647);
      _M0L6_2atmpS2656 = _M0L1iS1070 + 1;
      _M0L1iS1070 = _M0L6_2atmpS2656;
      continue;
    }
    break;
  }
  _M0L7_2abindS1073 = 0;
  _M0L1iS1074 = _M0L7_2abindS1073;
  while (1) {
    if (_M0L1iS1074 < _M0L1nS1067) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2657 = _M0L1pS1068->$15;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2658;
      int32_t _M0L6_2atmpS2659;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S2657, _M0L1iS1074, 0x0p+0f);
      _M0L8gaba__d1S2658 = _M0L1pS1068->$16;
      #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S2658, _M0L1iS1074, 0x0p+0f);
      _M0L6_2atmpS2659 = _M0L1iS1074 + 1;
      _M0L1iS1074 = _M0L6_2atmpS2659;
      continue;
    }
    break;
  }
  _M0L7_2abindS1076 = 0;
  _M0L1iS1077 = _M0L7_2abindS1076;
  while (1) {
    if (_M0L1iS1077 < _M0L1nS1067) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2660 = _M0L1pS1068->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2665 = _M0L1pS1068->$11;
      float _M0L6_2atmpS2662;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2664;
      float _M0L6_2atmpS2663;
      float _M0L6_2atmpS2661;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2666;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2671;
      float _M0L6_2atmpS2668;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2670;
      float _M0L6_2atmpS2669;
      float _M0L6_2atmpS2667;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2672;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2681;
      float _M0L6_2atmpS2674;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2680;
      float _M0L6_2atmpS2679;
      float _M0L6_2atmpS2677;
      float _M0L6tau__eS2678;
      float _M0L6_2atmpS2676;
      float _M0L6_2atmpS2675;
      float _M0L6_2atmpS2673;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2682;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2691;
      float _M0L6_2atmpS2684;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2690;
      float _M0L6_2atmpS2689;
      float _M0L6_2atmpS2687;
      float _M0L6tau__iS2688;
      float _M0L6_2atmpS2686;
      float _M0L6_2atmpS2685;
      float _M0L6_2atmpS2683;
      int32_t _M0L6_2atmpS2692;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2662
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2665, _M0L1iS1077);
      _M0L7glu__d2S2664 = _M0L1pS1068->$17;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2663
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S2664, _M0L1iS1077);
      _M0L6_2atmpS2661 = _M0L6_2atmpS2662 + _M0L6_2atmpS2663;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2660, _M0L1iS1077, _M0L6_2atmpS2661);
      _M0L6gi__d2S2666 = _M0L1pS1068->$12;
      _M0L6gi__d2S2671 = _M0L1pS1068->$12;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2668
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2671, _M0L1iS1077);
      _M0L8gaba__d2S2670 = _M0L1pS1068->$18;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2669
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S2670, _M0L1iS1077);
      _M0L6_2atmpS2667 = _M0L6_2atmpS2668 + _M0L6_2atmpS2669;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2666, _M0L1iS1077, _M0L6_2atmpS2667);
      _M0L6ge__d2S2672 = _M0L1pS1068->$11;
      _M0L6ge__d2S2681 = _M0L1pS1068->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2674
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2681, _M0L1iS1077);
      _M0L6ge__d2S2680 = _M0L1pS1068->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2679
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2680, _M0L1iS1077);
      _M0L6_2atmpS2677 = -_M0L6_2atmpS2679;
      _M0L6tau__eS2678 = _M0L1pS1068->$21;
      _M0L6_2atmpS2676 = _M0L6_2atmpS2677 / _M0L6tau__eS2678;
      _M0L6_2atmpS2675 = _M0L2dtS1071 * _M0L6_2atmpS2676;
      _M0L6_2atmpS2673 = _M0L6_2atmpS2674 + _M0L6_2atmpS2675;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2672, _M0L1iS1077, _M0L6_2atmpS2673);
      _M0L6gi__d2S2682 = _M0L1pS1068->$12;
      _M0L6gi__d2S2691 = _M0L1pS1068->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2684
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2691, _M0L1iS1077);
      _M0L6gi__d2S2690 = _M0L1pS1068->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2689
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2690, _M0L1iS1077);
      _M0L6_2atmpS2687 = -_M0L6_2atmpS2689;
      _M0L6tau__iS2688 = _M0L1pS1068->$22;
      _M0L6_2atmpS2686 = _M0L6_2atmpS2687 / _M0L6tau__iS2688;
      _M0L6_2atmpS2685 = _M0L2dtS1071 * _M0L6_2atmpS2686;
      _M0L6_2atmpS2683 = _M0L6_2atmpS2684 + _M0L6_2atmpS2685;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2682, _M0L1iS1077, _M0L6_2atmpS2683);
      _M0L6_2atmpS2692 = _M0L1iS1077 + 1;
      _M0L1iS1077 = _M0L6_2atmpS2692;
      continue;
    }
    break;
  }
  _M0L7_2abindS1079 = 0;
  _M0L1iS1080 = _M0L7_2abindS1079;
  while (1) {
    if (_M0L1iS1080 < _M0L1nS1067) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2693 = _M0L1pS1068->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2694;
      int32_t _M0L6_2atmpS2695;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S2693, _M0L1iS1080, 0x0p+0f);
      _M0L8gaba__d2S2694 = _M0L1pS1068->$18;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S2694, _M0L1iS1080, 0x0p+0f);
      _M0L6_2atmpS2695 = _M0L1iS1080 + 1;
      _M0L1iS1080 = _M0L6_2atmpS2695;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS1059,
  float _M0L2dtS1062
) {
  int32_t _M0L1nS1058;
  int32_t _M0L7_2abindS1060;
  int32_t _M0L1iS1061;
  int32_t _M0L7_2abindS1064;
  int32_t _M0L1iS1065;
  #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS1058 = _M0L1pS1059->$25;
  _M0L7_2abindS1060 = 0;
  _M0L1iS1061 = _M0L7_2abindS1060;
  while (1) {
    if (_M0L1iS1061 < _M0L1nS1058) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2588 = _M0L1pS1059->$7;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2593 = _M0L1pS1059->$7;
      float _M0L6_2atmpS2590;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2592;
      float _M0L6_2atmpS2591;
      float _M0L6_2atmpS2589;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2594;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2599;
      float _M0L6_2atmpS2596;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2598;
      float _M0L6_2atmpS2597;
      float _M0L6_2atmpS2595;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2600;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2609;
      float _M0L6_2atmpS2602;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2608;
      float _M0L6_2atmpS2607;
      float _M0L6_2atmpS2605;
      float _M0L6tau__eS2606;
      float _M0L6_2atmpS2604;
      float _M0L6_2atmpS2603;
      float _M0L6_2atmpS2601;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2610;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2619;
      float _M0L6_2atmpS2612;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2618;
      float _M0L6_2atmpS2617;
      float _M0L6_2atmpS2615;
      float _M0L6tau__iS2616;
      float _M0L6_2atmpS2614;
      float _M0L6_2atmpS2613;
      float _M0L6_2atmpS2611;
      int32_t _M0L6_2atmpS2620;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2590
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2593, _M0L1iS1061);
      _M0L6glu__sS2592 = _M0L1pS1059->$13;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2591
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2592, _M0L1iS1061);
      _M0L6_2atmpS2589 = _M0L6_2atmpS2590 + _M0L6_2atmpS2591;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2588, _M0L1iS1061, _M0L6_2atmpS2589);
      _M0L5gi__sS2594 = _M0L1pS1059->$8;
      _M0L5gi__sS2599 = _M0L1pS1059->$8;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2596
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2599, _M0L1iS1061);
      _M0L7gaba__sS2598 = _M0L1pS1059->$14;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2597
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2598, _M0L1iS1061);
      _M0L6_2atmpS2595 = _M0L6_2atmpS2596 + _M0L6_2atmpS2597;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2594, _M0L1iS1061, _M0L6_2atmpS2595);
      _M0L5ge__sS2600 = _M0L1pS1059->$7;
      _M0L5ge__sS2609 = _M0L1pS1059->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2602
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2609, _M0L1iS1061);
      _M0L5ge__sS2608 = _M0L1pS1059->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2607
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2608, _M0L1iS1061);
      _M0L6_2atmpS2605 = -_M0L6_2atmpS2607;
      _M0L6tau__eS2606 = _M0L1pS1059->$21;
      _M0L6_2atmpS2604 = _M0L6_2atmpS2605 / _M0L6tau__eS2606;
      _M0L6_2atmpS2603 = _M0L2dtS1062 * _M0L6_2atmpS2604;
      _M0L6_2atmpS2601 = _M0L6_2atmpS2602 + _M0L6_2atmpS2603;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2600, _M0L1iS1061, _M0L6_2atmpS2601);
      _M0L5gi__sS2610 = _M0L1pS1059->$8;
      _M0L5gi__sS2619 = _M0L1pS1059->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2612
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2619, _M0L1iS1061);
      _M0L5gi__sS2618 = _M0L1pS1059->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2617
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2618, _M0L1iS1061);
      _M0L6_2atmpS2615 = -_M0L6_2atmpS2617;
      _M0L6tau__iS2616 = _M0L1pS1059->$22;
      _M0L6_2atmpS2614 = _M0L6_2atmpS2615 / _M0L6tau__iS2616;
      _M0L6_2atmpS2613 = _M0L2dtS1062 * _M0L6_2atmpS2614;
      _M0L6_2atmpS2611 = _M0L6_2atmpS2612 + _M0L6_2atmpS2613;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2610, _M0L1iS1061, _M0L6_2atmpS2611);
      _M0L6_2atmpS2620 = _M0L1iS1061 + 1;
      _M0L1iS1061 = _M0L6_2atmpS2620;
      continue;
    }
    break;
  }
  _M0L7_2abindS1064 = 0;
  _M0L1iS1065 = _M0L7_2abindS1064;
  while (1) {
    if (_M0L1iS1065 < _M0L1nS1058) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2621 = _M0L1pS1059->$13;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2622;
      int32_t _M0L6_2atmpS2623;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2621, _M0L1iS1065, 0x0p+0f);
      _M0L7gaba__sS2622 = _M0L1pS1059->$14;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2622, _M0L1iS1065, 0x0p+0f);
      _M0L6_2atmpS2623 = _M0L1iS1065 + 1;
      _M0L1iS1065 = _M0L6_2atmpS2623;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t _M0L1nS1019,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS1021,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1024
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS1018;
  float _M0L2vtS2586;
  float _M0L2vrS2587;
  float _M0L6spreadS1020;
  int32_t _M0L7_2abindS1022;
  int32_t _M0L1kS1023;
  struct _M0TPB5ArrayGfE* _M0L4w__sS1026;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S1027;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S1028;
  int32_t _M0L7_2abindS1029;
  int32_t _M0L1kS1030;
  struct _M0TPB5ArrayGbE* _M0L4fireS1032;
  float _M0L2vtS2585;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1033;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1034;
  struct _M0TPB5ArrayGfE* _M0L4i__sS1035;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S1036;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S1037;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS1038;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS1039;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S1040;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S1041;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S1042;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S1043;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS1044;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS1045;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S1046;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S1047;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S1048;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S1049;
  int32_t _M0L6total4S1050;
  struct _M0TPB5ArrayGfE* _M0L2dvS1051;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS1052;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1053;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S1054;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S1055;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S1056;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S1057;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2584;
  struct _M0TP26RiantR8snn__mbt6Tripod* _block_3527;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4v__sS1018 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  _M0L2vtS2586 = _M0L11soma__paramS1021->$2;
  _M0L2vrS2587 = _M0L11soma__paramS1021->$3;
  _M0L6spreadS1020 = _M0L2vtS2586 - _M0L2vrS2587;
  _M0L7_2abindS1022 = 0;
  _M0L1kS1023 = _M0L7_2abindS1022;
  while (1) {
    if (_M0L1kS1023 < _M0L1nS1019) {
      float _M0L2vrS2571 = _M0L11soma__paramS1021->$3;
      float _M0L6_2atmpS2573;
      float _M0L6_2atmpS2572;
      float _M0L6_2atmpS2570;
      int32_t _M0L6_2atmpS2574;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2573 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1024);
      _M0L6_2atmpS2572 = _M0L6_2atmpS2573 * _M0L6spreadS1020;
      _M0L6_2atmpS2570 = _M0L2vrS2571 + _M0L6_2atmpS2572;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS1018, _M0L1kS1023, _M0L6_2atmpS2570);
      _M0L6_2atmpS2574 = _M0L1kS1023 + 1;
      _M0L1kS1023 = _M0L6_2atmpS2574;
      continue;
    }
    break;
  }
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4w__sS1026 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d1S1027 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d2S1028 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  _M0L7_2abindS1029 = 0;
  _M0L1kS1030 = _M0L7_2abindS1029;
  while (1) {
    if (_M0L1kS1030 < _M0L1nS1019) {
      float _M0L2vrS2576 = _M0L11soma__paramS1021->$3;
      float _M0L6_2atmpS2578;
      float _M0L6_2atmpS2577;
      float _M0L6_2atmpS2575;
      float _M0L2vrS2580;
      float _M0L6_2atmpS2582;
      float _M0L6_2atmpS2581;
      float _M0L6_2atmpS2579;
      int32_t _M0L6_2atmpS2583;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2578 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1024);
      _M0L6_2atmpS2577 = _M0L6_2atmpS2578 * _M0L6spreadS1020;
      _M0L6_2atmpS2575 = _M0L2vrS2576 + _M0L6_2atmpS2577;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S1027, _M0L1kS1030, _M0L6_2atmpS2575);
      _M0L2vrS2580 = _M0L11soma__paramS1021->$3;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2582 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1024);
      _M0L6_2atmpS2581 = _M0L6_2atmpS2582 * _M0L6spreadS1020;
      _M0L6_2atmpS2579 = _M0L2vrS2580 + _M0L6_2atmpS2581;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S1028, _M0L1kS1030, _M0L6_2atmpS2579);
      _M0L6_2atmpS2583 = _M0L1kS1030 + 1;
      _M0L1kS1030 = _M0L6_2atmpS2583;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4fireS1032 = _M0MPC15array5Array4makeGbE(_M0L1nS1019, 0);
  _M0L2vtS2585 = _M0L11soma__paramS1021->$2;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L9thresholdS1033
  = _M0MPC15array5Array4makeGfE(_M0L1nS1019, _M0L2vtS2585);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4tabsS1034 = _M0MPC15array5Array4makeGiE(_M0L1nS1019, 1);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4i__sS1035 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d1S1036 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d2S1037 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5ge__sS1038 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5gi__sS1039 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d1S1040 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d1S1041 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d2S1042 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d2S1043 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6glu__sS1044 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7gaba__sS1045 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d1S1046 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d1S1047 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d2S1048 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d2S1049 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  _M0L6total4S1050 = _M0L1nS1019 * 4;
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2dvS1051 = _M0MPC15array5Array4makeGfE(_M0L6total4S1050, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8dv__tempS1052 = _M0MPC15array5Array4makeGfE(_M0L6total4S1050, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L12syn__curr__sS1053 = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d1S1054
  = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d2S1055
  = _M0MPC15array5Array4makeGfE(_M0L1nS1019, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d1S1056 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1019);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d2S1057 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1019);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6_2atmpS2584 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS1021);
  _block_3527
  = (struct _M0TP26RiantR8snn__mbt6Tripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt6Tripod));
  Moonbit_object_header(_block_3527)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _block_3527->$0 = _M0L11soma__paramS1021;
  _block_3527->$1 = _M0L6_2atmpS2584;
  _block_3527->$2 = _M0L2d1S1056;
  _block_3527->$3 = _M0L2d2S1057;
  _block_3527->$4 = _M0L4i__sS1035;
  _block_3527->$5 = _M0L5i__d1S1036;
  _block_3527->$6 = _M0L5i__d2S1037;
  _block_3527->$7 = _M0L5ge__sS1038;
  _block_3527->$8 = _M0L5gi__sS1039;
  _block_3527->$9 = _M0L6ge__d1S1040;
  _block_3527->$10 = _M0L6gi__d1S1041;
  _block_3527->$11 = _M0L6ge__d2S1042;
  _block_3527->$12 = _M0L6gi__d2S1043;
  _block_3527->$13 = _M0L6glu__sS1044;
  _block_3527->$14 = _M0L7gaba__sS1045;
  _block_3527->$15 = _M0L7glu__d1S1046;
  _block_3527->$16 = _M0L8gaba__d1S1047;
  _block_3527->$17 = _M0L7glu__d2S1048;
  _block_3527->$18 = _M0L8gaba__d2S1049;
  _block_3527->$19 = 0x0p+0f;
  _block_3527->$20 = -0x1.2cp+6f;
  _block_3527->$21 = 0x1.8p+2f;
  _block_3527->$22 = 0x1p+1f;
  _block_3527->$23 = 0x1p+0f;
  _block_3527->$24 = 0x1p+0f;
  _block_3527->$25 = _M0L1nS1019;
  _block_3527->$26 = _M0L4v__sS1018;
  _block_3527->$27 = _M0L4w__sS1026;
  _block_3527->$28 = _M0L5v__d1S1027;
  _block_3527->$29 = _M0L5v__d2S1028;
  _block_3527->$30 = _M0L4fireS1032;
  _block_3527->$31 = _M0L9thresholdS1033;
  _block_3527->$32 = _M0L4tabsS1034;
  _block_3527->$33 = _M0L2dvS1051;
  _block_3527->$34 = _M0L8dv__tempS1052;
  _block_3527->$35 = _M0L12syn__curr__sS1053;
  _block_3527->$36 = _M0L13syn__curr__d1S1054;
  _block_3527->$37 = _M0L13syn__curr__d2S1055;
  return _block_3527;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS1011
) {
  struct _M0TPB5ArrayGfE* _M0L2elS1010;
  struct _M0TPB5ArrayGfE* _M0L1cS1012;
  struct _M0TPB5ArrayGfE* _M0L3gaxS1013;
  struct _M0TPB5ArrayGfE* _M0L2gmS1014;
  struct _M0TPB5ArrayGfE* _M0L1lS1015;
  struct _M0TPB5ArrayGfE* _M0L1dS1016;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS1017;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_3528;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS1010
  = _M0MPC15array5Array4makeGfE(_M0L1nS1011, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS1012 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS1013 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS1014 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS1015 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS1016 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS1017 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  _block_3528
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_3528)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 105, 0);
  _block_3528->$0 = _M0L1nS1011;
  _block_3528->$1 = _M0L2elS1010;
  _block_3528->$2 = _M0L1cS1012;
  _block_3528->$3 = _M0L3gaxS1013;
  _block_3528->$4 = _M0L2gmS1014;
  _block_3528->$5 = _M0L1lS1015;
  _block_3528->$6 = _M0L1dS1016;
  _block_3528->$7 = _M0L11gax__parentS1017;
  return _block_3528;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_3529;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_3529
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_3529)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3529->$0 = 0x0p+0f;
  _block_3529->$1 = 0x1.4p+3f;
  _block_3529->$2 = 0x1.4p+3f;
  _block_3529->$3 = 0x1p+0f;
  _block_3529->$4 = 0x1p+0f;
  return _block_3529;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1006
) {
  int32_t _M0L1nS1005;
  int32_t _M0L7_2abindS1007;
  int32_t _M0L1iS1008;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1005 = _M0L1pS1006->$2;
  _M0L7_2abindS1007 = 0;
  _M0L1iS1008 = _M0L7_2abindS1007;
  while (1) {
    if (_M0L1iS1008 < _M0L1nS1005) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2547 = _M0L1pS1006->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2568 = _M0L1pS1006->$9;
      float _M0L6_2atmpS2563;
      struct _M0TPB5ArrayGfE* _M0L1vS2567;
      float _M0L6_2atmpS2565;
      float _M0L4e__eS2566;
      float _M0L6_2atmpS2564;
      float _M0L6_2atmpS2560;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2562;
      float _M0L6_2atmpS2561;
      float _M0L6_2atmpS2549;
      struct _M0TPB5ArrayGfE* _M0L2giS2559;
      float _M0L6_2atmpS2554;
      struct _M0TPB5ArrayGfE* _M0L1vS2558;
      float _M0L6_2atmpS2556;
      float _M0L4e__iS2557;
      float _M0L6_2atmpS2555;
      float _M0L6_2atmpS2551;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2553;
      float _M0L6_2atmpS2552;
      float _M0L6_2atmpS2550;
      float _M0L6_2atmpS2548;
      int32_t _M0L6_2atmpS2569;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2563 = _M0MPC15array5Array2atGfE(_M0L2geS2568, _M0L1iS1008);
      _M0L1vS2567 = _M0L1pS1006->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2565 = _M0MPC15array5Array2atGfE(_M0L1vS2567, _M0L1iS1008);
      _M0L4e__eS2566 = _M0L1pS1006->$17;
      _M0L6_2atmpS2564 = _M0L6_2atmpS2565 - _M0L4e__eS2566;
      _M0L6_2atmpS2560 = _M0L6_2atmpS2563 * _M0L6_2atmpS2564;
      _M0L7gsyn__eS2562 = _M0L1pS1006->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2561
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2562, _M0L1iS1008);
      _M0L6_2atmpS2549 = _M0L6_2atmpS2560 * _M0L6_2atmpS2561;
      _M0L2giS2559 = _M0L1pS1006->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2554 = _M0MPC15array5Array2atGfE(_M0L2giS2559, _M0L1iS1008);
      _M0L1vS2558 = _M0L1pS1006->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2556 = _M0MPC15array5Array2atGfE(_M0L1vS2558, _M0L1iS1008);
      _M0L4e__iS2557 = _M0L1pS1006->$18;
      _M0L6_2atmpS2555 = _M0L6_2atmpS2556 - _M0L4e__iS2557;
      _M0L6_2atmpS2551 = _M0L6_2atmpS2554 * _M0L6_2atmpS2555;
      _M0L7gsyn__iS2553 = _M0L1pS1006->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2552
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2553, _M0L1iS1008);
      _M0L6_2atmpS2550 = _M0L6_2atmpS2551 * _M0L6_2atmpS2552;
      _M0L6_2atmpS2548 = _M0L6_2atmpS2549 + _M0L6_2atmpS2550;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2547, _M0L1iS1008, _M0L6_2atmpS2548);
      _M0L6_2atmpS2569 = _M0L1iS1008 + 1;
      _M0L1iS1008 = _M0L6_2atmpS2569;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS997,
  float _M0L2dtS1000
) {
  int32_t _M0L1nS996;
  int32_t _M0L7_2abindS998;
  int32_t _M0L1iS999;
  int32_t _M0L7_2abindS1002;
  int32_t _M0L1iS1003;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS996 = _M0L1pS997->$2;
  _M0L7_2abindS998 = 0;
  _M0L1iS999 = _M0L7_2abindS998;
  while (1) {
    if (_M0L1iS999 < _M0L1nS996) {
      struct _M0TPB5ArrayGfE* _M0L2heS2485 = _M0L1pS997->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2490 = _M0L1pS997->$11;
      float _M0L6_2atmpS2487;
      struct _M0TPB5ArrayGfE* _M0L3gluS2489;
      float _M0L6_2atmpS2488;
      float _M0L6_2atmpS2486;
      struct _M0TPB5ArrayGfE* _M0L2hiS2491;
      struct _M0TPB5ArrayGfE* _M0L2hiS2496;
      float _M0L6_2atmpS2493;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2495;
      float _M0L6_2atmpS2494;
      float _M0L6_2atmpS2492;
      struct _M0TPB5ArrayGfE* _M0L2geS2497;
      struct _M0TPB5ArrayGfE* _M0L2geS2509;
      float _M0L6_2atmpS2499;
      struct _M0TPB5ArrayGfE* _M0L2geS2508;
      float _M0L6_2atmpS2507;
      float _M0L6_2atmpS2505;
      float _M0L3tdeS2506;
      float _M0L6_2atmpS2502;
      struct _M0TPB5ArrayGfE* _M0L2heS2504;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2501;
      float _M0L6_2atmpS2500;
      float _M0L6_2atmpS2498;
      struct _M0TPB5ArrayGfE* _M0L2heS2510;
      struct _M0TPB5ArrayGfE* _M0L2heS2519;
      float _M0L6_2atmpS2512;
      struct _M0TPB5ArrayGfE* _M0L2heS2518;
      float _M0L6_2atmpS2517;
      float _M0L6_2atmpS2515;
      float _M0L3treS2516;
      float _M0L6_2atmpS2514;
      float _M0L6_2atmpS2513;
      float _M0L6_2atmpS2511;
      struct _M0TPB5ArrayGfE* _M0L2giS2520;
      struct _M0TPB5ArrayGfE* _M0L2giS2532;
      float _M0L6_2atmpS2522;
      struct _M0TPB5ArrayGfE* _M0L2giS2531;
      float _M0L6_2atmpS2530;
      float _M0L6_2atmpS2528;
      float _M0L3tdiS2529;
      float _M0L6_2atmpS2525;
      struct _M0TPB5ArrayGfE* _M0L2hiS2527;
      float _M0L6_2atmpS2526;
      float _M0L6_2atmpS2524;
      float _M0L6_2atmpS2523;
      float _M0L6_2atmpS2521;
      struct _M0TPB5ArrayGfE* _M0L2hiS2533;
      struct _M0TPB5ArrayGfE* _M0L2hiS2542;
      float _M0L6_2atmpS2535;
      struct _M0TPB5ArrayGfE* _M0L2hiS2541;
      float _M0L6_2atmpS2540;
      float _M0L6_2atmpS2538;
      float _M0L3triS2539;
      float _M0L6_2atmpS2537;
      float _M0L6_2atmpS2536;
      float _M0L6_2atmpS2534;
      int32_t _M0L6_2atmpS2543;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2487 = _M0MPC15array5Array2atGfE(_M0L2heS2490, _M0L1iS999);
      _M0L3gluS2489 = _M0L1pS997->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2488 = _M0MPC15array5Array2atGfE(_M0L3gluS2489, _M0L1iS999);
      _M0L6_2atmpS2486 = _M0L6_2atmpS2487 + _M0L6_2atmpS2488;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2485, _M0L1iS999, _M0L6_2atmpS2486);
      _M0L2hiS2491 = _M0L1pS997->$12;
      _M0L2hiS2496 = _M0L1pS997->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2493 = _M0MPC15array5Array2atGfE(_M0L2hiS2496, _M0L1iS999);
      _M0L4gabaS2495 = _M0L1pS997->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2494
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2495, _M0L1iS999);
      _M0L6_2atmpS2492 = _M0L6_2atmpS2493 + _M0L6_2atmpS2494;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2491, _M0L1iS999, _M0L6_2atmpS2492);
      _M0L2geS2497 = _M0L1pS997->$9;
      _M0L2geS2509 = _M0L1pS997->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2499 = _M0MPC15array5Array2atGfE(_M0L2geS2509, _M0L1iS999);
      _M0L2geS2508 = _M0L1pS997->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2507 = _M0MPC15array5Array2atGfE(_M0L2geS2508, _M0L1iS999);
      _M0L6_2atmpS2505 = -_M0L6_2atmpS2507;
      _M0L3tdeS2506 = _M0L1pS997->$20;
      _M0L6_2atmpS2502 = _M0L6_2atmpS2505 / _M0L3tdeS2506;
      _M0L2heS2504 = _M0L1pS997->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2503 = _M0MPC15array5Array2atGfE(_M0L2heS2504, _M0L1iS999);
      _M0L6_2atmpS2501 = _M0L6_2atmpS2502 + _M0L6_2atmpS2503;
      _M0L6_2atmpS2500 = _M0L2dtS1000 * _M0L6_2atmpS2501;
      _M0L6_2atmpS2498 = _M0L6_2atmpS2499 + _M0L6_2atmpS2500;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2497, _M0L1iS999, _M0L6_2atmpS2498);
      _M0L2heS2510 = _M0L1pS997->$11;
      _M0L2heS2519 = _M0L1pS997->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2512 = _M0MPC15array5Array2atGfE(_M0L2heS2519, _M0L1iS999);
      _M0L2heS2518 = _M0L1pS997->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2517 = _M0MPC15array5Array2atGfE(_M0L2heS2518, _M0L1iS999);
      _M0L6_2atmpS2515 = -_M0L6_2atmpS2517;
      _M0L3treS2516 = _M0L1pS997->$19;
      _M0L6_2atmpS2514 = _M0L6_2atmpS2515 / _M0L3treS2516;
      _M0L6_2atmpS2513 = _M0L2dtS1000 * _M0L6_2atmpS2514;
      _M0L6_2atmpS2511 = _M0L6_2atmpS2512 + _M0L6_2atmpS2513;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2510, _M0L1iS999, _M0L6_2atmpS2511);
      _M0L2giS2520 = _M0L1pS997->$10;
      _M0L2giS2532 = _M0L1pS997->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2522 = _M0MPC15array5Array2atGfE(_M0L2giS2532, _M0L1iS999);
      _M0L2giS2531 = _M0L1pS997->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2530 = _M0MPC15array5Array2atGfE(_M0L2giS2531, _M0L1iS999);
      _M0L6_2atmpS2528 = -_M0L6_2atmpS2530;
      _M0L3tdiS2529 = _M0L1pS997->$22;
      _M0L6_2atmpS2525 = _M0L6_2atmpS2528 / _M0L3tdiS2529;
      _M0L2hiS2527 = _M0L1pS997->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2526 = _M0MPC15array5Array2atGfE(_M0L2hiS2527, _M0L1iS999);
      _M0L6_2atmpS2524 = _M0L6_2atmpS2525 + _M0L6_2atmpS2526;
      _M0L6_2atmpS2523 = _M0L2dtS1000 * _M0L6_2atmpS2524;
      _M0L6_2atmpS2521 = _M0L6_2atmpS2522 + _M0L6_2atmpS2523;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2520, _M0L1iS999, _M0L6_2atmpS2521);
      _M0L2hiS2533 = _M0L1pS997->$12;
      _M0L2hiS2542 = _M0L1pS997->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2535 = _M0MPC15array5Array2atGfE(_M0L2hiS2542, _M0L1iS999);
      _M0L2hiS2541 = _M0L1pS997->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2540 = _M0MPC15array5Array2atGfE(_M0L2hiS2541, _M0L1iS999);
      _M0L6_2atmpS2538 = -_M0L6_2atmpS2540;
      _M0L3triS2539 = _M0L1pS997->$21;
      _M0L6_2atmpS2537 = _M0L6_2atmpS2538 / _M0L3triS2539;
      _M0L6_2atmpS2536 = _M0L2dtS1000 * _M0L6_2atmpS2537;
      _M0L6_2atmpS2534 = _M0L6_2atmpS2535 + _M0L6_2atmpS2536;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2533, _M0L1iS999, _M0L6_2atmpS2534);
      _M0L6_2atmpS2543 = _M0L1iS999 + 1;
      _M0L1iS999 = _M0L6_2atmpS2543;
      continue;
    }
    break;
  }
  _M0L7_2abindS1002 = 0;
  _M0L1iS1003 = _M0L7_2abindS1002;
  while (1) {
    if (_M0L1iS1003 < _M0L1nS996) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2544 = _M0L1pS997->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2545;
      int32_t _M0L6_2atmpS2546;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2544, _M0L1iS1003, 0x0p+0f);
      _M0L4gabaS2545 = _M0L1pS997->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2545, _M0L1iS1003, 0x0p+0f);
      _M0L6_2atmpS2546 = _M0L1iS1003 + 1;
      _M0L1iS1003 = _M0L6_2atmpS2546;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS982,
  float _M0L2dtS991
) {
  int32_t _M0L1nS981;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S983;
  float _M0L2tmS984;
  float _M0L2elS985;
  float _M0L1rS986;
  float _M0L2vtS987;
  float _M0L2vrS988;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2484;
  float _M0L11tabs__constS989;
  float _M0L6_2atmpS2483;
  int32_t _M0L11tabs__stepsS990;
  int32_t _M0L7_2abindS992;
  int32_t _M0L1iS993;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS981 = _M0L1pS982->$2;
  _M0L3p__S983 = _M0L1pS982->$0;
  _M0L2tmS984 = _M0L3p__S983->$2;
  _M0L2elS985 = _M0L3p__S983->$5;
  _M0L1rS986 = _M0L3p__S983->$6;
  _M0L2vtS987 = _M0L3p__S983->$3;
  _M0L2vrS988 = _M0L3p__S983->$4;
  _M0L5spikeS2484 = _M0L1pS982->$1;
  _M0L11tabs__constS989 = _M0L5spikeS2484->$0;
  _M0L6_2atmpS2483 = _M0L11tabs__constS989 / _M0L2dtS991;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS990 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2483);
  _M0L7_2abindS992 = 0;
  _M0L1iS993 = _M0L7_2abindS992;
  while (1) {
    if (_M0L1iS993 < _M0L1nS981) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2443 = _M0L1pS982->$6;
      int32_t _M0L6_2atmpS2442;
      struct _M0TPB5ArrayGfE* _M0L1vS2449;
      struct _M0TPB5ArrayGfE* _M0L1vS2470;
      float _M0L6_2atmpS2451;
      float _M0L6_2atmpS2453;
      struct _M0TPB5ArrayGfE* _M0L1vS2469;
      float _M0L6_2atmpS2468;
      float _M0L6_2atmpS2467;
      float _M0L6_2atmpS2459;
      struct _M0TPB5ArrayGfE* _M0L1wS2466;
      float _M0L6_2atmpS2465;
      float _M0L6_2atmpS2462;
      struct _M0TPB5ArrayGfE* _M0L1iS2464;
      float _M0L6_2atmpS2463;
      float _M0L6_2atmpS2461;
      float _M0L6_2atmpS2460;
      float _M0L6_2atmpS2455;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2458;
      float _M0L6_2atmpS2457;
      float _M0L6_2atmpS2456;
      float _M0L6_2atmpS2454;
      float _M0L6_2atmpS2452;
      float _M0L6_2atmpS2450;
      struct _M0TPB5ArrayGbE* _M0L4fireS2471;
      struct _M0TPB5ArrayGfE* _M0L1vS2474;
      float _M0L6_2atmpS2473;
      int32_t _M0L6_2atmpS2472;
      struct _M0TPB5ArrayGfE* _M0L1vS2475;
      struct _M0TPB5ArrayGbE* _M0L4fireS2477;
      float _M0L6_2atmpS2476;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2479;
      struct _M0TPB5ArrayGbE* _M0L4fireS2481;
      int32_t _M0L6_2atmpS2480;
      int32_t _M0L6_2atmpS2441;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2442
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2443, _M0L1iS993);
      if (_M0L6_2atmpS2442 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2444 = _M0L1pS982->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2445;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2448;
        int32_t _M0L6_2atmpS2447;
        int32_t _M0L6_2atmpS2446;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2444, _M0L1iS993, 0);
        _M0L4tabsS2445 = _M0L1pS982->$6;
        _M0L4tabsS2448 = _M0L1pS982->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2447
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2448, _M0L1iS993);
        _M0L6_2atmpS2446 = _M0L6_2atmpS2447 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2445, _M0L1iS993, _M0L6_2atmpS2446);
        goto join_994;
      }
      _M0L1vS2449 = _M0L1pS982->$3;
      _M0L1vS2470 = _M0L1pS982->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2451 = _M0MPC15array5Array2atGfE(_M0L1vS2470, _M0L1iS993);
      _M0L6_2atmpS2453 = _M0L2dtS991 / _M0L2tmS984;
      _M0L1vS2469 = _M0L1pS982->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2468 = _M0MPC15array5Array2atGfE(_M0L1vS2469, _M0L1iS993);
      _M0L6_2atmpS2467 = _M0L6_2atmpS2468 - _M0L2elS985;
      _M0L6_2atmpS2459 = -_M0L6_2atmpS2467;
      _M0L1wS2466 = _M0L1pS982->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2465 = _M0MPC15array5Array2atGfE(_M0L1wS2466, _M0L1iS993);
      _M0L6_2atmpS2462 = -_M0L6_2atmpS2465;
      _M0L1iS2464 = _M0L1pS982->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2463 = _M0MPC15array5Array2atGfE(_M0L1iS2464, _M0L1iS993);
      _M0L6_2atmpS2461 = _M0L6_2atmpS2462 + _M0L6_2atmpS2463;
      _M0L6_2atmpS2460 = _M0L1rS986 * _M0L6_2atmpS2461;
      _M0L6_2atmpS2455 = _M0L6_2atmpS2459 + _M0L6_2atmpS2460;
      _M0L9syn__currS2458 = _M0L1pS982->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2457
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2458, _M0L1iS993);
      _M0L6_2atmpS2456 = _M0L1rS986 * _M0L6_2atmpS2457;
      _M0L6_2atmpS2454 = _M0L6_2atmpS2455 - _M0L6_2atmpS2456;
      _M0L6_2atmpS2452 = _M0L6_2atmpS2453 * _M0L6_2atmpS2454;
      _M0L6_2atmpS2450 = _M0L6_2atmpS2451 + _M0L6_2atmpS2452;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2449, _M0L1iS993, _M0L6_2atmpS2450);
      _M0L4fireS2471 = _M0L1pS982->$5;
      _M0L1vS2474 = _M0L1pS982->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2473 = _M0MPC15array5Array2atGfE(_M0L1vS2474, _M0L1iS993);
      _M0L6_2atmpS2472 = _M0L6_2atmpS2473 > _M0L2vtS987;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2471, _M0L1iS993, _M0L6_2atmpS2472);
      _M0L1vS2475 = _M0L1pS982->$3;
      _M0L4fireS2477 = _M0L1pS982->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2477, _M0L1iS993)) {
        _M0L6_2atmpS2476 = _M0L2vrS988;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2478 = _M0L1pS982->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2476 = _M0MPC15array5Array2atGfE(_M0L1vS2478, _M0L1iS993);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2475, _M0L1iS993, _M0L6_2atmpS2476);
      _M0L4tabsS2479 = _M0L1pS982->$6;
      _M0L4fireS2481 = _M0L1pS982->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2481, _M0L1iS993)) {
        _M0L6_2atmpS2480 = _M0L11tabs__stepsS990;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2482 = _M0L1pS982->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2480
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2482, _M0L1iS993);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2479, _M0L1iS993, _M0L6_2atmpS2480);
      goto join_994;
      goto joinlet_3534;
      join_994:;
      _M0L6_2atmpS2441 = _M0L1iS993 + 1;
      _M0L1iS993 = _M0L6_2atmpS2441;
      continue;
      joinlet_3534:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS969,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS972,
  struct _M0TPB5ArrayGfE* _M0L7post__gS978
) {
  int32_t _M0L4rowsS968;
  int32_t _M0L7_2abindS970;
  int32_t _M0L1iS971;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS968 = _M0L1mS969->$0;
  _M0L7_2abindS970 = 0;
  _M0L1iS971 = _M0L7_2abindS970;
  while (1) {
    if (_M0L1iS971 < _M0L4rowsS968) {
      int32_t _M0L6_2atmpS2440;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS972, _M0L1iS971)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2439 = _M0L1mS969->$2;
        int32_t _M0L5startS973;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2437;
        int32_t _M0L6_2atmpS2438;
        int32_t _M0L3endS974;
        int32_t _M0L1kS975;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS973
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2439, _M0L1iS971);
        _M0L6rowptrS2437 = _M0L1mS969->$2;
        _M0L6_2atmpS2438 = _M0L1iS971 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS974
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2437, _M0L6_2atmpS2438);
        _M0L1kS975 = _M0L5startS973;
        while (1) {
          if (_M0L1kS975 < _M0L3endS974) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2435 = _M0L1mS969->$3;
            int32_t _M0L9post__idxS976;
            struct _M0TPB5ArrayGfE* _M0L4valsS2434;
            float _M0L1wS977;
            float _M0L6_2atmpS2433;
            float _M0L6_2atmpS2432;
            int32_t _M0L6_2atmpS2436;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS976
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2435, _M0L1kS975);
            _M0L4valsS2434 = _M0L1mS969->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS977
            = _M0MPC15array5Array2atGfE(_M0L4valsS2434, _M0L1kS975);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2433
            = _M0MPC15array5Array2atGfE(_M0L7post__gS978, _M0L9post__idxS976);
            _M0L6_2atmpS2432 = _M0L6_2atmpS2433 + _M0L1wS977;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS978, _M0L9post__idxS976, _M0L6_2atmpS2432);
            _M0L6_2atmpS2436 = _M0L1kS975 + 1;
            _M0L1kS975 = _M0L6_2atmpS2436;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2440 = _M0L1iS971 + 1;
      _M0L1iS971 = _M0L6_2atmpS2440;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS962,
  int32_t _M0L4colsS963,
  float _M0L2muS964,
  float _M0L5sigmaS965,
  float _M0L1pS966,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS967
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS962, _M0L4colsS963, _M0L2muS964, _M0L5sigmaS965, _M0L1pS966, 0, _M0L3rngS967);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS876,
  int32_t _M0L4colsS880,
  float _M0L2muS886,
  float _M0L5sigmaS887,
  float _M0L1pS899,
  int32_t _M0L4ruleS893,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS889
) {
  float* _M0L6_2atmpS2431;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2430;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS875;
  int32_t _M0L7_2abindS877;
  int32_t _M0L1iS878;
  int32_t _M0L6_2atmpS2429;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS952;
  int32_t* _M0L6_2atmpS2428;
  struct _M0TPB5ArrayGiE* _M0L6colptrS953;
  float* _M0L6_2atmpS2427;
  struct _M0TPB5ArrayGfE* _M0L4valsS954;
  int32_t _M0L7_2abindS955;
  int32_t _M0L1iS956;
  int32_t _M0L6_2atmpS2426;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_3556;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2431 = moonbit_empty_float_array;
  _M0L6_2atmpS2430
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2430)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2430->$0 = _M0L6_2atmpS2431;
  _M0L6_2atmpS2430->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS875
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS876, _M0L6_2atmpS2430);
  _M0L7_2abindS877 = 0;
  _M0L1iS878 = _M0L7_2abindS877;
  while (1) {
    if (_M0L1iS878 < _M0L4rowsS876) {
      struct _M0TPB5ArrayGfE* _M0L3rowS879;
      int32_t _M0L7_2abindS881;
      int32_t _M0L1jS882;
      int32_t _M0L6_2atmpS2382;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS879 = _M0MPC15array5Array4makeGfE(_M0L4colsS880, 0x0p+0f);
      _M0L7_2abindS881 = 0;
      _M0L1jS882 = _M0L7_2abindS881;
      while (1) {
        if (_M0L1jS882 < _M0L4colsS880) {
          double _M0L2z1S884;
          struct _M0TUddE* _M0L7_2abindS888;
          double _M0L5_2az1S890;
          float _M0L6_2atmpS2380;
          float _M0L6_2atmpS2379;
          float _M0L1wS885;
          int32_t _M0L6_2atmpS2381;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS888
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS889);
          _M0L5_2az1S890 = _M0L7_2abindS888->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS888);
          _M0L2z1S884 = _M0L5_2az1S890;
          goto join_883;
          goto joinlet_3539;
          join_883:;
          _M0L6_2atmpS2380 = (float)_M0L2z1S884;
          _M0L6_2atmpS2379 = _M0L5sigmaS887 * _M0L6_2atmpS2380;
          _M0L1wS885 = _M0L2muS886 + _M0L6_2atmpS2379;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS879, _M0L1jS882, _M0L1wS885);
          joinlet_3539:;
          _M0L6_2atmpS2381 = _M0L1jS882 + 1;
          _M0L1jS882 = _M0L6_2atmpS2381;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS878, _M0L3rowS879);
      _M0L6_2atmpS2382 = _M0L1iS878 + 1;
      _M0L1iS878 = _M0L6_2atmpS2382;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS893) {
    case 0: {
      int32_t _M0L7_2abindS894 = 0;
      int32_t _M0L1iS895 = _M0L7_2abindS894;
      while (1) {
        if (_M0L1iS895 < _M0L4rowsS876) {
          int32_t _M0L7_2abindS896 = 0;
          int32_t _M0L1jS897 = _M0L7_2abindS896;
          int32_t _M0L6_2atmpS2385;
          while (1) {
            if (_M0L1jS897 < _M0L4colsS880) {
              float _M0L1uS898;
              int32_t _M0L6_2atmpS2384;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS898 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
              if (_M0L1uS898 >= _M0L1pS899) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2383;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2383
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS895);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2383, _M0L1jS897, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2383);
              }
              _M0L6_2atmpS2384 = _M0L1jS897 + 1;
              _M0L1jS897 = _M0L6_2atmpS2384;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2385 = _M0L1iS895 + 1;
          _M0L1iS895 = _M0L6_2atmpS2385;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2403 = (float)_M0L4rowsS876;
      float _M0L6_2atmpS2402 = _M0L6_2atmpS2403 * _M0L1pS899;
      int32_t _M0L7n__keepS902;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS902 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2402);
      if (_M0L7n__keepS902 > 0 && _M0L7n__keepS902 <= _M0L4rowsS876) {
        int32_t _M0L7_2abindS903 = 0;
        int32_t _M0L1jS904 = _M0L7_2abindS903;
        while (1) {
          if (_M0L1jS904 < _M0L4colsS880) {
            int32_t* _M0L6_2atmpS2397 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS905 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS906;
            int32_t _M0L1kS907;
            int32_t _M0L7n__dropS909;
            int32_t _M0L7_2abindS910;
            int32_t _M0L1kS911;
            int32_t _M0L7_2abindS917;
            int32_t _M0L1kS918;
            int32_t _M0L6_2atmpS2398;
            Moonbit_object_header(_M0L8pre__idxS905)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS905->$0 = _M0L6_2atmpS2397;
            _M0L8pre__idxS905->$1 = 0;
            _M0L7_2abindS906 = 0;
            _M0L1kS907 = _M0L7_2abindS906;
            while (1) {
              if (_M0L1kS907 < _M0L4rowsS876) {
                int32_t _M0L6_2atmpS2386;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS905, _M0L1kS907);
                _M0L6_2atmpS2386 = _M0L1kS907 + 1;
                _M0L1kS907 = _M0L6_2atmpS2386;
                continue;
              }
              break;
            }
            _M0L7n__dropS909 = _M0L4rowsS876 - _M0L7n__keepS902;
            _M0L7_2abindS910 = 0;
            _M0L1kS911 = _M0L7_2abindS910;
            while (1) {
              if (_M0L1kS911 < _M0L7n__dropS909) {
                float _M0L1uS912;
                float _M0L6_2atmpS2390;
                float _M0L6_2atmpS2392;
                float _M0L6_2atmpS2391;
                float _M0L6_2atmpS2389;
                int32_t _M0L6_2atmpS2388;
                int32_t _M0L6r__idxS913;
                int32_t _M0L10r__clampedS914;
                int32_t _M0L3tmpS915;
                int32_t _M0L6_2atmpS2387;
                int32_t _M0L6_2atmpS2393;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS912 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
                _M0L6_2atmpS2390 = (float)_M0L4rowsS876;
                _M0L6_2atmpS2392 = (float)_M0L1kS911;
                _M0L6_2atmpS2391 = _M0L6_2atmpS2392 * _M0L1uS912;
                _M0L6_2atmpS2389 = _M0L6_2atmpS2390 - _M0L6_2atmpS2391;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2388
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2389);
                _M0L6r__idxS913 = _M0L1kS911 + _M0L6_2atmpS2388;
                if (_M0L6r__idxS913 >= _M0L4rowsS876) {
                  _M0L10r__clampedS914 = _M0L4rowsS876 - 1;
                } else {
                  _M0L10r__clampedS914 = _M0L6r__idxS913;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS915
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L1kS911);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2387
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L10r__clampedS914);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS905, _M0L1kS911, _M0L6_2atmpS2387);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS905, _M0L10r__clampedS914, _M0L3tmpS915);
                _M0L6_2atmpS2393 = _M0L1kS911 + 1;
                _M0L1kS911 = _M0L6_2atmpS2393;
                continue;
              }
              break;
            }
            _M0L7_2abindS917 = 0;
            _M0L1kS918 = _M0L7_2abindS917;
            while (1) {
              if (_M0L1kS918 < _M0L7n__dropS909) {
                int32_t _M0L6_2atmpS2395;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2394;
                int32_t _M0L6_2atmpS2396;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2395
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L1kS918);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2394
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L6_2atmpS2395);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2394, _M0L1jS904, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2394);
                _M0L6_2atmpS2396 = _M0L1kS918 + 1;
                _M0L1kS918 = _M0L6_2atmpS2396;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS905);
              }
              break;
            }
            _M0L6_2atmpS2398 = _M0L1jS904 + 1;
            _M0L1jS904 = _M0L6_2atmpS2398;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS902 == 0) {
        int32_t _M0L7_2abindS921 = 0;
        int32_t _M0L1iS922 = _M0L7_2abindS921;
        while (1) {
          if (_M0L1iS922 < _M0L4rowsS876) {
            int32_t _M0L7_2abindS923 = 0;
            int32_t _M0L1jS924 = _M0L7_2abindS923;
            int32_t _M0L6_2atmpS2401;
            while (1) {
              if (_M0L1jS924 < _M0L4colsS880) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2399;
                int32_t _M0L6_2atmpS2400;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2399
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS922);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2399, _M0L1jS924, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2399);
                _M0L6_2atmpS2400 = _M0L1jS924 + 1;
                _M0L1jS924 = _M0L6_2atmpS2400;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2401 = _M0L1iS922 + 1;
            _M0L1iS922 = _M0L6_2atmpS2401;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2421 = (float)_M0L4colsS880;
      float _M0L6_2atmpS2420 = _M0L6_2atmpS2421 * _M0L1pS899;
      int32_t _M0L7n__keepS927;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS927 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2420);
      if (_M0L7n__keepS927 > 0 && _M0L7n__keepS927 <= _M0L4colsS880) {
        int32_t _M0L7_2abindS928 = 0;
        int32_t _M0L1iS929 = _M0L7_2abindS928;
        while (1) {
          if (_M0L1iS929 < _M0L4rowsS876) {
            int32_t* _M0L6_2atmpS2415 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS930 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS931;
            int32_t _M0L1kS932;
            int32_t _M0L7n__dropS934;
            int32_t _M0L7_2abindS935;
            int32_t _M0L1kS936;
            int32_t _M0L7_2abindS942;
            int32_t _M0L1kS943;
            int32_t _M0L6_2atmpS2416;
            Moonbit_object_header(_M0L9post__idxS930)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS930->$0 = _M0L6_2atmpS2415;
            _M0L9post__idxS930->$1 = 0;
            _M0L7_2abindS931 = 0;
            _M0L1kS932 = _M0L7_2abindS931;
            while (1) {
              if (_M0L1kS932 < _M0L4colsS880) {
                int32_t _M0L6_2atmpS2404;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS930, _M0L1kS932);
                _M0L6_2atmpS2404 = _M0L1kS932 + 1;
                _M0L1kS932 = _M0L6_2atmpS2404;
                continue;
              }
              break;
            }
            _M0L7n__dropS934 = _M0L4colsS880 - _M0L7n__keepS927;
            _M0L7_2abindS935 = 0;
            _M0L1kS936 = _M0L7_2abindS935;
            while (1) {
              if (_M0L1kS936 < _M0L7n__dropS934) {
                float _M0L1uS937;
                float _M0L6_2atmpS2408;
                float _M0L6_2atmpS2410;
                float _M0L6_2atmpS2409;
                float _M0L6_2atmpS2407;
                int32_t _M0L6_2atmpS2406;
                int32_t _M0L6r__idxS938;
                int32_t _M0L10r__clampedS939;
                int32_t _M0L3tmpS940;
                int32_t _M0L6_2atmpS2405;
                int32_t _M0L6_2atmpS2411;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS937 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
                _M0L6_2atmpS2408 = (float)_M0L4colsS880;
                _M0L6_2atmpS2410 = (float)_M0L1kS936;
                _M0L6_2atmpS2409 = _M0L6_2atmpS2410 * _M0L1uS937;
                _M0L6_2atmpS2407 = _M0L6_2atmpS2408 - _M0L6_2atmpS2409;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2406
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2407);
                _M0L6r__idxS938 = _M0L1kS936 + _M0L6_2atmpS2406;
                if (_M0L6r__idxS938 >= _M0L4colsS880) {
                  _M0L10r__clampedS939 = _M0L4colsS880 - 1;
                } else {
                  _M0L10r__clampedS939 = _M0L6r__idxS938;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS940
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L1kS936);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2405
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L10r__clampedS939);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS930, _M0L1kS936, _M0L6_2atmpS2405);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS930, _M0L10r__clampedS939, _M0L3tmpS940);
                _M0L6_2atmpS2411 = _M0L1kS936 + 1;
                _M0L1kS936 = _M0L6_2atmpS2411;
                continue;
              }
              break;
            }
            _M0L7_2abindS942 = 0;
            _M0L1kS943 = _M0L7_2abindS942;
            while (1) {
              if (_M0L1kS943 < _M0L7n__dropS934) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2412;
                int32_t _M0L6_2atmpS2413;
                int32_t _M0L6_2atmpS2414;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2412
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS929);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2413
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L1kS943);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2412, _M0L6_2atmpS2413, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2412);
                _M0L6_2atmpS2414 = _M0L1kS943 + 1;
                _M0L1kS943 = _M0L6_2atmpS2414;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS930);
              }
              break;
            }
            _M0L6_2atmpS2416 = _M0L1iS929 + 1;
            _M0L1iS929 = _M0L6_2atmpS2416;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS927 == 0) {
        int32_t _M0L7_2abindS946 = 0;
        int32_t _M0L1iS947 = _M0L7_2abindS946;
        while (1) {
          if (_M0L1iS947 < _M0L4rowsS876) {
            int32_t _M0L7_2abindS948 = 0;
            int32_t _M0L1jS949 = _M0L7_2abindS948;
            int32_t _M0L6_2atmpS2419;
            while (1) {
              if (_M0L1jS949 < _M0L4colsS880) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2417;
                int32_t _M0L6_2atmpS2418;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2417
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS947);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2417, _M0L1jS949, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2417);
                _M0L6_2atmpS2418 = _M0L1jS949 + 1;
                _M0L1jS949 = _M0L6_2atmpS2418;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2419 = _M0L1iS947 + 1;
            _M0L1iS947 = _M0L6_2atmpS2419;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2429 = _M0L4rowsS876 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS952 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2429, 0);
  _M0L6_2atmpS2428 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS953
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS953)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS953->$0 = _M0L6_2atmpS2428;
  _M0L6colptrS953->$1 = 0;
  _M0L6_2atmpS2427 = moonbit_empty_float_array;
  _M0L4valsS954
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS954->$0 = _M0L6_2atmpS2427;
  _M0L4valsS954->$1 = 0;
  _M0L7_2abindS955 = 0;
  _M0L1iS956 = _M0L7_2abindS955;
  while (1) {
    if (_M0L1iS956 < _M0L4rowsS876) {
      int32_t _M0L6_2atmpS2422;
      int32_t _M0L7_2abindS957;
      int32_t _M0L1jS958;
      int32_t _M0L6_2atmpS2425;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2422 = _M0MPC15array5Array6lengthGfE(_M0L4valsS954);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS952, _M0L1iS956, _M0L6_2atmpS2422);
      _M0L7_2abindS957 = 0;
      _M0L1jS958 = _M0L7_2abindS957;
      while (1) {
        if (_M0L1jS958 < _M0L4colsS880) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2423;
          float _M0L1vS959;
          int32_t _M0L6_2atmpS2424;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2423
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS956);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS959
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2423, _M0L1jS958);
          moonbit_decref_cycle_free(_M0L6_2atmpS2423);
          if (_M0L1vS959 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS953, _M0L1jS958);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS954, _M0L1vS959);
          }
          _M0L6_2atmpS2424 = _M0L1jS958 + 1;
          _M0L1jS958 = _M0L6_2atmpS2424;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2425 = _M0L1iS956 + 1;
      _M0L1iS956 = _M0L6_2atmpS2425;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS875);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2426 = _M0MPC15array5Array6lengthGfE(_M0L4valsS954);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS952, _M0L4rowsS876, _M0L6_2atmpS2426);
  _block_3556
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_3556)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 114, 0);
  _block_3556->$0 = _M0L4rowsS876;
  _block_3556->$1 = _M0L4colsS880;
  _block_3556->$2 = _M0L6rowptrS952;
  _block_3556->$3 = _M0L6colptrS953;
  _block_3556->$4 = _M0L4valsS954;
  return _block_3556;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS873
) {
  struct _M0TUmmmmE* _M0L1sS872;
  uint64_t _M0L6_2atmpS2378;
  struct _M0TUmmmmE* _M0L1tS874;
  uint64_t _M0L6_2atmpS2374;
  uint64_t _M0L6_2atmpS2375;
  uint64_t _M0L6_2atmpS2376;
  uint64_t _M0L6_2atmpS2377;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_3557;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS872 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS873);
  _M0L6_2atmpS2378 = _M0L1sS872->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS874 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2378);
  _M0L6_2atmpS2374 = _M0L1sS872->$0;
  _M0L6_2atmpS2375 = _M0L1sS872->$1;
  _M0L6_2atmpS2376 = _M0L1sS872->$2;
  moonbit_decref_cycle_free(_M0L1sS872);
  _M0L6_2atmpS2377 = _M0L1tS874->$0;
  moonbit_decref_cycle_free(_M0L1tS874);
  _block_3557
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_3557)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3557->$0 = _M0L6_2atmpS2374;
  _block_3557->$1 = _M0L6_2atmpS2375;
  _block_3557->$2 = _M0L6_2atmpS2376;
  _block_3557->$3 = _M0L6_2atmpS2377;
  return _block_3557;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS864) {
  uint64_t _M0L2s1S863;
  uint64_t _M0L2z1S865;
  uint64_t _M0L2s2S866;
  uint64_t _M0L2z2S867;
  uint64_t _M0L2s3S868;
  uint64_t _M0L2z3S869;
  uint64_t _M0L2s4S870;
  uint64_t _M0L2z4S871;
  struct _M0TUmmmmE* _block_3558;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S863 = _M0L4seedS864 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S865 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S863);
  _M0L2s2S866 = _M0L2s1S863 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S867 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S866);
  _M0L2s3S868 = _M0L2s2S866 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S869 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S868);
  _M0L2s4S870 = _M0L2s3S868 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S871 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S870);
  _block_3558 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_3558)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3558->$0 = _M0L2z1S865;
  _block_3558->$1 = _M0L2z2S867;
  _block_3558->$2 = _M0L2z3S869;
  _block_3558->$3 = _M0L2z4S871;
  return _block_3558;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS861) {
  uint64_t _M0L6_2atmpS2373;
  uint64_t _M0L6_2atmpS2372;
  uint64_t _M0L1zS860;
  uint64_t _M0L6_2atmpS2371;
  uint64_t _M0L6_2atmpS2370;
  uint64_t _M0L1zS862;
  uint64_t _M0L6_2atmpS2369;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2373 = _M0L1zS861 >> 30;
  _M0L6_2atmpS2372 = _M0L1zS861 ^ _M0L6_2atmpS2373;
  _M0L1zS860 = _M0L6_2atmpS2372 * 13787848793156543929ull;
  _M0L6_2atmpS2371 = _M0L1zS860 >> 27;
  _M0L6_2atmpS2370 = _M0L1zS860 ^ _M0L6_2atmpS2371;
  _M0L1zS862 = _M0L6_2atmpS2370 * 10723151780598845931ull;
  _M0L6_2atmpS2369 = _M0L1zS862 >> 31;
  return _M0L1zS862 ^ _M0L6_2atmpS2369;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS855
) {
  double _M0L2u1S854;
  double _M0L8u1__safeS856;
  double _M0L2u2S857;
  double _M0L6_2atmpS2368;
  double _M0L6_2atmpS2367;
  double _M0L1rS858;
  double _M0L5thetaS859;
  double _M0L6_2atmpS2366;
  double _M0L6_2atmpS2363;
  double _M0L6_2atmpS2365;
  double _M0L6_2atmpS2364;
  struct _M0TUddE* _block_3559;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S854 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS855);
  if (_M0L2u1S854 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS856 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS856 = _M0L2u1S854;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S857 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS855);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2368 = _M0FPC14math2ln(_M0L8u1__safeS856);
  _M0L6_2atmpS2367 = -0x1p+1 * _M0L6_2atmpS2368;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS858 = sqrt(_M0L6_2atmpS2367);
  _M0L5thetaS859 = 0x1.921fb54442d18p+2 * _M0L2u2S857;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2366 = _M0FPC14math3cos(_M0L5thetaS859);
  _M0L6_2atmpS2363 = _M0L1rS858 * _M0L6_2atmpS2366;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2365 = _M0FPC14math3sin(_M0L5thetaS859);
  _M0L6_2atmpS2364 = _M0L1rS858 * _M0L6_2atmpS2365;
  _block_3559 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_3559)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3559->$0 = _M0L6_2atmpS2363;
  _block_3559->$1 = _M0L6_2atmpS2364;
  return _block_3559;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS852
) {
  uint64_t _M0L1uS851;
  uint64_t _M0L4bitsS853;
  double _M0L6_2atmpS2362;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS851 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS852);
  _M0L4bitsS853 = _M0L1uS851 >> 11;
  _M0L6_2atmpS2362 = (double)_M0L4bitsS853;
  return _M0L6_2atmpS2362 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS849
) {
  uint32_t _M0L1uS848;
  uint32_t _M0L4bitsS850;
  double _M0L6_2atmpS2361;
  double _M0L6_2atmpS2360;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS848 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS849);
  _M0L4bitsS850 = _M0L1uS848 >> 8;
  _M0L6_2atmpS2361 = (double)_M0L4bitsS850;
  _M0L6_2atmpS2360 = _M0L6_2atmpS2361 * 0x1p-24;
  return (float)_M0L6_2atmpS2360;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS847
) {
  uint64_t _M0L1uS846;
  uint64_t _M0L6_2atmpS2359;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS846 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS847);
  _M0L6_2atmpS2359 = _M0L1uS846 >> 32;
  return (uint32_t)_M0L6_2atmpS2359;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS839
) {
  uint64_t _M0L2s0S838;
  uint64_t _M0L2s1S840;
  uint64_t _M0L2s2S841;
  uint64_t _M0L2s3S842;
  uint64_t _M0L3tmpS843;
  uint64_t _M0L6_2atmpS2358;
  uint64_t _M0L3resS844;
  uint64_t _M0L1tS845;
  uint64_t _M0L6_2atmpS2348;
  uint64_t _M0L6_2atmpS2349;
  uint64_t _M0L2s2S2351;
  uint64_t _M0L6_2atmpS2350;
  uint64_t _M0L2s3S2353;
  uint64_t _M0L6_2atmpS2352;
  uint64_t _M0L2s2S2355;
  uint64_t _M0L6_2atmpS2354;
  uint64_t _M0L2s3S2357;
  uint64_t _M0L6_2atmpS2356;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S838 = _M0L1rS839->$0;
  _M0L2s1S840 = _M0L1rS839->$1;
  _M0L2s2S841 = _M0L1rS839->$2;
  _M0L2s3S842 = _M0L1rS839->$3;
  _M0L3tmpS843 = _M0L2s0S838 + _M0L2s3S842;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2358 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS843, 23);
  _M0L3resS844 = _M0L6_2atmpS2358 + _M0L2s0S838;
  _M0L1tS845 = _M0L2s1S840 << 17;
  _M0L6_2atmpS2348 = _M0L2s2S841 ^ _M0L2s0S838;
  _M0L1rS839->$2 = _M0L6_2atmpS2348;
  _M0L6_2atmpS2349 = _M0L2s3S842 ^ _M0L2s1S840;
  _M0L1rS839->$3 = _M0L6_2atmpS2349;
  _M0L2s2S2351 = _M0L1rS839->$2;
  _M0L6_2atmpS2350 = _M0L2s1S840 ^ _M0L2s2S2351;
  _M0L1rS839->$1 = _M0L6_2atmpS2350;
  _M0L2s3S2353 = _M0L1rS839->$3;
  _M0L6_2atmpS2352 = _M0L2s0S838 ^ _M0L2s3S2353;
  _M0L1rS839->$0 = _M0L6_2atmpS2352;
  _M0L2s2S2355 = _M0L1rS839->$2;
  _M0L6_2atmpS2354 = _M0L2s2S2355 ^ _M0L1tS845;
  _M0L1rS839->$2 = _M0L6_2atmpS2354;
  _M0L2s3S2357 = _M0L1rS839->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2356 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2357, 45);
  _M0L1rS839->$3 = _M0L6_2atmpS2356;
  return _M0L3resS844;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS836, int32_t _M0L1kS837) {
  uint64_t _M0L6_2atmpS2345;
  int32_t _M0L6_2atmpS2347;
  uint64_t _M0L6_2atmpS2346;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2345 = _M0L1xS836 << (_M0L1kS837 & 63);
  _M0L6_2atmpS2347 = 64 - _M0L1kS837;
  _M0L6_2atmpS2346 = _M0L1xS836 >> (_M0L6_2atmpS2347 & 63);
  return _M0L6_2atmpS2345 | _M0L6_2atmpS2346;
}

double _M0FPC14math2ln(double _M0L1xS822) {
  struct _M0TUdiE* _M0L7_2abindS823;
  double _M0L5_2af1S824;
  int32_t _M0L5_2akiS825;
  double _M0L1fS827;
  double _M0L1kS828;
  double _M0L6_2atmpS2338;
  double _M0L1sS829;
  double _M0L2s2S830;
  double _M0L2s4S831;
  double _M0L6_2atmpS2337;
  double _M0L6_2atmpS2336;
  double _M0L6_2atmpS2335;
  double _M0L6_2atmpS2334;
  double _M0L6_2atmpS2333;
  double _M0L6_2atmpS2332;
  double _M0L2t1S832;
  double _M0L6_2atmpS2331;
  double _M0L6_2atmpS2330;
  double _M0L6_2atmpS2329;
  double _M0L6_2atmpS2328;
  double _M0L2t2S833;
  double _M0L1rS834;
  double _M0L6_2atmpS2327;
  double _M0L4hfsqS835;
  double _M0L6_2atmpS2320;
  double _M0L6_2atmpS2326;
  double _M0L6_2atmpS2324;
  double _M0L6_2atmpS2325;
  double _M0L6_2atmpS2323;
  double _M0L6_2atmpS2322;
  double _M0L6_2atmpS2321;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS822 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS822)
      || _M0MPC16double6Double7is__inf(_M0L1xS822)
    ) {
      return _M0L1xS822;
    } else if (_M0L1xS822 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS823 = _M0FPC14math5frexp(_M0L1xS822);
  _M0L5_2af1S824 = _M0L7_2abindS823->$0;
  _M0L5_2akiS825 = _M0L7_2abindS823->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS823);
  if (_M0L5_2af1S824 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2342 = _M0L5_2af1S824 * 0x1p+1;
    double _M0L6_2atmpS2339 = _M0L6_2atmpS2342 - 0x1p+0;
    int32_t _M0L6_2atmpS2341 = _M0L5_2akiS825 - 1;
    double _M0L6_2atmpS2340 = (double)_M0L6_2atmpS2341;
    _M0L1fS827 = _M0L6_2atmpS2339;
    _M0L1kS828 = _M0L6_2atmpS2340;
    goto join_826;
  } else {
    double _M0L6_2atmpS2343 = _M0L5_2af1S824 - 0x1p+0;
    double _M0L6_2atmpS2344 = (double)_M0L5_2akiS825;
    _M0L1fS827 = _M0L6_2atmpS2343;
    _M0L1kS828 = _M0L6_2atmpS2344;
    goto join_826;
  }
  join_826:;
  _M0L6_2atmpS2338 = 0x1p+1 + _M0L1fS827;
  _M0L1sS829 = _M0L1fS827 / _M0L6_2atmpS2338;
  _M0L2s2S830 = _M0L1sS829 * _M0L1sS829;
  _M0L2s4S831 = _M0L2s2S830 * _M0L2s2S830;
  _M0L6_2atmpS2337 = _M0L2s4S831 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2336 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2337;
  _M0L6_2atmpS2335 = _M0L2s4S831 * _M0L6_2atmpS2336;
  _M0L6_2atmpS2334 = 0x1.2492494229359p-2 + _M0L6_2atmpS2335;
  _M0L6_2atmpS2333 = _M0L2s4S831 * _M0L6_2atmpS2334;
  _M0L6_2atmpS2332 = 0x1.5555555555593p-1 + _M0L6_2atmpS2333;
  _M0L2t1S832 = _M0L2s2S830 * _M0L6_2atmpS2332;
  _M0L6_2atmpS2331 = _M0L2s4S831 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2330 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2331;
  _M0L6_2atmpS2329 = _M0L2s4S831 * _M0L6_2atmpS2330;
  _M0L6_2atmpS2328 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2329;
  _M0L2t2S833 = _M0L2s4S831 * _M0L6_2atmpS2328;
  _M0L1rS834 = _M0L2t1S832 + _M0L2t2S833;
  _M0L6_2atmpS2327 = 0x1p-1 * _M0L1fS827;
  _M0L4hfsqS835 = _M0L6_2atmpS2327 * _M0L1fS827;
  _M0L6_2atmpS2320 = _M0L1kS828 * 0x1.62e42feep-1;
  _M0L6_2atmpS2326 = _M0L4hfsqS835 + _M0L1rS834;
  _M0L6_2atmpS2324 = _M0L1sS829 * _M0L6_2atmpS2326;
  _M0L6_2atmpS2325 = _M0L1kS828 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2323 = _M0L6_2atmpS2324 + _M0L6_2atmpS2325;
  _M0L6_2atmpS2322 = _M0L4hfsqS835 - _M0L6_2atmpS2323;
  _M0L6_2atmpS2321 = _M0L6_2atmpS2322 - _M0L1fS827;
  return _M0L6_2atmpS2320 - _M0L6_2atmpS2321;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS815) {
  struct _M0TUdiE* _M0L7_2abindS816;
  double _M0L10_2anorm__fS817;
  int32_t _M0L6_2aexpS818;
  uint64_t _M0L1uS819;
  uint64_t _M0L6_2atmpS2319;
  uint64_t _M0L6_2atmpS2318;
  int32_t _M0L6_2atmpS2317;
  int32_t _M0L6_2atmpS2316;
  int32_t _M0L3expS820;
  uint64_t _M0L6_2atmpS2315;
  uint64_t _M0L6_2atmpS2314;
  uint64_t _M0L6_2atmpS2313;
  double _M0L4fracS821;
  struct _M0TUdiE* _block_3562;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS815 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS815)
    || _M0MPC16double6Double7is__nan(_M0L1fS815)
  ) {
    struct _M0TUdiE* _block_3561 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3561)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3561->$0 = _M0L1fS815;
    _block_3561->$1 = 0;
    return _block_3561;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS816 = _M0FPC14math9normalize(_M0L1fS815);
  _M0L10_2anorm__fS817 = _M0L7_2abindS816->$0;
  _M0L6_2aexpS818 = _M0L7_2abindS816->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS816);
  _M0L1uS819 = *(int64_t*)&_M0L10_2anorm__fS817;
  _M0L6_2atmpS2319 = _M0L1uS819 >> 52;
  _M0L6_2atmpS2318 = _M0L6_2atmpS2319 & 2047ull;
  _M0L6_2atmpS2317 = (int32_t)_M0L6_2atmpS2318;
  _M0L6_2atmpS2316 = _M0L6_2aexpS818 + _M0L6_2atmpS2317;
  _M0L3expS820 = _M0L6_2atmpS2316 - 1022;
  _M0L6_2atmpS2315 = ~9218868437227405312ull;
  _M0L6_2atmpS2314 = _M0L1uS819 & _M0L6_2atmpS2315;
  _M0L6_2atmpS2313 = _M0L6_2atmpS2314 | 4602678819172646912ull;
  _M0L4fracS821 = *(double*)&_M0L6_2atmpS2313;
  _block_3562 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3562)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3562->$0 = _M0L4fracS821;
  _block_3562->$1 = _M0L3expS820;
  return _block_3562;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS814) {
  double _M0L6_2atmpS2310;
  struct _M0TUdiE* _block_3564;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2310 = fabs(_M0L1fS814);
  if (_M0L6_2atmpS2310 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2312 = (double)4503599627370496ll;
    double _M0L6_2atmpS2311 = _M0L1fS814 * _M0L6_2atmpS2312;
    struct _M0TUdiE* _block_3563 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3563)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3563->$0 = _M0L6_2atmpS2311;
    _block_3563->$1 = -52;
    return _block_3563;
  }
  _block_3564 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3564)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3564->$0 = _M0L1fS814;
  _block_3564->$1 = 0;
  return _block_3564;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS813) {
  double _M0L6_2atmpS2309;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2309 = (double)_M0L4selfS813;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2309);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS812) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS812 != _M0L4selfS812) {
    return 0;
  } else if (_M0L4selfS812 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS812 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS812;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS793,
  float _M0L4elemS795
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS792;
  int32_t _M0L1iS794;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS792 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS793);
  _M0L1iS794 = 0;
  while (1) {
    if (_M0L1iS794 < _M0L3lenS793) {
      float* _M0L3bufS2301 = _M0L3arrS792->$0;
      int32_t _M0L6_2atmpS2302;
      _M0L3bufS2301[_M0L1iS794] = _M0L4elemS795;
      _M0L6_2atmpS2302 = _M0L1iS794 + 1;
      _M0L1iS794 = _M0L6_2atmpS2302;
      continue;
    }
    break;
  }
  return _M0L3arrS792;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS798,
  int32_t _M0L4elemS800
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS797;
  int32_t _M0L1iS799;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS797 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS798);
  _M0L1iS799 = 0;
  while (1) {
    if (_M0L1iS799 < _M0L3lenS798) {
      uint8_t* _M0L3bufS2303 = _M0L3arrS797->$0;
      int32_t _M0L6_2atmpS2304;
      _M0L3bufS2303[_M0L1iS799] = _M0L4elemS800;
      _M0L6_2atmpS2304 = _M0L1iS799 + 1;
      _M0L1iS799 = _M0L6_2atmpS2304;
      continue;
    }
    break;
  }
  return _M0L3arrS797;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS803,
  int32_t _M0L4elemS805
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS802;
  int32_t _M0L1iS804;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS802 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS803);
  _M0L1iS804 = 0;
  while (1) {
    if (_M0L1iS804 < _M0L3lenS803) {
      int32_t* _M0L3bufS2305 = _M0L3arrS802->$0;
      int32_t _M0L6_2atmpS2306;
      _M0L3bufS2305[_M0L1iS804] = _M0L4elemS805;
      _M0L6_2atmpS2306 = _M0L1iS804 + 1;
      _M0L1iS804 = _M0L6_2atmpS2306;
      continue;
    }
    break;
  }
  return _M0L3arrS802;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS808,
  struct _M0TPB5ArrayGfE* _M0L4elemS810
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS807;
  int32_t _M0L1iS809;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS807
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS808);
  _M0L1iS809 = 0;
  while (1) {
    if (_M0L1iS809 < _M0L3lenS808) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2307 = _M0L3arrS807->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS3433 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2307[_M0L1iS809];
      int32_t _M0L6_2atmpS2308;
      moonbit_incref_cycle_free(_M0L4elemS810);
      if (_M0L6_2aoldS3433) {
        moonbit_decref_cycle_free(_M0L6_2aoldS3433);
      }
      _M0L3bufS2307[_M0L1iS809] = _M0L4elemS810;
      _M0L6_2atmpS2308 = _M0L1iS809 + 1;
      _M0L1iS809 = _M0L6_2atmpS2308;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS810);
    }
    break;
  }
  return _M0L3arrS807;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS777,
  int32_t _M0L5indexS778,
  float _M0L5valueS779
) {
  int32_t _M0L3lenS776;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS776 = _M0L4selfS777->$1;
  if (_M0L5indexS778 >= 0 && _M0L5indexS778 < _M0L3lenS776) {
    float* _M0L6_2atmpS2297;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2297 = _M0MPC15array5Array6bufferGfE(_M0L4selfS777);
    _M0L6_2atmpS2297[_M0L5indexS778] = _M0L5valueS779;
    moonbit_decref_cycle_free(_M0L6_2atmpS2297);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS781,
  int32_t _M0L5indexS782,
  int32_t _M0L5valueS783
) {
  int32_t _M0L3lenS780;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS780 = _M0L4selfS781->$1;
  if (_M0L5indexS782 >= 0 && _M0L5indexS782 < _M0L3lenS780) {
    uint8_t* _M0L6_2atmpS2298;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2298 = _M0MPC15array5Array6bufferGbE(_M0L4selfS781);
    _M0L6_2atmpS2298[_M0L5indexS782] = _M0L5valueS783;
    moonbit_decref_cycle_free(_M0L6_2atmpS2298);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS785,
  int32_t _M0L5indexS786,
  int32_t _M0L5valueS787
) {
  int32_t _M0L3lenS784;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L5indexS786 >= 0 && _M0L5indexS786 < _M0L3lenS784) {
    int32_t* _M0L6_2atmpS2299;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2299 = _M0MPC15array5Array6bufferGiE(_M0L4selfS785);
    _M0L6_2atmpS2299[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2299);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS789,
  int32_t _M0L5indexS790,
  struct _M0TPB5ArrayGfE* _M0L5valueS791
) {
  int32_t _M0L3lenS788;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS788 = _M0L4selfS789->$1;
  if (_M0L5indexS790 >= 0 && _M0L5indexS790 < _M0L3lenS788) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2300;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS3434;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2300
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS789);
    _M0L6_2aoldS3434
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2300[_M0L5indexS790];
    if (_M0L6_2aoldS3434) {
      moonbit_decref_cycle_free(_M0L6_2aoldS3434);
    }
    _M0L6_2atmpS2300[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2300);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS791);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    uint8_t* _M0L6_2atmpS2292;
    int32_t _result_3569;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2292 = _M0MPC15array5Array6bufferGbE(_M0L4selfS762);
    _result_3569 = (int32_t)_M0L6_2atmpS2292[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS2292);
    return _result_3569;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    int32_t* _M0L6_2atmpS2293;
    int32_t _result_3570;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2293 = _M0MPC15array5Array6bufferGiE(_M0L4selfS765);
    _result_3570 = (int32_t)_M0L6_2atmpS2293[_M0L5indexS766];
    moonbit_decref_cycle_free(_M0L6_2atmpS2293);
    return _result_3570;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    float* _M0L6_2atmpS2294;
    float _result_3571;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2294 = _M0MPC15array5Array6bufferGfE(_M0L4selfS768);
    _result_3571 = (float)_M0L6_2atmpS2294[_M0L5indexS769];
    moonbit_decref_cycle_free(_M0L6_2atmpS2294);
    return _result_3571;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS771,
  int32_t _M0L5indexS772
) {
  int32_t _M0L3lenS770;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    moonbit_string_t* _M0L6_2atmpS2295;
    moonbit_string_t _M0L6_2atmpS3435;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2295 = _M0MPC15array5Array6bufferGsE(_M0L4selfS771);
    _M0L6_2atmpS3435 = (moonbit_string_t)_M0L6_2atmpS2295[_M0L5indexS772];
    moonbit_incref_cycle_free(_M0L6_2atmpS3435);
    moonbit_decref_cycle_free(_M0L6_2atmpS2295);
    return _M0L6_2atmpS3435;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS774,
  int32_t _M0L5indexS775
) {
  int32_t _M0L3lenS773;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L5indexS775 >= 0 && _M0L5indexS775 < _M0L3lenS773) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2296;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS3436;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2296
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS774);
    _M0L6_2atmpS3436
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2296[_M0L5indexS775];
    if (_M0L6_2atmpS3436) {
      moonbit_incref_cycle_free(_M0L6_2atmpS3436);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2296);
    return _M0L6_2atmpS3436;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS760) {
  moonbit_string_t _M0L6_2atmpS2291;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2291 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS760);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2291);
  moonbit_decref_cycle_free(_M0L6_2atmpS2291);
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
  uint64_t _M0L6_2atmpS2290;
  uint64_t _M0L6_2atmpS2289;
  int32_t _M0L8ieeeSignS746;
  uint64_t _M0L12ieeeMantissaS747;
  uint64_t _M0L6_2atmpS2288;
  uint64_t _M0L6_2atmpS2287;
  int32_t _M0L12ieeeExponentS748;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS749;
  struct _M0TPB17FloatingDecimal64* _M0L1vS750;
  moonbit_string_t _result_3573;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS742 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L3valS742 >= -0x1p+53 && _M0L3valS742 <= 0x1p+53) {
    if (_M0L3valS742 >= -0x1p+31 && _M0L3valS742 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS743;
      double _M0L6_2atmpS2276;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double7to__int(_M0L3valS742);
      _M0L6_2atmpS2276 = (double)_M0L1iS743;
      if (_M0L6_2atmpS2276 == _M0L3valS742) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS743, 10);
      }
    } else {
      int64_t _M0L1iS744;
      double _M0L6_2atmpS2277;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double9to__int64(_M0L3valS742);
      _M0L6_2atmpS2277 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2277 == _M0L3valS742) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS744, 10);
      }
    }
  }
  _M0L4bitsS745 = *(int64_t*)&_M0L3valS742;
  _M0L6_2atmpS2290 = _M0L4bitsS745 >> 63;
  _M0L6_2atmpS2289 = _M0L6_2atmpS2290 & 1ull;
  _M0L8ieeeSignS746 = _M0L6_2atmpS2289 != 0ull;
  _M0L12ieeeMantissaS747 = _M0L4bitsS745 & 4503599627370495ull;
  _M0L6_2atmpS2288 = _M0L4bitsS745 >> 52;
  _M0L6_2atmpS2287 = _M0L6_2atmpS2288 & 2047ull;
  _M0L12ieeeExponentS748 = (int32_t)_M0L6_2atmpS2287;
  if (
    _M0L12ieeeExponentS748 == 2047
    || _M0L12ieeeExponentS748 == 0 && _M0L12ieeeMantissaS747 == 0ull
  ) {
    int32_t _M0L6_2atmpS2278 = _M0L12ieeeExponentS748 != 0;
    int32_t _M0L6_2atmpS2279 = _M0L12ieeeMantissaS747 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS746, _M0L6_2atmpS2278, _M0L6_2atmpS2279);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS749
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS747, _M0L12ieeeExponentS748);
  if (_M0L7_2abindS749 == 0) {
    uint32_t _M0L6_2atmpS2280;
    if (_M0L7_2abindS749) {
      moonbit_decref_cycle_free(_M0L7_2abindS749);
    }
    _M0L6_2atmpS2280 = *(uint32_t*)&_M0L12ieeeExponentS748;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS750 = _M0FPB3d2d(_M0L12ieeeMantissaS747, _M0L6_2atmpS2280);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS751 = _M0L7_2abindS749;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS752 = _M0L7_2aSomeS751;
    struct _M0TPB17FloatingDecimal64* _M0L1xS753 = _M0L4_2afS752;
    while (1) {
      uint64_t _M0L8mantissaS2286 = _M0L1xS753->$0;
      uint64_t _M0L1qS754 = _M0L8mantissaS2286 / 10ull;
      uint64_t _M0L8mantissaS2284 = _M0L1xS753->$0;
      uint64_t _M0L6_2atmpS2285 = 10ull * _M0L1qS754;
      uint64_t _M0L1rS755 = _M0L8mantissaS2284 - _M0L6_2atmpS2285;
      int32_t _M0L8exponentS2283;
      int32_t _M0L6_2atmpS2282;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2281;
      if (_M0L1rS755 != 0ull) {
        _M0L1vS750 = _M0L1xS753;
        break;
      }
      _M0L8exponentS2283 = _M0L1xS753->$1;
      moonbit_decref_cycle_free(_M0L1xS753);
      _M0L6_2atmpS2282 = _M0L8exponentS2283 + 1;
      _M0L6_2atmpS2281
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2281)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2281->$0 = _M0L1qS754;
      _M0L6_2atmpS2281->$1 = _M0L6_2atmpS2282;
      _M0L1xS753 = _M0L6_2atmpS2281;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3573 = _M0FPB9to__chars(_M0L1vS750, _M0L8ieeeSignS746);
  moonbit_decref_cycle_free(_M0L1vS750);
  return _result_3573;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS737,
  int32_t _M0L12ieeeExponentS739
) {
  uint64_t _M0L2m2S736;
  int32_t _M0L6_2atmpS2275;
  int32_t _M0L2e2S738;
  int32_t _M0L6_2atmpS2274;
  uint64_t _M0L6_2atmpS2273;
  uint64_t _M0L4maskS740;
  uint64_t _M0L8fractionS741;
  int32_t _M0L6_2atmpS2272;
  uint64_t _M0L6_2atmpS2271;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2270;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S736 = 4503599627370496ull | _M0L12ieeeMantissaS737;
  _M0L6_2atmpS2275 = _M0L12ieeeExponentS739 - 1023;
  _M0L2e2S738 = _M0L6_2atmpS2275 - 52;
  if (_M0L2e2S738 > 0) {
    return 0;
  }
  if (_M0L2e2S738 < -52) {
    return 0;
  }
  _M0L6_2atmpS2274 = -_M0L2e2S738;
  _M0L6_2atmpS2273 = 1ull << (_M0L6_2atmpS2274 & 63);
  _M0L4maskS740 = _M0L6_2atmpS2273 - 1ull;
  _M0L8fractionS741 = _M0L2m2S736 & _M0L4maskS740;
  if (_M0L8fractionS741 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2272 = -_M0L2e2S738;
  _M0L6_2atmpS2271 = _M0L2m2S736 >> (_M0L6_2atmpS2272 & 63);
  _M0L6_2atmpS2270
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2270)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2270->$0 = _M0L6_2atmpS2271;
  _M0L6_2atmpS2270->$1 = 0;
  return _M0L6_2atmpS2270;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS704,
  int32_t _M0L4signS702
) {
  moonbit_bytes_t _M0L6resultS700;
  int32_t _M0Lm5indexS701;
  uint64_t _M0L6outputS703;
  int32_t _M0L7olengthS705;
  int32_t _M0L8exponentS2269;
  int32_t _M0L6_2atmpS2268;
  int32_t _M0Lm3expS706;
  int32_t _M0L6_2atmpS2267;
  int32_t _M0L6_2atmpS2265;
  int32_t _M0L18scientificNotationS707;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS700 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS701 = 0;
  if (_M0L4signS702) {
    int32_t _M0L6_2atmpS2139 = _M0Lm5indexS701;
    int32_t _M0L6_2atmpS2140;
    if (
      _M0L6_2atmpS2139 < 0
      || _M0L6_2atmpS2139 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2139] = 45;
    _M0L6_2atmpS2140 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2140 + 1;
  }
  _M0L6outputS703 = _M0L1vS704->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS705 = _M0FPB17decimal__length17(_M0L6outputS703);
  _M0L8exponentS2269 = _M0L1vS704->$1;
  _M0L6_2atmpS2268 = _M0L8exponentS2269 + _M0L7olengthS705;
  _M0Lm3expS706 = _M0L6_2atmpS2268 - 1;
  _M0L6_2atmpS2267 = _M0Lm3expS706;
  if (_M0L6_2atmpS2267 >= -6) {
    int32_t _M0L6_2atmpS2266 = _M0Lm3expS706;
    _M0L6_2atmpS2265 = _M0L6_2atmpS2266 < 21;
  } else {
    _M0L6_2atmpS2265 = 0;
  }
  _M0L18scientificNotationS707 = !_M0L6_2atmpS2265;
  if (_M0L18scientificNotationS707) {
    int32_t _M0L7_2abindS708 = _M0L7olengthS705 - 1;
    uint64_t _M0L6outputS709;
    int32_t _M0L1iS710 = 0;
    uint64_t _M0L6outputS711 = _M0L6outputS703;
    int32_t _M0L6_2atmpS2141;
    int32_t _M0L6_2atmpS2145;
    int32_t _M0L6_2atmpS2144;
    int32_t _M0L6_2atmpS2143;
    int32_t _M0L6_2atmpS2142;
    int32_t _M0L6_2atmpS2149;
    int32_t _M0L6_2atmpS2150;
    int32_t _M0L6_2atmpS2151;
    int32_t _M0L6_2atmpS2152;
    int32_t _M0L6_2atmpS2153;
    int32_t _M0L6_2atmpS2159;
    int32_t _M0L6_2atmpS2192;
    moonbit_string_t _result_3575;
    while (1) {
      if (_M0L1iS710 < _M0L7_2abindS708) {
        uint64_t _M0L1cS712 = _M0L6outputS711 % 10ull;
        int32_t _M0L6_2atmpS2198 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2197 = _M0L6_2atmpS2198 + _M0L7olengthS705;
        int32_t _M0L6_2atmpS2193 = _M0L6_2atmpS2197 - _M0L1iS710;
        int32_t _M0L6_2atmpS2196 = (int32_t)_M0L1cS712;
        int32_t _M0L6_2atmpS2195 = 48 + _M0L6_2atmpS2196;
        int32_t _M0L6_2atmpS2194 = _M0L6_2atmpS2195 & 0xff;
        int32_t _M0L6_2atmpS2199;
        uint64_t _M0L6_2atmpS2200;
        if (
          _M0L6_2atmpS2193 < 0
          || _M0L6_2atmpS2193 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2193] = _M0L6_2atmpS2194;
        _M0L6_2atmpS2199 = _M0L1iS710 + 1;
        _M0L6_2atmpS2200 = _M0L6outputS711 / 10ull;
        _M0L1iS710 = _M0L6_2atmpS2199;
        _M0L6outputS711 = _M0L6_2atmpS2200;
        continue;
      } else {
        _M0L6outputS709 = _M0L6outputS711;
      }
      break;
    }
    _M0L6_2atmpS2141 = _M0Lm5indexS701;
    _M0L6_2atmpS2145 = (int32_t)_M0L6outputS709;
    _M0L6_2atmpS2144 = _M0L6_2atmpS2145 % 10;
    _M0L6_2atmpS2143 = 48 + _M0L6_2atmpS2144;
    _M0L6_2atmpS2142 = _M0L6_2atmpS2143 & 0xff;
    if (
      _M0L6_2atmpS2141 < 0
      || _M0L6_2atmpS2141 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2141] = _M0L6_2atmpS2142;
    if (_M0L7olengthS705 > 1) {
      int32_t _M0L6_2atmpS2147 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2146 = _M0L6_2atmpS2147 + 1;
      if (
        _M0L6_2atmpS2146 < 0
        || _M0L6_2atmpS2146 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2146] = 46;
    } else {
      int32_t _M0L6_2atmpS2148 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2148 - 1;
    }
    _M0L6_2atmpS2149 = _M0Lm5indexS701;
    _M0L6_2atmpS2150 = _M0L7olengthS705 + 1;
    _M0Lm5indexS701 = _M0L6_2atmpS2149 + _M0L6_2atmpS2150;
    _M0L6_2atmpS2151 = _M0Lm5indexS701;
    if (
      _M0L6_2atmpS2151 < 0
      || _M0L6_2atmpS2151 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2151] = 101;
    _M0L6_2atmpS2152 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2152 + 1;
    _M0L6_2atmpS2153 = _M0Lm3expS706;
    if (_M0L6_2atmpS2153 < 0) {
      int32_t _M0L6_2atmpS2154 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2155;
      int32_t _M0L6_2atmpS2156;
      if (
        _M0L6_2atmpS2154 < 0
        || _M0L6_2atmpS2154 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2154] = 45;
      _M0L6_2atmpS2155 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2155 + 1;
      _M0L6_2atmpS2156 = _M0Lm3expS706;
      _M0Lm3expS706 = -_M0L6_2atmpS2156;
    } else {
      int32_t _M0L6_2atmpS2157 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2158;
      if (
        _M0L6_2atmpS2157 < 0
        || _M0L6_2atmpS2157 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2157] = 43;
      _M0L6_2atmpS2158 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2158 + 1;
    }
    _M0L6_2atmpS2159 = _M0Lm3expS706;
    if (_M0L6_2atmpS2159 >= 100) {
      int32_t _M0L6_2atmpS2175 = _M0Lm3expS706;
      int32_t _M0L1aS714 = _M0L6_2atmpS2175 / 100;
      int32_t _M0L6_2atmpS2174 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2173 = _M0L6_2atmpS2174 / 10;
      int32_t _M0L1bS715 = _M0L6_2atmpS2173 % 10;
      int32_t _M0L6_2atmpS2172 = _M0Lm3expS706;
      int32_t _M0L1cS716 = _M0L6_2atmpS2172 % 10;
      int32_t _M0L6_2atmpS2160 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2162 = 48 + _M0L1aS714;
      int32_t _M0L6_2atmpS2161 = _M0L6_2atmpS2162 & 0xff;
      int32_t _M0L6_2atmpS2166;
      int32_t _M0L6_2atmpS2163;
      int32_t _M0L6_2atmpS2165;
      int32_t _M0L6_2atmpS2164;
      int32_t _M0L6_2atmpS2170;
      int32_t _M0L6_2atmpS2167;
      int32_t _M0L6_2atmpS2169;
      int32_t _M0L6_2atmpS2168;
      int32_t _M0L6_2atmpS2171;
      if (
        _M0L6_2atmpS2160 < 0
        || _M0L6_2atmpS2160 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2160] = _M0L6_2atmpS2161;
      _M0L6_2atmpS2166 = _M0Lm5indexS701;
      _M0L6_2atmpS2163 = _M0L6_2atmpS2166 + 1;
      _M0L6_2atmpS2165 = 48 + _M0L1bS715;
      _M0L6_2atmpS2164 = _M0L6_2atmpS2165 & 0xff;
      if (
        _M0L6_2atmpS2163 < 0
        || _M0L6_2atmpS2163 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2163] = _M0L6_2atmpS2164;
      _M0L6_2atmpS2170 = _M0Lm5indexS701;
      _M0L6_2atmpS2167 = _M0L6_2atmpS2170 + 2;
      _M0L6_2atmpS2169 = 48 + _M0L1cS716;
      _M0L6_2atmpS2168 = _M0L6_2atmpS2169 & 0xff;
      if (
        _M0L6_2atmpS2167 < 0
        || _M0L6_2atmpS2167 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2167] = _M0L6_2atmpS2168;
      _M0L6_2atmpS2171 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2171 + 3;
    } else {
      int32_t _M0L6_2atmpS2176 = _M0Lm3expS706;
      if (_M0L6_2atmpS2176 >= 10) {
        int32_t _M0L6_2atmpS2186 = _M0Lm3expS706;
        int32_t _M0L1aS717 = _M0L6_2atmpS2186 / 10;
        int32_t _M0L6_2atmpS2185 = _M0Lm3expS706;
        int32_t _M0L1bS718 = _M0L6_2atmpS2185 % 10;
        int32_t _M0L6_2atmpS2177 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2179 = 48 + _M0L1aS717;
        int32_t _M0L6_2atmpS2178 = _M0L6_2atmpS2179 & 0xff;
        int32_t _M0L6_2atmpS2183;
        int32_t _M0L6_2atmpS2180;
        int32_t _M0L6_2atmpS2182;
        int32_t _M0L6_2atmpS2181;
        int32_t _M0L6_2atmpS2184;
        if (
          _M0L6_2atmpS2177 < 0
          || _M0L6_2atmpS2177 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2177] = _M0L6_2atmpS2178;
        _M0L6_2atmpS2183 = _M0Lm5indexS701;
        _M0L6_2atmpS2180 = _M0L6_2atmpS2183 + 1;
        _M0L6_2atmpS2182 = 48 + _M0L1bS718;
        _M0L6_2atmpS2181 = _M0L6_2atmpS2182 & 0xff;
        if (
          _M0L6_2atmpS2180 < 0
          || _M0L6_2atmpS2180 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2180] = _M0L6_2atmpS2181;
        _M0L6_2atmpS2184 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2184 + 2;
      } else {
        int32_t _M0L6_2atmpS2187 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2190 = _M0Lm3expS706;
        int32_t _M0L6_2atmpS2189 = 48 + _M0L6_2atmpS2190;
        int32_t _M0L6_2atmpS2188 = _M0L6_2atmpS2189 & 0xff;
        int32_t _M0L6_2atmpS2191;
        if (
          _M0L6_2atmpS2187 < 0
          || _M0L6_2atmpS2187 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2187] = _M0L6_2atmpS2188;
        _M0L6_2atmpS2191 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2191 + 1;
      }
    }
    _M0L6_2atmpS2192 = _M0Lm5indexS701;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3575
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2192);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_3575;
  } else {
    int32_t _M0L6_2atmpS2201 = _M0Lm3expS706;
    int32_t _M0L6_2atmpS2264;
    moonbit_string_t _result_3581;
    if (_M0L6_2atmpS2201 < 0) {
      int32_t _M0L6_2atmpS2202 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2204;
      int32_t _M0L6_2atmpS2203;
      int32_t _M0L6_2atmpS2205;
      int32_t _M0L1iS719;
      int32_t _M0L6_2atmpS2220;
      int32_t _M0L6_2atmpS2222;
      int32_t _M0L6_2atmpS2221;
      int32_t _M0L7currentS721;
      int32_t _M0L1iS722;
      uint64_t _M0L6outputS723;
      if (
        _M0L6_2atmpS2202 < 0
        || _M0L6_2atmpS2202 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2202] = 48;
      _M0L6_2atmpS2204 = _M0Lm5indexS701;
      _M0L6_2atmpS2203 = _M0L6_2atmpS2204 + 1;
      if (
        _M0L6_2atmpS2203 < 0
        || _M0L6_2atmpS2203 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2203] = 46;
      _M0L6_2atmpS2205 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2205 + 2;
      _M0L1iS719 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2206 = _M0Lm3expS706;
        if (_M0L1iS719 > _M0L6_2atmpS2206) {
          int32_t _M0L6_2atmpS2209 = _M0Lm5indexS701;
          int32_t _M0L6_2atmpS2208 = _M0L6_2atmpS2209 - _M0L1iS719;
          int32_t _M0L6_2atmpS2207 = _M0L6_2atmpS2208 - 1;
          int32_t _M0L6_2atmpS2210;
          if (
            _M0L6_2atmpS2207 < 0
            || _M0L6_2atmpS2207 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2207] = 48;
          _M0L6_2atmpS2210 = _M0L1iS719 - 1;
          _M0L1iS719 = _M0L6_2atmpS2210;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2220 = _M0Lm5indexS701;
      _M0L6_2atmpS2222 = _M0Lm3expS706;
      _M0L6_2atmpS2221 = -1 - _M0L6_2atmpS2222;
      _M0L7currentS721 = _M0L6_2atmpS2220 + _M0L6_2atmpS2221;
      _M0L1iS722 = 0;
      _M0L6outputS723 = _M0L6outputS703;
      while (1) {
        if (_M0L1iS722 < _M0L7olengthS705) {
          int32_t _M0L6_2atmpS2217 = _M0L7currentS721 + _M0L7olengthS705;
          int32_t _M0L6_2atmpS2216 = _M0L6_2atmpS2217 - _M0L1iS722;
          int32_t _M0L6_2atmpS2211 = _M0L6_2atmpS2216 - 1;
          uint64_t _M0L6_2atmpS2215 = _M0L6outputS723 % 10ull;
          int32_t _M0L6_2atmpS2214 = (int32_t)_M0L6_2atmpS2215;
          int32_t _M0L6_2atmpS2213 = 48 + _M0L6_2atmpS2214;
          int32_t _M0L6_2atmpS2212 = _M0L6_2atmpS2213 & 0xff;
          int32_t _M0L6_2atmpS2218;
          uint64_t _M0L6_2atmpS2219;
          if (
            _M0L6_2atmpS2211 < 0
            || _M0L6_2atmpS2211 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2211] = _M0L6_2atmpS2212;
          _M0L6_2atmpS2218 = _M0L1iS722 + 1;
          _M0L6_2atmpS2219 = _M0L6outputS723 / 10ull;
          _M0L1iS722 = _M0L6_2atmpS2218;
          _M0L6outputS723 = _M0L6_2atmpS2219;
          continue;
        }
        break;
      }
      _M0Lm5indexS701 = _M0L7currentS721 + _M0L7olengthS705;
    } else {
      int32_t _M0L6_2atmpS2224 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2223 = _M0L6_2atmpS2224 + 1;
      if (_M0L6_2atmpS2223 >= _M0L7olengthS705) {
        int32_t _M0L1iS725 = 0;
        uint64_t _M0L6outputS726 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2235;
        int32_t _M0L6_2atmpS2240;
        int32_t _M0L7_2abindS728;
        int32_t _M0L1iS729;
        int32_t _M0L6_2atmpS2241;
        int32_t _M0L6_2atmpS2244;
        int32_t _M0L6_2atmpS2243;
        int32_t _M0L6_2atmpS2242;
        while (1) {
          if (_M0L1iS725 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2232 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2231 = _M0L6_2atmpS2232 + _M0L7olengthS705;
            int32_t _M0L6_2atmpS2230 = _M0L6_2atmpS2231 - _M0L1iS725;
            int32_t _M0L6_2atmpS2225 = _M0L6_2atmpS2230 - 1;
            uint64_t _M0L6_2atmpS2229 = _M0L6outputS726 % 10ull;
            int32_t _M0L6_2atmpS2228 = (int32_t)_M0L6_2atmpS2229;
            int32_t _M0L6_2atmpS2227 = 48 + _M0L6_2atmpS2228;
            int32_t _M0L6_2atmpS2226 = _M0L6_2atmpS2227 & 0xff;
            int32_t _M0L6_2atmpS2233;
            uint64_t _M0L6_2atmpS2234;
            if (
              _M0L6_2atmpS2225 < 0
              || _M0L6_2atmpS2225 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2225] = _M0L6_2atmpS2226;
            _M0L6_2atmpS2233 = _M0L1iS725 + 1;
            _M0L6_2atmpS2234 = _M0L6outputS726 / 10ull;
            _M0L1iS725 = _M0L6_2atmpS2233;
            _M0L6outputS726 = _M0L6_2atmpS2234;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2235 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2235 + _M0L7olengthS705;
        _M0L6_2atmpS2240 = _M0Lm3expS706;
        _M0L7_2abindS728 = _M0L6_2atmpS2240 + 1;
        _M0L1iS729 = _M0L7olengthS705;
        while (1) {
          if (_M0L1iS729 < _M0L7_2abindS728) {
            int32_t _M0L6_2atmpS2238 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2237 = _M0L6_2atmpS2238 + _M0L1iS729;
            int32_t _M0L6_2atmpS2236 = _M0L6_2atmpS2237 - _M0L7olengthS705;
            int32_t _M0L6_2atmpS2239;
            if (
              _M0L6_2atmpS2236 < 0
              || _M0L6_2atmpS2236 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2236] = 48;
            _M0L6_2atmpS2239 = _M0L1iS729 + 1;
            _M0L1iS729 = _M0L6_2atmpS2239;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2241 = _M0Lm5indexS701;
        _M0L6_2atmpS2244 = _M0Lm3expS706;
        _M0L6_2atmpS2243 = _M0L6_2atmpS2244 + 1;
        _M0L6_2atmpS2242 = _M0L6_2atmpS2243 - _M0L7olengthS705;
        _M0Lm5indexS701 = _M0L6_2atmpS2241 + _M0L6_2atmpS2242;
      } else {
        int32_t _M0L6_2atmpS2261 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2260 = _M0L6_2atmpS2261 + 1;
        int32_t _M0L1iS731 = 0;
        int32_t _M0L7currentS732 = _M0L6_2atmpS2260;
        uint64_t _M0L6outputS733 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2262;
        int32_t _M0L6_2atmpS2263;
        while (1) {
          if (_M0L1iS731 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2256 = _M0L7olengthS705 - _M0L1iS731;
            int32_t _M0L6_2atmpS2254 = _M0L6_2atmpS2256 - 1;
            int32_t _M0L6_2atmpS2255 = _M0Lm3expS706;
            int32_t _M0L7currentS734;
            int32_t _M0L6_2atmpS2251;
            int32_t _M0L6_2atmpS2250;
            int32_t _M0L6_2atmpS2245;
            uint64_t _M0L6_2atmpS2249;
            int32_t _M0L6_2atmpS2248;
            int32_t _M0L6_2atmpS2247;
            int32_t _M0L6_2atmpS2246;
            int32_t _M0L6_2atmpS2252;
            uint64_t _M0L6_2atmpS2253;
            if (_M0L6_2atmpS2254 == _M0L6_2atmpS2255) {
              int32_t _M0L6_2atmpS2259 = _M0L7currentS732 + _M0L7olengthS705;
              int32_t _M0L6_2atmpS2258 = _M0L6_2atmpS2259 - _M0L1iS731;
              int32_t _M0L6_2atmpS2257 = _M0L6_2atmpS2258 - 1;
              if (
                _M0L6_2atmpS2257 < 0
                || _M0L6_2atmpS2257 >= Moonbit_array_length(_M0L6resultS700)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS700[_M0L6_2atmpS2257] = 46;
              _M0L7currentS734 = _M0L7currentS732 - 1;
            } else {
              _M0L7currentS734 = _M0L7currentS732;
            }
            _M0L6_2atmpS2251 = _M0L7currentS734 + _M0L7olengthS705;
            _M0L6_2atmpS2250 = _M0L6_2atmpS2251 - _M0L1iS731;
            _M0L6_2atmpS2245 = _M0L6_2atmpS2250 - 1;
            _M0L6_2atmpS2249 = _M0L6outputS733 % 10ull;
            _M0L6_2atmpS2248 = (int32_t)_M0L6_2atmpS2249;
            _M0L6_2atmpS2247 = 48 + _M0L6_2atmpS2248;
            _M0L6_2atmpS2246 = _M0L6_2atmpS2247 & 0xff;
            if (
              _M0L6_2atmpS2245 < 0
              || _M0L6_2atmpS2245 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2245] = _M0L6_2atmpS2246;
            _M0L6_2atmpS2252 = _M0L1iS731 + 1;
            _M0L6_2atmpS2253 = _M0L6outputS733 / 10ull;
            _M0L1iS731 = _M0L6_2atmpS2252;
            _M0L7currentS732 = _M0L7currentS734;
            _M0L6outputS733 = _M0L6_2atmpS2253;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2262 = _M0Lm5indexS701;
        _M0L6_2atmpS2263 = _M0L7olengthS705 + 1;
        _M0Lm5indexS701 = _M0L6_2atmpS2262 + _M0L6_2atmpS2263;
      }
    }
    _M0L6_2atmpS2264 = _M0Lm5indexS701;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3581
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2264);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_3581;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS646,
  uint32_t _M0L12ieeeExponentS645
) {
  int32_t _M0Lm2e2S643;
  uint64_t _M0Lm2m2S644;
  uint64_t _M0L6_2atmpS2138;
  uint64_t _M0L6_2atmpS2137;
  int32_t _M0L4evenS647;
  uint64_t _M0L6_2atmpS2136;
  uint64_t _M0L2mvS648;
  int32_t _M0L7mmShiftS649;
  uint64_t _M0Lm2vrS650;
  uint64_t _M0Lm2vpS651;
  uint64_t _M0Lm2vmS652;
  int32_t _M0Lm3e10S653;
  int32_t _M0Lm17vmIsTrailingZerosS654;
  int32_t _M0Lm17vrIsTrailingZerosS655;
  int32_t _M0L6_2atmpS2038;
  int32_t _M0Lm7removedS674;
  int32_t _M0Lm16lastRemovedDigitS675;
  uint64_t _M0Lm6outputS676;
  int32_t _M0L6_2atmpS2134;
  int32_t _M0L6_2atmpS2135;
  int32_t _M0L3expS699;
  uint64_t _M0L6_2atmpS2133;
  struct _M0TPB17FloatingDecimal64* _block_3587;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S643 = 0;
  _M0Lm2m2S644 = 0ull;
  if (_M0L12ieeeExponentS645 == 0u) {
    _M0Lm2e2S643 = -1076;
    _M0Lm2m2S644 = _M0L12ieeeMantissaS646;
  } else {
    int32_t _M0L6_2atmpS2037 = *(int32_t*)&_M0L12ieeeExponentS645;
    int32_t _M0L6_2atmpS2036 = _M0L6_2atmpS2037 - 1023;
    int32_t _M0L6_2atmpS2035 = _M0L6_2atmpS2036 - 52;
    _M0Lm2e2S643 = _M0L6_2atmpS2035 - 2;
    _M0Lm2m2S644 = 4503599627370496ull | _M0L12ieeeMantissaS646;
  }
  _M0L6_2atmpS2138 = _M0Lm2m2S644;
  _M0L6_2atmpS2137 = _M0L6_2atmpS2138 & 1ull;
  _M0L4evenS647 = _M0L6_2atmpS2137 == 0ull;
  _M0L6_2atmpS2136 = _M0Lm2m2S644;
  _M0L2mvS648 = 4ull * _M0L6_2atmpS2136;
  _M0L7mmShiftS649
  = _M0L12ieeeMantissaS646 != 0ull || _M0L12ieeeExponentS645 <= 1u;
  _M0Lm2vrS650 = 0ull;
  _M0Lm2vpS651 = 0ull;
  _M0Lm2vmS652 = 0ull;
  _M0Lm3e10S653 = 0;
  _M0Lm17vmIsTrailingZerosS654 = 0;
  _M0Lm17vrIsTrailingZerosS655 = 0;
  _M0L6_2atmpS2038 = _M0Lm2e2S643;
  if (_M0L6_2atmpS2038 >= 0) {
    int32_t _M0L6_2atmpS2060 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2056;
    int32_t _M0L6_2atmpS2059;
    int32_t _M0L6_2atmpS2058;
    int32_t _M0L6_2atmpS2057;
    int32_t _M0L1qS656;
    int32_t _M0L6_2atmpS2055;
    int32_t _M0L6_2atmpS2054;
    int32_t _M0L1kS657;
    int32_t _M0L6_2atmpS2053;
    int32_t _M0L6_2atmpS2052;
    int32_t _M0L6_2atmpS2051;
    int32_t _M0L1iS658;
    struct _M0TPB8Pow5Pair _M0L4pow5S659;
    uint64_t _M0L6_2atmpS2050;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS660;
    uint64_t _M0L8_2avrOutS661;
    uint64_t _M0L8_2avpOutS662;
    uint64_t _M0L8_2avmOutS663;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2056 = _M0FPB9log10Pow2(_M0L6_2atmpS2060);
    _M0L6_2atmpS2059 = _M0Lm2e2S643;
    _M0L6_2atmpS2058 = _M0L6_2atmpS2059 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2057 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2058);
    _M0L1qS656 = _M0L6_2atmpS2056 - _M0L6_2atmpS2057;
    _M0Lm3e10S653 = _M0L1qS656;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2055 = _M0FPB8pow5bits(_M0L1qS656);
    _M0L6_2atmpS2054 = 125 + _M0L6_2atmpS2055;
    _M0L1kS657 = _M0L6_2atmpS2054 - 1;
    _M0L6_2atmpS2053 = _M0Lm2e2S643;
    _M0L6_2atmpS2052 = -_M0L6_2atmpS2053;
    _M0L6_2atmpS2051 = _M0L6_2atmpS2052 + _M0L1qS656;
    _M0L1iS658 = _M0L6_2atmpS2051 + _M0L1kS657;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S659 = _M0FPB22double__computeInvPow5(_M0L1qS656);
    _M0L6_2atmpS2050 = _M0Lm2m2S644;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS660
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2050, _M0L4pow5S659, _M0L1iS658, _M0L7mmShiftS649);
    _M0L8_2avrOutS661 = _M0L7_2abindS660.$0;
    _M0L8_2avpOutS662 = _M0L7_2abindS660.$1;
    _M0L8_2avmOutS663 = _M0L7_2abindS660.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS661;
    _M0Lm2vpS651 = _M0L8_2avpOutS662;
    _M0Lm2vmS652 = _M0L8_2avmOutS663;
    if (_M0L1qS656 <= 21) {
      int32_t _M0L6_2atmpS2046 = (int32_t)_M0L2mvS648;
      uint64_t _M0L6_2atmpS2049 = _M0L2mvS648 / 5ull;
      int32_t _M0L6_2atmpS2048 = (int32_t)_M0L6_2atmpS2049;
      int32_t _M0L6_2atmpS2047 = 5 * _M0L6_2atmpS2048;
      int32_t _M0L6mvMod5S664 = _M0L6_2atmpS2046 - _M0L6_2atmpS2047;
      if (_M0L6mvMod5S664 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS648, _M0L1qS656);
      } else if (_M0L4evenS647) {
        uint64_t _M0L6_2atmpS2040 = _M0L2mvS648 - 1ull;
        uint64_t _M0L6_2atmpS2041;
        uint64_t _M0L6_2atmpS2039;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2041 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS649);
        _M0L6_2atmpS2039 = _M0L6_2atmpS2040 - _M0L6_2atmpS2041;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2039, _M0L1qS656);
      } else {
        uint64_t _M0L6_2atmpS2042 = _M0Lm2vpS651;
        uint64_t _M0L6_2atmpS2045 = _M0L2mvS648 + 2ull;
        int32_t _M0L6_2atmpS2044;
        uint64_t _M0L6_2atmpS2043;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2044
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2045, _M0L1qS656);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2043 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2044);
        _M0Lm2vpS651 = _M0L6_2atmpS2042 - _M0L6_2atmpS2043;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2074 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2073 = -_M0L6_2atmpS2074;
    int32_t _M0L6_2atmpS2068;
    int32_t _M0L6_2atmpS2072;
    int32_t _M0L6_2atmpS2071;
    int32_t _M0L6_2atmpS2070;
    int32_t _M0L6_2atmpS2069;
    int32_t _M0L1qS665;
    int32_t _M0L6_2atmpS2061;
    int32_t _M0L6_2atmpS2067;
    int32_t _M0L6_2atmpS2066;
    int32_t _M0L1iS666;
    int32_t _M0L6_2atmpS2065;
    int32_t _M0L1kS667;
    int32_t _M0L1jS668;
    struct _M0TPB8Pow5Pair _M0L4pow5S669;
    uint64_t _M0L6_2atmpS2064;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS670;
    uint64_t _M0L8_2avrOutS671;
    uint64_t _M0L8_2avpOutS672;
    uint64_t _M0L8_2avmOutS673;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2068 = _M0FPB9log10Pow5(_M0L6_2atmpS2073);
    _M0L6_2atmpS2072 = _M0Lm2e2S643;
    _M0L6_2atmpS2071 = -_M0L6_2atmpS2072;
    _M0L6_2atmpS2070 = _M0L6_2atmpS2071 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2069 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2070);
    _M0L1qS665 = _M0L6_2atmpS2068 - _M0L6_2atmpS2069;
    _M0L6_2atmpS2061 = _M0Lm2e2S643;
    _M0Lm3e10S653 = _M0L1qS665 + _M0L6_2atmpS2061;
    _M0L6_2atmpS2067 = _M0Lm2e2S643;
    _M0L6_2atmpS2066 = -_M0L6_2atmpS2067;
    _M0L1iS666 = _M0L6_2atmpS2066 - _M0L1qS665;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2065 = _M0FPB8pow5bits(_M0L1iS666);
    _M0L1kS667 = _M0L6_2atmpS2065 - 125;
    _M0L1jS668 = _M0L1qS665 - _M0L1kS667;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S669 = _M0FPB19double__computePow5(_M0L1iS666);
    _M0L6_2atmpS2064 = _M0Lm2m2S644;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS670
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2064, _M0L4pow5S669, _M0L1jS668, _M0L7mmShiftS649);
    _M0L8_2avrOutS671 = _M0L7_2abindS670.$0;
    _M0L8_2avpOutS672 = _M0L7_2abindS670.$1;
    _M0L8_2avmOutS673 = _M0L7_2abindS670.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS671;
    _M0Lm2vpS651 = _M0L8_2avpOutS672;
    _M0Lm2vmS652 = _M0L8_2avmOutS673;
    if (_M0L1qS665 <= 1) {
      _M0Lm17vrIsTrailingZerosS655 = 1;
      if (_M0L4evenS647) {
        int32_t _M0L6_2atmpS2062;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2062 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS649);
        _M0Lm17vmIsTrailingZerosS654 = _M0L6_2atmpS2062 == 1;
      } else {
        uint64_t _M0L6_2atmpS2063 = _M0Lm2vpS651;
        _M0Lm2vpS651 = _M0L6_2atmpS2063 - 1ull;
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
    int32_t _if__result_3584;
    uint64_t _M0L6_2atmpS2104;
    uint64_t _M0L6_2atmpS2110;
    uint64_t _M0L6_2atmpS2111;
    int32_t _if__result_3585;
    int32_t _M0L6_2atmpS2107;
    int64_t _M0L6_2atmpS2106;
    uint64_t _M0L6_2atmpS2105;
    while (1) {
      uint64_t _M0L6_2atmpS2087 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S677 = _M0L6_2atmpS2087 / 10ull;
      uint64_t _M0L6_2atmpS2086 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S678 = _M0L6_2atmpS2086 / 10ull;
      uint64_t _M0L6_2atmpS2085;
      int32_t _M0L6_2atmpS2082;
      int32_t _M0L6_2atmpS2084;
      int32_t _M0L6_2atmpS2083;
      int32_t _M0L7vmMod10S680;
      uint64_t _M0L6_2atmpS2081;
      uint64_t _M0L7vrDiv10S681;
      uint64_t _M0L6_2atmpS2080;
      int32_t _M0L6_2atmpS2077;
      int32_t _M0L6_2atmpS2079;
      int32_t _M0L6_2atmpS2078;
      int32_t _M0L7vrMod10S682;
      int32_t _M0L6_2atmpS2076;
      if (_M0L7vpDiv10S677 <= _M0L7vmDiv10S678) {
        break;
      }
      _M0L6_2atmpS2085 = _M0Lm2vmS652;
      _M0L6_2atmpS2082 = (int32_t)_M0L6_2atmpS2085;
      _M0L6_2atmpS2084 = (int32_t)_M0L7vmDiv10S678;
      _M0L6_2atmpS2083 = 10 * _M0L6_2atmpS2084;
      _M0L7vmMod10S680 = _M0L6_2atmpS2082 - _M0L6_2atmpS2083;
      _M0L6_2atmpS2081 = _M0Lm2vrS650;
      _M0L7vrDiv10S681 = _M0L6_2atmpS2081 / 10ull;
      _M0L6_2atmpS2080 = _M0Lm2vrS650;
      _M0L6_2atmpS2077 = (int32_t)_M0L6_2atmpS2080;
      _M0L6_2atmpS2079 = (int32_t)_M0L7vrDiv10S681;
      _M0L6_2atmpS2078 = 10 * _M0L6_2atmpS2079;
      _M0L7vrMod10S682 = _M0L6_2atmpS2077 - _M0L6_2atmpS2078;
      _M0Lm17vmIsTrailingZerosS654
      = _M0Lm17vmIsTrailingZerosS654 && _M0L7vmMod10S680 == 0;
      if (_M0Lm17vrIsTrailingZerosS655) {
        int32_t _M0L6_2atmpS2075 = _M0Lm16lastRemovedDigitS675;
        _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2075 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS655 = 0;
      }
      _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S682;
      _M0Lm2vrS650 = _M0L7vrDiv10S681;
      _M0Lm2vpS651 = _M0L7vpDiv10S677;
      _M0Lm2vmS652 = _M0L7vmDiv10S678;
      _M0L6_2atmpS2076 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2076 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS654) {
      while (1) {
        uint64_t _M0L6_2atmpS2100 = _M0Lm2vmS652;
        uint64_t _M0L7vmDiv10S683 = _M0L6_2atmpS2100 / 10ull;
        uint64_t _M0L6_2atmpS2099 = _M0Lm2vmS652;
        int32_t _M0L6_2atmpS2096 = (int32_t)_M0L6_2atmpS2099;
        int32_t _M0L6_2atmpS2098 = (int32_t)_M0L7vmDiv10S683;
        int32_t _M0L6_2atmpS2097 = 10 * _M0L6_2atmpS2098;
        int32_t _M0L7vmMod10S684 = _M0L6_2atmpS2096 - _M0L6_2atmpS2097;
        uint64_t _M0L6_2atmpS2095;
        uint64_t _M0L7vpDiv10S686;
        uint64_t _M0L6_2atmpS2094;
        uint64_t _M0L7vrDiv10S687;
        uint64_t _M0L6_2atmpS2093;
        int32_t _M0L6_2atmpS2090;
        int32_t _M0L6_2atmpS2092;
        int32_t _M0L6_2atmpS2091;
        int32_t _M0L7vrMod10S688;
        int32_t _M0L6_2atmpS2089;
        if (_M0L7vmMod10S684 != 0) {
          break;
        }
        _M0L6_2atmpS2095 = _M0Lm2vpS651;
        _M0L7vpDiv10S686 = _M0L6_2atmpS2095 / 10ull;
        _M0L6_2atmpS2094 = _M0Lm2vrS650;
        _M0L7vrDiv10S687 = _M0L6_2atmpS2094 / 10ull;
        _M0L6_2atmpS2093 = _M0Lm2vrS650;
        _M0L6_2atmpS2090 = (int32_t)_M0L6_2atmpS2093;
        _M0L6_2atmpS2092 = (int32_t)_M0L7vrDiv10S687;
        _M0L6_2atmpS2091 = 10 * _M0L6_2atmpS2092;
        _M0L7vrMod10S688 = _M0L6_2atmpS2090 - _M0L6_2atmpS2091;
        if (_M0Lm17vrIsTrailingZerosS655) {
          int32_t _M0L6_2atmpS2088 = _M0Lm16lastRemovedDigitS675;
          _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2088 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS655 = 0;
        }
        _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S688;
        _M0Lm2vrS650 = _M0L7vrDiv10S687;
        _M0Lm2vpS651 = _M0L7vpDiv10S686;
        _M0Lm2vmS652 = _M0L7vmDiv10S683;
        _M0L6_2atmpS2089 = _M0Lm7removedS674;
        _M0Lm7removedS674 = _M0L6_2atmpS2089 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS655) {
      int32_t _M0L6_2atmpS2103 = _M0Lm16lastRemovedDigitS675;
      if (_M0L6_2atmpS2103 == 5) {
        uint64_t _M0L6_2atmpS2102 = _M0Lm2vrS650;
        uint64_t _M0L6_2atmpS2101 = _M0L6_2atmpS2102 % 2ull;
        _if__result_3584 = _M0L6_2atmpS2101 == 0ull;
      } else {
        _if__result_3584 = 0;
      }
    } else {
      _if__result_3584 = 0;
    }
    if (_if__result_3584) {
      _M0Lm16lastRemovedDigitS675 = 4;
    }
    _M0L6_2atmpS2104 = _M0Lm2vrS650;
    _M0L6_2atmpS2110 = _M0Lm2vrS650;
    _M0L6_2atmpS2111 = _M0Lm2vmS652;
    if (_M0L6_2atmpS2110 == _M0L6_2atmpS2111) {
      if (!_M0L4evenS647) {
        _if__result_3585 = 1;
      } else {
        int32_t _M0L6_2atmpS2109 = _M0Lm17vmIsTrailingZerosS654;
        _if__result_3585 = !_M0L6_2atmpS2109;
      }
    } else {
      _if__result_3585 = 0;
    }
    if (_if__result_3585) {
      _M0L6_2atmpS2107 = 1;
    } else {
      int32_t _M0L6_2atmpS2108 = _M0Lm16lastRemovedDigitS675;
      _M0L6_2atmpS2107 = _M0L6_2atmpS2108 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2106 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2107);
    _M0L6_2atmpS2105 = *(uint64_t*)&_M0L6_2atmpS2106;
    _M0Lm6outputS676 = _M0L6_2atmpS2104 + _M0L6_2atmpS2105;
  } else {
    int32_t _M0Lm7roundUpS689 = 0;
    uint64_t _M0L6_2atmpS2132 = _M0Lm2vpS651;
    uint64_t _M0L8vpDiv100S690 = _M0L6_2atmpS2132 / 100ull;
    uint64_t _M0L6_2atmpS2131 = _M0Lm2vmS652;
    uint64_t _M0L8vmDiv100S691 = _M0L6_2atmpS2131 / 100ull;
    uint64_t _M0L6_2atmpS2126;
    uint64_t _M0L6_2atmpS2129;
    uint64_t _M0L6_2atmpS2130;
    int32_t _M0L6_2atmpS2128;
    uint64_t _M0L6_2atmpS2127;
    if (_M0L8vpDiv100S690 > _M0L8vmDiv100S691) {
      uint64_t _M0L6_2atmpS2117 = _M0Lm2vrS650;
      uint64_t _M0L8vrDiv100S692 = _M0L6_2atmpS2117 / 100ull;
      uint64_t _M0L6_2atmpS2116 = _M0Lm2vrS650;
      int32_t _M0L6_2atmpS2113 = (int32_t)_M0L6_2atmpS2116;
      int32_t _M0L6_2atmpS2115 = (int32_t)_M0L8vrDiv100S692;
      int32_t _M0L6_2atmpS2114 = 100 * _M0L6_2atmpS2115;
      int32_t _M0L8vrMod100S693 = _M0L6_2atmpS2113 - _M0L6_2atmpS2114;
      int32_t _M0L6_2atmpS2112;
      _M0Lm7roundUpS689 = _M0L8vrMod100S693 >= 50;
      _M0Lm2vrS650 = _M0L8vrDiv100S692;
      _M0Lm2vpS651 = _M0L8vpDiv100S690;
      _M0Lm2vmS652 = _M0L8vmDiv100S691;
      _M0L6_2atmpS2112 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2112 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2125 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S694 = _M0L6_2atmpS2125 / 10ull;
      uint64_t _M0L6_2atmpS2124 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S695 = _M0L6_2atmpS2124 / 10ull;
      uint64_t _M0L6_2atmpS2123;
      uint64_t _M0L7vrDiv10S697;
      uint64_t _M0L6_2atmpS2122;
      int32_t _M0L6_2atmpS2119;
      int32_t _M0L6_2atmpS2121;
      int32_t _M0L6_2atmpS2120;
      int32_t _M0L7vrMod10S698;
      int32_t _M0L6_2atmpS2118;
      if (_M0L7vpDiv10S694 <= _M0L7vmDiv10S695) {
        break;
      }
      _M0L6_2atmpS2123 = _M0Lm2vrS650;
      _M0L7vrDiv10S697 = _M0L6_2atmpS2123 / 10ull;
      _M0L6_2atmpS2122 = _M0Lm2vrS650;
      _M0L6_2atmpS2119 = (int32_t)_M0L6_2atmpS2122;
      _M0L6_2atmpS2121 = (int32_t)_M0L7vrDiv10S697;
      _M0L6_2atmpS2120 = 10 * _M0L6_2atmpS2121;
      _M0L7vrMod10S698 = _M0L6_2atmpS2119 - _M0L6_2atmpS2120;
      _M0Lm7roundUpS689 = _M0L7vrMod10S698 >= 5;
      _M0Lm2vrS650 = _M0L7vrDiv10S697;
      _M0Lm2vpS651 = _M0L7vpDiv10S694;
      _M0Lm2vmS652 = _M0L7vmDiv10S695;
      _M0L6_2atmpS2118 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2118 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2126 = _M0Lm2vrS650;
    _M0L6_2atmpS2129 = _M0Lm2vrS650;
    _M0L6_2atmpS2130 = _M0Lm2vmS652;
    _M0L6_2atmpS2128
    = _M0L6_2atmpS2129 == _M0L6_2atmpS2130 || _M0Lm7roundUpS689;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2127 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2128);
    _M0Lm6outputS676 = _M0L6_2atmpS2126 + _M0L6_2atmpS2127;
  }
  _M0L6_2atmpS2134 = _M0Lm3e10S653;
  _M0L6_2atmpS2135 = _M0Lm7removedS674;
  _M0L3expS699 = _M0L6_2atmpS2134 + _M0L6_2atmpS2135;
  _M0L6_2atmpS2133 = _M0Lm6outputS676;
  _block_3587
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_3587)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3587->$0 = _M0L6_2atmpS2133;
  _block_3587->$1 = _M0L3expS699;
  return _block_3587;
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
  int32_t _M0L6_2atmpS2034;
  int32_t _M0L6_2atmpS2033;
  int32_t _M0L4baseS621;
  int32_t _M0L5base2S623;
  int32_t _M0L6offsetS624;
  int32_t _M0L6_2atmpS2032;
  uint64_t _M0L4mul0S625;
  int32_t _M0L6_2atmpS2031;
  int32_t _M0L6_2atmpS2030;
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
  int32_t _M0L6_2atmpS2028;
  int32_t _M0L6_2atmpS2029;
  int32_t _M0L5deltaS636;
  uint64_t _M0L6_2atmpS2027;
  uint64_t _M0L6_2atmpS2019;
  int32_t _M0L6_2atmpS2026;
  uint32_t _M0L6_2atmpS2023;
  int32_t _M0L6_2atmpS2025;
  int32_t _M0L6_2atmpS2024;
  uint32_t _M0L6_2atmpS2022;
  uint32_t _M0L6_2atmpS2021;
  uint64_t _M0L6_2atmpS2020;
  uint64_t _M0L1aS637;
  uint64_t _M0L6_2atmpS2018;
  uint64_t _M0L1bS638;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2034 = _M0L1iS622 + 26;
  _M0L6_2atmpS2033 = _M0L6_2atmpS2034 - 1;
  _M0L4baseS621 = _M0L6_2atmpS2033 / 26;
  _M0L5base2S623 = _M0L4baseS621 * 26;
  _M0L6offsetS624 = _M0L5base2S623 - _M0L1iS622;
  _M0L6_2atmpS2032 = _M0L4baseS621 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2032);
  _M0L6_2atmpS2031 = _M0L4baseS621 * 2;
  _M0L6_2atmpS2030 = _M0L6_2atmpS2031 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2030);
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
    uint64_t _M0L6_2atmpS2017 = _M0Lm5high1S635;
    _M0Lm5high1S635 = _M0L6_2atmpS2017 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2028 = _M0FPB8pow5bits(_M0L5base2S623);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2029 = _M0FPB8pow5bits(_M0L1iS622);
  _M0L5deltaS636 = _M0L6_2atmpS2028 - _M0L6_2atmpS2029;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2027
  = _M0FPB13shiftright128(_M0L7_2alow0S632, _M0L3sumS634, _M0L5deltaS636);
  _M0L6_2atmpS2019 = _M0L6_2atmpS2027 + 1ull;
  _M0L6_2atmpS2026 = _M0L1iS622 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2023
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2026);
  _M0L6_2atmpS2025 = _M0L1iS622 % 16;
  _M0L6_2atmpS2024 = _M0L6_2atmpS2025 << 1;
  _M0L6_2atmpS2022 = _M0L6_2atmpS2023 >> (_M0L6_2atmpS2024 & 31);
  _M0L6_2atmpS2021 = _M0L6_2atmpS2022 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2020 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2021);
  _M0L1aS637 = _M0L6_2atmpS2019 + _M0L6_2atmpS2020;
  _M0L6_2atmpS2018 = _M0Lm5high1S635;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS638
  = _M0FPB13shiftright128(_M0L3sumS634, _M0L6_2atmpS2018, _M0L5deltaS636);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS637, .$1 = _M0L1bS638};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS604) {
  int32_t _M0L4baseS603;
  int32_t _M0L5base2S605;
  int32_t _M0L6offsetS606;
  int32_t _M0L6_2atmpS2016;
  uint64_t _M0L4mul0S607;
  int32_t _M0L6_2atmpS2015;
  int32_t _M0L6_2atmpS2014;
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
  int32_t _M0L6_2atmpS2012;
  int32_t _M0L6_2atmpS2013;
  int32_t _M0L5deltaS618;
  uint64_t _M0L6_2atmpS2004;
  int32_t _M0L6_2atmpS2011;
  uint32_t _M0L6_2atmpS2008;
  int32_t _M0L6_2atmpS2010;
  int32_t _M0L6_2atmpS2009;
  uint32_t _M0L6_2atmpS2007;
  uint32_t _M0L6_2atmpS2006;
  uint64_t _M0L6_2atmpS2005;
  uint64_t _M0L1aS619;
  uint64_t _M0L6_2atmpS2003;
  uint64_t _M0L1bS620;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS603 = _M0L1iS604 / 26;
  _M0L5base2S605 = _M0L4baseS603 * 26;
  _M0L6offsetS606 = _M0L1iS604 - _M0L5base2S605;
  _M0L6_2atmpS2016 = _M0L4baseS603 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2016);
  _M0L6_2atmpS2015 = _M0L4baseS603 * 2;
  _M0L6_2atmpS2014 = _M0L6_2atmpS2015 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2014);
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
    uint64_t _M0L6_2atmpS2002 = _M0Lm5high1S617;
    _M0Lm5high1S617 = _M0L6_2atmpS2002 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2012 = _M0FPB8pow5bits(_M0L1iS604);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2013 = _M0FPB8pow5bits(_M0L5base2S605);
  _M0L5deltaS618 = _M0L6_2atmpS2012 - _M0L6_2atmpS2013;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2004
  = _M0FPB13shiftright128(_M0L7_2alow0S614, _M0L3sumS616, _M0L5deltaS618);
  _M0L6_2atmpS2011 = _M0L1iS604 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2008
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2011);
  _M0L6_2atmpS2010 = _M0L1iS604 % 16;
  _M0L6_2atmpS2009 = _M0L6_2atmpS2010 << 1;
  _M0L6_2atmpS2007 = _M0L6_2atmpS2008 >> (_M0L6_2atmpS2009 & 31);
  _M0L6_2atmpS2006 = _M0L6_2atmpS2007 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2005 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2006);
  _M0L1aS619 = _M0L6_2atmpS2004 + _M0L6_2atmpS2005;
  _M0L6_2atmpS2003 = _M0Lm5high1S617;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS620
  = _M0FPB13shiftright128(_M0L3sumS616, _M0L6_2atmpS2003, _M0L5deltaS618);
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
  uint64_t _M0L6_2atmpS2001;
  uint64_t _M0L2hiS585;
  uint64_t _M0L3lo2S586;
  uint64_t _M0L6_2atmpS1999;
  uint64_t _M0L6_2atmpS2000;
  uint64_t _M0L4mid2S587;
  uint64_t _M0L6_2atmpS1998;
  uint64_t _M0L3hi2S588;
  int32_t _M0L6_2atmpS1997;
  int32_t _M0L6_2atmpS1996;
  uint64_t _M0L2vpS589;
  uint64_t _M0Lm2vmS591;
  int32_t _M0L6_2atmpS1995;
  int32_t _M0L6_2atmpS1994;
  uint64_t _M0L2vrS602;
  uint64_t _M0L6_2atmpS1993;
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
    _M0L6_2atmpS2001 = 1ull;
  } else {
    _M0L6_2atmpS2001 = 0ull;
  }
  _M0L2hiS585 = _M0L6_2ahi2S583 + _M0L6_2atmpS2001;
  _M0L3lo2S586 = _M0L5_2aloS579 + _M0L7_2amul0S573;
  _M0L6_2atmpS1999 = _M0L3midS584 + _M0L7_2amul1S575;
  if (_M0L3lo2S586 < _M0L5_2aloS579) {
    _M0L6_2atmpS2000 = 1ull;
  } else {
    _M0L6_2atmpS2000 = 0ull;
  }
  _M0L4mid2S587 = _M0L6_2atmpS1999 + _M0L6_2atmpS2000;
  if (_M0L4mid2S587 < _M0L3midS584) {
    _M0L6_2atmpS1998 = 1ull;
  } else {
    _M0L6_2atmpS1998 = 0ull;
  }
  _M0L3hi2S588 = _M0L2hiS585 + _M0L6_2atmpS1998;
  _M0L6_2atmpS1997 = _M0L1jS590 - 64;
  _M0L6_2atmpS1996 = _M0L6_2atmpS1997 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS589
  = _M0FPB13shiftright128(_M0L4mid2S587, _M0L3hi2S588, _M0L6_2atmpS1996);
  _M0Lm2vmS591 = 0ull;
  if (_M0L7mmShiftS592) {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS579 - _M0L7_2amul0S573;
    uint64_t _M0L6_2atmpS1983 = _M0L3midS584 - _M0L7_2amul1S575;
    uint64_t _M0L6_2atmpS1984;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS1982;
    uint64_t _M0L3hi3S595;
    int32_t _M0L6_2atmpS1981;
    int32_t _M0L6_2atmpS1980;
    if (_M0L5_2aloS579 < _M0L3lo3S593) {
      _M0L6_2atmpS1984 = 1ull;
    } else {
      _M0L6_2atmpS1984 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS1983 - _M0L6_2atmpS1984;
    if (_M0L3midS584 < _M0L4mid3S594) {
      _M0L6_2atmpS1982 = 1ull;
    } else {
      _M0L6_2atmpS1982 = 0ull;
    }
    _M0L3hi3S595 = _M0L2hiS585 - _M0L6_2atmpS1982;
    _M0L6_2atmpS1981 = _M0L1jS590 - 64;
    _M0L6_2atmpS1980 = _M0L6_2atmpS1981 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid3S594, _M0L3hi3S595, _M0L6_2atmpS1980);
  } else {
    uint64_t _M0L3lo3S596 = _M0L5_2aloS579 + _M0L5_2aloS579;
    uint64_t _M0L6_2atmpS1991 = _M0L3midS584 + _M0L3midS584;
    uint64_t _M0L6_2atmpS1992;
    uint64_t _M0L4mid3S597;
    uint64_t _M0L6_2atmpS1989;
    uint64_t _M0L6_2atmpS1990;
    uint64_t _M0L3hi3S598;
    uint64_t _M0L3lo4S599;
    uint64_t _M0L6_2atmpS1987;
    uint64_t _M0L6_2atmpS1988;
    uint64_t _M0L4mid4S600;
    uint64_t _M0L6_2atmpS1986;
    uint64_t _M0L3hi4S601;
    int32_t _M0L6_2atmpS1985;
    if (_M0L3lo3S596 < _M0L5_2aloS579) {
      _M0L6_2atmpS1992 = 1ull;
    } else {
      _M0L6_2atmpS1992 = 0ull;
    }
    _M0L4mid3S597 = _M0L6_2atmpS1991 + _M0L6_2atmpS1992;
    _M0L6_2atmpS1989 = _M0L2hiS585 + _M0L2hiS585;
    if (_M0L4mid3S597 < _M0L3midS584) {
      _M0L6_2atmpS1990 = 1ull;
    } else {
      _M0L6_2atmpS1990 = 0ull;
    }
    _M0L3hi3S598 = _M0L6_2atmpS1989 + _M0L6_2atmpS1990;
    _M0L3lo4S599 = _M0L3lo3S596 - _M0L7_2amul0S573;
    _M0L6_2atmpS1987 = _M0L4mid3S597 - _M0L7_2amul1S575;
    if (_M0L3lo3S596 < _M0L3lo4S599) {
      _M0L6_2atmpS1988 = 1ull;
    } else {
      _M0L6_2atmpS1988 = 0ull;
    }
    _M0L4mid4S600 = _M0L6_2atmpS1987 - _M0L6_2atmpS1988;
    if (_M0L4mid3S597 < _M0L4mid4S600) {
      _M0L6_2atmpS1986 = 1ull;
    } else {
      _M0L6_2atmpS1986 = 0ull;
    }
    _M0L3hi4S601 = _M0L3hi3S598 - _M0L6_2atmpS1986;
    _M0L6_2atmpS1985 = _M0L1jS590 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid4S600, _M0L3hi4S601, _M0L6_2atmpS1985);
  }
  _M0L6_2atmpS1995 = _M0L1jS590 - 64;
  _M0L6_2atmpS1994 = _M0L6_2atmpS1995 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS602
  = _M0FPB13shiftright128(_M0L3midS584, _M0L2hiS585, _M0L6_2atmpS1994);
  _M0L6_2atmpS1993 = _M0Lm2vmS591;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS602,
                                                .$1 = _M0L2vpS589,
                                                .$2 = _M0L6_2atmpS1993};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  uint64_t _M0L6_2atmpS1979;
  uint64_t _M0L6_2atmpS1978;
  uint64_t _M0L6_2atmpS1977;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1979 = 1ull << (_M0L1pS572 & 63);
  _M0L6_2atmpS1978 = _M0L6_2atmpS1979 - 1ull;
  _M0L6_2atmpS1977 = _M0L5valueS571 & _M0L6_2atmpS1978;
  return _M0L6_2atmpS1977 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  int32_t _M0L6_2atmpS1976;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1976 = _M0FPB10pow5Factor(_M0L5valueS569);
  return _M0L6_2atmpS1976 >= _M0L1pS570;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS564) {
  uint64_t _M0L6_2atmpS1967;
  uint64_t _M0L6_2atmpS1968;
  uint64_t _M0L6_2atmpS1969;
  uint64_t _M0L6_2atmpS1970;
  uint64_t _M0L6_2atmpS1975;
  int32_t _M0L5countS565;
  uint64_t _M0L1vS566;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1967 = _M0L5valueS564 % 5ull;
  if (_M0L6_2atmpS1967 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1968 = _M0L5valueS564 % 25ull;
  if (_M0L6_2atmpS1968 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1969 = _M0L5valueS564 % 125ull;
  if (_M0L6_2atmpS1969 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1970 = _M0L5valueS564 % 625ull;
  if (_M0L6_2atmpS1970 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1975 = _M0L5valueS564 / 625ull;
  _M0L5countS565 = 4;
  _M0L1vS566 = _M0L6_2atmpS1975;
  while (1) {
    if (_M0L1vS566 > 0ull) {
      uint64_t _M0L6_2atmpS1971 = _M0L1vS566 % 5ull;
      int32_t _M0L6_2atmpS1972;
      uint64_t _M0L6_2atmpS1973;
      if (_M0L6_2atmpS1971 != 0ull) {
        return _M0L5countS565;
      }
      _M0L6_2atmpS1972 = _M0L5countS565 + 1;
      _M0L6_2atmpS1973 = _M0L1vS566 / 5ull;
      _M0L5countS565 = _M0L6_2atmpS1972;
      _M0L1vS566 = _M0L6_2atmpS1973;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS568;
      moonbit_string_t _M0L6_2atmpS1974;
      int32_t _result_3589;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS568
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS568, (moonbit_string_t)moonbit_string_literal_14.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS568, _M0L5valueS564);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1974
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS568);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS568);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_3589 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1974);
      moonbit_decref_cycle_free(_M0L6_2atmpS1974);
      return _result_3589;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS563,
  uint64_t _M0L2hiS561,
  int32_t _M0L4distS562
) {
  int32_t _M0L6_2atmpS1966;
  uint64_t _M0L6_2atmpS1964;
  uint64_t _M0L6_2atmpS1965;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1966 = 64 - _M0L4distS562;
  _M0L6_2atmpS1964 = _M0L2hiS561 << (_M0L6_2atmpS1966 & 63);
  _M0L6_2atmpS1965 = _M0L2loS563 >> (_M0L4distS562 & 63);
  return _M0L6_2atmpS1964 | _M0L6_2atmpS1965;
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
  uint64_t _M0L6_2atmpS1962;
  uint64_t _M0L6_2atmpS1963;
  uint64_t _M0L1yS557;
  uint64_t _M0L6_2atmpS1960;
  uint64_t _M0L6_2atmpS1961;
  uint64_t _M0L1zS558;
  uint64_t _M0L6_2atmpS1958;
  uint64_t _M0L6_2atmpS1959;
  uint64_t _M0L6_2atmpS1956;
  uint64_t _M0L6_2atmpS1957;
  uint64_t _M0L1wS559;
  uint64_t _M0L2loS560;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS550 = _M0L1aS551 & 4294967295ull;
  _M0L3aHiS552 = _M0L1aS551 >> 32;
  _M0L3bLoS553 = _M0L1bS554 & 4294967295ull;
  _M0L3bHiS555 = _M0L1bS554 >> 32;
  _M0L1xS556 = _M0L3aLoS550 * _M0L3bLoS553;
  _M0L6_2atmpS1962 = _M0L3aHiS552 * _M0L3bLoS553;
  _M0L6_2atmpS1963 = _M0L1xS556 >> 32;
  _M0L1yS557 = _M0L6_2atmpS1962 + _M0L6_2atmpS1963;
  _M0L6_2atmpS1960 = _M0L3aLoS550 * _M0L3bHiS555;
  _M0L6_2atmpS1961 = _M0L1yS557 & 4294967295ull;
  _M0L1zS558 = _M0L6_2atmpS1960 + _M0L6_2atmpS1961;
  _M0L6_2atmpS1958 = _M0L3aHiS552 * _M0L3bHiS555;
  _M0L6_2atmpS1959 = _M0L1yS557 >> 32;
  _M0L6_2atmpS1956 = _M0L6_2atmpS1958 + _M0L6_2atmpS1959;
  _M0L6_2atmpS1957 = _M0L1zS558 >> 32;
  _M0L1wS559 = _M0L6_2atmpS1956 + _M0L6_2atmpS1957;
  _M0L2loS560 = _M0L1aS551 * _M0L1bS554;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS560, .$1 = _M0L1wS559};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS548,
  int32_t _M0L4fromS545,
  int32_t _M0L2toS544
) {
  int32_t _M0L3lenS543;
  int32_t _M0L6_2atmpS1955;
  uint16_t* _M0L6bufferS546;
  int32_t _M0L1iS547;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS543 = _M0L2toS544 - _M0L4fromS545;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1955 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS546
  = (uint16_t*)moonbit_make_string(_M0L3lenS543, _M0L6_2atmpS1955);
  _M0L1iS547 = 0;
  while (1) {
    if (_M0L1iS547 < _M0L3lenS543) {
      int32_t _M0L6_2atmpS1953 = _M0L4fromS545 + _M0L1iS547;
      int32_t _M0L6_2atmpS1952;
      int32_t _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1954;
      if (
        _M0L6_2atmpS1953 < 0
        || _M0L6_2atmpS1953 >= Moonbit_array_length(_M0L5bytesS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1952 = (int32_t)_M0L5bytesS548[_M0L6_2atmpS1953];
      _M0L6_2atmpS1951 = (uint16_t)_M0L6_2atmpS1952;
      if (
        _M0L1iS547 < 0 || _M0L1iS547 >= Moonbit_array_length(_M0L6bufferS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS546[_M0L1iS547] = _M0L6_2atmpS1951;
      _M0L6_2atmpS1954 = _M0L1iS547 + 1;
      _M0L1iS547 = _M0L6_2atmpS1954;
      continue;
    }
    break;
  }
  return _M0L6bufferS546;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1950;
  uint32_t _M0L6_2atmpS1949;
  uint32_t _M0L6_2atmpS1948;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1950 = _M0L1eS542 * 78913;
  _M0L6_2atmpS1949 = *(uint32_t*)&_M0L6_2atmpS1950;
  _M0L6_2atmpS1948 = _M0L6_2atmpS1949 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1948;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS1947;
  uint32_t _M0L6_2atmpS1946;
  uint32_t _M0L6_2atmpS1945;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1947 = _M0L1eS541 * 732923;
  _M0L6_2atmpS1946 = *(uint32_t*)&_M0L6_2atmpS1947;
  _M0L6_2atmpS1945 = _M0L6_2atmpS1946 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1945;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS539,
  int32_t _M0L8exponentS540,
  int32_t _M0L8mantissaS537
) {
  moonbit_string_t _M0L1sS538;
  moonbit_string_t _result_3592;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS537) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L4signS539) {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_16.data;
  } else {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS540) {
    moonbit_string_t _result_3591;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3591
    = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_17.data);
    moonbit_decref_cycle_free(_M0L1sS538);
    return _result_3591;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3592
  = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_18.data);
  moonbit_decref_cycle_free(_M0L1sS538);
  return _result_3592;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS536) {
  int32_t _M0L6_2atmpS1944;
  uint32_t _M0L6_2atmpS1943;
  uint32_t _M0L6_2atmpS1942;
  int32_t _M0L6_2atmpS1941;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1944 = _M0L1eS536 * 1217359;
  _M0L6_2atmpS1943 = *(uint32_t*)&_M0L6_2atmpS1944;
  _M0L6_2atmpS1942 = _M0L6_2atmpS1943 >> 19;
  _M0L6_2atmpS1941 = *(int32_t*)&_M0L6_2atmpS1942;
  return _M0L6_2atmpS1941 + 1;
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
  int32_t _M0L3lenS530
) {
  float* _M0L6_2atmpS1937;
  struct _M0TPB5ArrayGfE* _block_3593;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1937 = (float*)moonbit_make_float_array_raw(_M0L3lenS530);
  _block_3593
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_3593)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_3593->$0 = _M0L6_2atmpS1937;
  _block_3593->$1 = _M0L3lenS530;
  return _block_3593;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS531
) {
  uint8_t* _M0L6_2atmpS1938;
  struct _M0TPB5ArrayGbE* _block_3594;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1938 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS531);
  _block_3594
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_3594)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 119, 0);
  _block_3594->$0 = _M0L6_2atmpS1938;
  _block_3594->$1 = _M0L3lenS531;
  return _block_3594;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS532
) {
  int32_t* _M0L6_2atmpS1939;
  struct _M0TPB5ArrayGiE* _block_3595;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1939 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS532);
  _block_3595
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_3595)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_3595->$0 = _M0L6_2atmpS1939;
  _block_3595->$1 = _M0L3lenS532;
  return _block_3595;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS533
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1940;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_3596;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1940
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS533, 0);
  _block_3596
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_3596)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 122, 0);
  _block_3596->$0 = _M0L6_2atmpS1940;
  _block_3596->$1 = _M0L3lenS533;
  return _block_3596;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint64_t* _M0L6_2atmpS1935;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1935 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1935)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1935[_M0L5indexS527];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint32_t* _M0L6_2atmpS1936;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1936 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS1936)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1936[_M0L5indexS529];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS525
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS525, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS524) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS524, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS523) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS523;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS511,
  float _M0L5valueS513
) {
  int32_t _M0L3lenS1907;
  float* _M0L6_2atmpS1909;
  int32_t _M0L6_2atmpS1908;
  int32_t _M0L6lengthS512;
  float* _M0L3bufS1912;
  int32_t _M0L6_2atmpS1913;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1907 = _M0L4selfS511->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1909 = _M0MPC15array5Array6bufferGfE(_M0L4selfS511);
  _M0L6_2atmpS1908 = Moonbit_array_length(_M0L6_2atmpS1909);
  moonbit_decref_cycle_free(_M0L6_2atmpS1909);
  if (_M0L3lenS1907 == _M0L6_2atmpS1908) {
    int32_t _M0L3lenS1911 = _M0L4selfS511->$1;
    int32_t _M0L6_2atmpS1910 = _M0L3lenS1911 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS511, _M0L6_2atmpS1910);
  }
  _M0L6lengthS512 = _M0L4selfS511->$1;
  _M0L3bufS1912 = _M0L4selfS511->$0;
  _M0L3bufS1912[_M0L6lengthS512] = _M0L5valueS513;
  _M0L6_2atmpS1913 = _M0L6lengthS512 + 1;
  _M0L4selfS511->$1 = _M0L6_2atmpS1913;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS514,
  int32_t _M0L5valueS516
) {
  int32_t _M0L3lenS1914;
  int32_t* _M0L6_2atmpS1916;
  int32_t _M0L6_2atmpS1915;
  int32_t _M0L6lengthS515;
  int32_t* _M0L3bufS1919;
  int32_t _M0L6_2atmpS1920;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1914 = _M0L4selfS514->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1916 = _M0MPC15array5Array6bufferGiE(_M0L4selfS514);
  _M0L6_2atmpS1915 = Moonbit_array_length(_M0L6_2atmpS1916);
  moonbit_decref_cycle_free(_M0L6_2atmpS1916);
  if (_M0L3lenS1914 == _M0L6_2atmpS1915) {
    int32_t _M0L3lenS1918 = _M0L4selfS514->$1;
    int32_t _M0L6_2atmpS1917 = _M0L3lenS1918 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS514, _M0L6_2atmpS1917);
  }
  _M0L6lengthS515 = _M0L4selfS514->$1;
  _M0L3bufS1919 = _M0L4selfS514->$0;
  _M0L3bufS1919[_M0L6lengthS515] = _M0L5valueS516;
  _M0L6_2atmpS1920 = _M0L6lengthS515 + 1;
  _M0L4selfS514->$1 = _M0L6_2atmpS1920;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS517,
  moonbit_string_t _M0L5valueS519
) {
  int32_t _M0L3lenS1921;
  moonbit_string_t* _M0L6_2atmpS1923;
  int32_t _M0L6_2atmpS1922;
  int32_t _M0L6lengthS518;
  moonbit_string_t* _M0L3bufS1926;
  moonbit_string_t _M0L6_2aoldS3437;
  int32_t _M0L6_2atmpS1927;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1921 = _M0L4selfS517->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1923 = _M0MPC15array5Array6bufferGsE(_M0L4selfS517);
  _M0L6_2atmpS1922 = Moonbit_array_length(_M0L6_2atmpS1923);
  moonbit_decref_cycle_free(_M0L6_2atmpS1923);
  if (_M0L3lenS1921 == _M0L6_2atmpS1922) {
    int32_t _M0L3lenS1925 = _M0L4selfS517->$1;
    int32_t _M0L6_2atmpS1924 = _M0L3lenS1925 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS517, _M0L6_2atmpS1924);
  }
  _M0L6lengthS518 = _M0L4selfS517->$1;
  _M0L3bufS1926 = _M0L4selfS517->$0;
  _M0L6_2aoldS3437 = (moonbit_string_t)_M0L3bufS1926[_M0L6lengthS518];
  moonbit_decref_cycle_free(_M0L6_2aoldS3437);
  _M0L3bufS1926[_M0L6lengthS518] = _M0L5valueS519;
  _M0L6_2atmpS1927 = _M0L6lengthS518 + 1;
  _M0L4selfS517->$1 = _M0L6_2atmpS1927;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS520,
  struct _M0TUsiE* _M0L5valueS522
) {
  int32_t _M0L3lenS1928;
  struct _M0TUsiE** _M0L6_2atmpS1930;
  int32_t _M0L6_2atmpS1929;
  int32_t _M0L6lengthS521;
  struct _M0TUsiE** _M0L3bufS1933;
  struct _M0TUsiE* _M0L6_2aoldS3438;
  int32_t _M0L6_2atmpS1934;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1928 = _M0L4selfS520->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1930 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS520);
  _M0L6_2atmpS1929 = Moonbit_array_length(_M0L6_2atmpS1930);
  moonbit_decref_cycle_free(_M0L6_2atmpS1930);
  if (_M0L3lenS1928 == _M0L6_2atmpS1929) {
    int32_t _M0L3lenS1932 = _M0L4selfS520->$1;
    int32_t _M0L6_2atmpS1931 = _M0L3lenS1932 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS520, _M0L6_2atmpS1931);
  }
  _M0L6lengthS521 = _M0L4selfS520->$1;
  _M0L3bufS1933 = _M0L4selfS520->$0;
  _M0L6_2aoldS3438 = (struct _M0TUsiE*)_M0L3bufS1933[_M0L6lengthS521];
  if (_M0L6_2aoldS3438) {
    moonbit_decref_cycle_free(_M0L6_2aoldS3438);
  }
  _M0L3bufS1933[_M0L6lengthS521] = _M0L5valueS522;
  _M0L6_2atmpS1934 = _M0L6lengthS521 + 1;
  _M0L4selfS520->$1 = _M0L6_2atmpS1934;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS496,
  int32_t _M0L8requiredS498
) {
  int32_t _M0L8old__capS495;
  int32_t _M0L3lenS1903;
  int32_t _M0L8new__capS497;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS495 = _M0MPC15array5Array8capacityGfE(_M0L4selfS496);
  _M0L3lenS1903 = _M0L4selfS496->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS497
  = _M0FPB23array__growth__capacity(_M0L8old__capS495, _M0L3lenS1903, _M0L8requiredS498);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS496, _M0L8new__capS497);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS500,
  int32_t _M0L8requiredS502
) {
  int32_t _M0L8old__capS499;
  int32_t _M0L3lenS1904;
  int32_t _M0L8new__capS501;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS499 = _M0MPC15array5Array8capacityGiE(_M0L4selfS500);
  _M0L3lenS1904 = _M0L4selfS500->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS501
  = _M0FPB23array__growth__capacity(_M0L8old__capS499, _M0L3lenS1904, _M0L8requiredS502);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS500, _M0L8new__capS501);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS504,
  int32_t _M0L8requiredS506
) {
  int32_t _M0L8old__capS503;
  int32_t _M0L3lenS1905;
  int32_t _M0L8new__capS505;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS503 = _M0MPC15array5Array8capacityGsE(_M0L4selfS504);
  _M0L3lenS1905 = _M0L4selfS504->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS505
  = _M0FPB23array__growth__capacity(_M0L8old__capS503, _M0L3lenS1905, _M0L8requiredS506);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS504, _M0L8new__capS505);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS508,
  int32_t _M0L8requiredS510
) {
  int32_t _M0L8old__capS507;
  int32_t _M0L3lenS1906;
  int32_t _M0L8new__capS509;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS507 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS508);
  _M0L3lenS1906 = _M0L4selfS508->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS509
  = _M0FPB23array__growth__capacity(_M0L8old__capS507, _M0L3lenS1906, _M0L8requiredS510);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS508, _M0L8new__capS509);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS472,
  int32_t _M0L13new__capacityS475
) {
  float* _M0L8old__bufS471;
  int32_t _M0L3lenS473;
  int32_t _M0L9copy__lenS474;
  float* _M0L8new__bufS476;
  float* _M0L6_2aoldS3439;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS471 = _M0L4selfS472->$0;
  _M0L3lenS473 = _M0L4selfS472->$1;
  if (_M0L3lenS473 < _M0L13new__capacityS475) {
    _M0L9copy__lenS474 = _M0L3lenS473;
  } else {
    _M0L9copy__lenS474 = _M0L13new__capacityS475;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS471);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS476
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS471, _M0L13new__capacityS475, _M0L9copy__lenS474, 0, 0);
  _M0L6_2aoldS3439 = _M0L4selfS472->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3439);
  _M0L4selfS472->$0 = _M0L8new__bufS476;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS478,
  int32_t _M0L13new__capacityS481
) {
  int32_t* _M0L8old__bufS477;
  int32_t _M0L3lenS479;
  int32_t _M0L9copy__lenS480;
  int32_t* _M0L8new__bufS482;
  int32_t* _M0L6_2aoldS3440;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS477 = _M0L4selfS478->$0;
  _M0L3lenS479 = _M0L4selfS478->$1;
  if (_M0L3lenS479 < _M0L13new__capacityS481) {
    _M0L9copy__lenS480 = _M0L3lenS479;
  } else {
    _M0L9copy__lenS480 = _M0L13new__capacityS481;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS477);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS482
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS477, _M0L13new__capacityS481, _M0L9copy__lenS480, 0, 0);
  _M0L6_2aoldS3440 = _M0L4selfS478->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3440);
  _M0L4selfS478->$0 = _M0L8new__bufS482;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS484,
  int32_t _M0L13new__capacityS487
) {
  moonbit_string_t* _M0L8old__bufS483;
  int32_t _M0L3lenS485;
  int32_t _M0L9copy__lenS486;
  moonbit_string_t* _M0L8new__bufS488;
  moonbit_string_t* _M0L6_2aoldS3441;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS483 = _M0L4selfS484->$0;
  _M0L3lenS485 = _M0L4selfS484->$1;
  if (_M0L3lenS485 < _M0L13new__capacityS487) {
    _M0L9copy__lenS486 = _M0L3lenS485;
  } else {
    _M0L9copy__lenS486 = _M0L13new__capacityS487;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS483);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS488
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS483, _M0L13new__capacityS487, _M0L9copy__lenS486, 0, 0);
  _M0L6_2aoldS3441 = _M0L4selfS484->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3441);
  _M0L4selfS484->$0 = _M0L8new__bufS488;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS490,
  int32_t _M0L13new__capacityS493
) {
  struct _M0TUsiE** _M0L8old__bufS489;
  int32_t _M0L3lenS491;
  int32_t _M0L9copy__lenS492;
  struct _M0TUsiE** _M0L8new__bufS494;
  struct _M0TUsiE** _M0L6_2aoldS3442;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS489 = _M0L4selfS490->$0;
  _M0L3lenS491 = _M0L4selfS490->$1;
  if (_M0L3lenS491 < _M0L13new__capacityS493) {
    _M0L9copy__lenS492 = _M0L3lenS491;
  } else {
    _M0L9copy__lenS492 = _M0L13new__capacityS493;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS489);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS494
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS489, _M0L13new__capacityS493, _M0L9copy__lenS492, 0, 0);
  _M0L6_2aoldS3442 = _M0L4selfS490->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3442);
  _M0L4selfS490->$0 = _M0L8new__bufS494;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS467
) {
  float* _M0L6_2atmpS1899;
  int32_t _result_3597;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1899 = _M0MPC15array5Array6bufferGfE(_M0L4selfS467);
  _result_3597 = Moonbit_array_length(_M0L6_2atmpS1899);
  moonbit_decref_cycle_free(_M0L6_2atmpS1899);
  return _result_3597;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS468
) {
  int32_t* _M0L6_2atmpS1900;
  int32_t _result_3598;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1900 = _M0MPC15array5Array6bufferGiE(_M0L4selfS468);
  _result_3598 = Moonbit_array_length(_M0L6_2atmpS1900);
  moonbit_decref_cycle_free(_M0L6_2atmpS1900);
  return _result_3598;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS469
) {
  moonbit_string_t* _M0L6_2atmpS1901;
  int32_t _result_3599;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1901 = _M0MPC15array5Array6bufferGsE(_M0L4selfS469);
  _result_3599 = Moonbit_array_length(_M0L6_2atmpS1901);
  moonbit_decref_cycle_free(_M0L6_2atmpS1901);
  return _result_3599;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS470
) {
  struct _M0TUsiE** _M0L6_2atmpS1902;
  int32_t _result_3600;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1902 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS470);
  _result_3600 = Moonbit_array_length(_M0L6_2atmpS1902);
  moonbit_decref_cycle_free(_M0L6_2atmpS1902);
  return _result_3600;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS463,
  int32_t _M0L3lenS461,
  int32_t _M0L8requiredS460
) {
  int32_t _M0L5startS462;
  int32_t _M0L5spaceS464;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS460 < _M0L3lenS461) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L7currentS463 == 0) {
    _M0L5startS462 = 8;
  } else {
    _M0L5startS462 = _M0L7currentS463;
  }
  _M0L5spaceS464 = _M0L5startS462;
  while (1) {
    if (_M0L5spaceS464 < _M0L8requiredS460) {
      int32_t _M0L4nextS465 = _M0L5spaceS464 * 2;
      if (_M0L4nextS465 <= _M0L5spaceS464) {
        return _M0L8requiredS460;
      }
      _M0L5spaceS464 = _M0L4nextS465;
      continue;
    } else {
      return _M0L5spaceS464;
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

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS3443;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3443 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3443);
  return _M0L8_2afieldS3443;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS453) {
  uint8_t* _M0L8_2afieldS3444;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3444 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3444);
  return _M0L8_2afieldS3444;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS3445;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3445 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3445);
  return _M0L8_2afieldS3445;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS3446;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3446 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3446);
  return _M0L8_2afieldS3446;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS3447;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3447 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3447);
  return _M0L8_2afieldS3447;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS3448;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3448 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3448);
  return _M0L8_2afieldS3448;
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
  int32_t _M0L3endS1897;
  int32_t _M0L5startS1898;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1896;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1889;
  int32_t _M0L6_2atmpS1888;
  int32_t _if__result_3602;
  uint16_t* _M0L4dataS1890;
  int32_t _M0L3lenS1891;
  moonbit_string_t _M0L6_2atmpS1892;
  int32_t _M0L6_2atmpS1893;
  int32_t _M0L3lenS1895;
  int32_t _M0L6_2atmpS1894;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1897 = _M0L3strS448.$2;
  _M0L5startS1898 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1897 - _M0L5startS1898;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1896 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1896 + _M0L8str__lenS447;
  _M0L4dataS1889 = _M0L4selfS450->$0;
  _M0L6_2atmpS1888 = Moonbit_array_length(_M0L4dataS1889);
  if (_M0L8requiredS449 > _M0L6_2atmpS1888) {
    _if__result_3602 = 1;
  } else {
    int32_t _M0L3lenS1887 = _M0L4selfS450->$1;
    _if__result_3602 = _M0L8requiredS449 < _M0L3lenS1887;
  }
  if (_if__result_3602) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1890 = _M0L4selfS450->$0;
  _M0L3lenS1891 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1890);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1892 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1893 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1890, _M0L3lenS1891, _M0L6_2atmpS1892, _M0L6_2atmpS1893, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1890);
  moonbit_decref_cycle_free(_M0L6_2atmpS1892);
  _M0L3lenS1895 = _M0L4selfS450->$1;
  _M0L6_2atmpS1894 = _M0L3lenS1895 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1894;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_3603;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1886;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1885;
  moonbit_string_t _result_3604;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1884 = Moonbit_array_length(_M0L3strS444);
    _if__result_3603 = _M0L3endS443 == _M0L6_2atmpS1884;
  } else {
    _if__result_3603 = 0;
  }
  if (_if__result_3603) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1886 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1886, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1885 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_3604
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1885, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1885);
  return _result_3604;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_3605;
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
      int32_t _M0L6_2atmpS1883 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_3605 = _M0L6_2atmpS1883 <= _M0L3lenS436;
    } else {
      _if__result_3605 = 0;
    }
  } else {
    _if__result_3605 = 0;
  }
  if (_if__result_3605) {
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
  int32_t _M0L6_2atmpS1882;
  int32_t _M0L6_2atmpS1881;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1880;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1882 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1881 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1882;
  _M0L2e1S422 = _M0L6_2atmpS1881 - 1;
  _M0L6_2atmpS1880 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1880 - 1;
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
        int32_t _M0L6_2atmpS1877 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1876 = (int32_t)_M0L6_2atmpS1877;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1876;
        uint32_t _M0L6_2atmpS1872 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1871;
        int32_t _M0L6_2atmpS1873;
        uint32_t _M0L6_2atmpS1875;
        int32_t _M0L6_2atmpS1874;
        int32_t _M0L6_2atmpS1878;
        int32_t _M0L6_2atmpS1879;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1871 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1872);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1871;
        _M0L6_2atmpS1873 = _M0L1jS433 + 1;
        _M0L6_2atmpS1875 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1874 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1875);
        if (
          _M0L6_2atmpS1873 < 0
          || _M0L6_2atmpS1873 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1873] = _M0L6_2atmpS1874;
        _M0L6_2atmpS1878 = _M0L1iS432 + 1;
        _M0L6_2atmpS1879 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1878;
        _M0L1jS433 = _M0L6_2atmpS1879;
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
  int32_t _M0L6_2atmpS1870;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1870 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1870 & 0xff;
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
    return (moonbit_string_t)moonbit_string_literal_13.data;
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
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1869 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1869;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1866;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1866 = 1;
      } else {
        _M0L6_2atmpS1866 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1866;
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
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1867 = 1;
      } else {
        _M0L6_2atmpS1867 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1867;
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
      int32_t _M0L6_2atmpS1868;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1868 = 1;
      } else {
        _M0L6_2atmpS1868 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1868;
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
  int32_t _M0L6_2atmpS1865;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1865 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1865;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1842 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1842;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1841 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1840 = 48 + _M0L6_2atmpS1841;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1840;
      int32_t _M0L6_2atmpS1839 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1838 = 48 + _M0L6_2atmpS1839;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1838;
      int32_t _M0L6_2atmpS1837 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1836 = 48 + _M0L6_2atmpS1837;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1836;
      int32_t _M0L6_2atmpS1835 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1834 = 48 + _M0L6_2atmpS1835;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1834;
      int32_t _M0L6_2atmpS1826 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1825 = _M0L6_2atmpS1826 - 4;
      int32_t _M0L6_2atmpS1828;
      int32_t _M0L6_2atmpS1827;
      int32_t _M0L6_2atmpS1830;
      int32_t _M0L6_2atmpS1829;
      int32_t _M0L6_2atmpS1832;
      int32_t _M0L6_2atmpS1831;
      int32_t _M0L6_2atmpS1833;
      _M0L6bufferS381[_M0L6_2atmpS1825] = _M0L6d1__hiS377;
      _M0L6_2atmpS1828 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1827 = _M0L6_2atmpS1828 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1827] = _M0L6d1__loS378;
      _M0L6_2atmpS1830 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1829 = _M0L6_2atmpS1830 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1829] = _M0L6d2__hiS379;
      _M0L6_2atmpS1832 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1831 = _M0L6_2atmpS1832 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1831] = _M0L6d2__loS380;
      _M0L6_2atmpS1833 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1833;
      continue;
    } else {
      int32_t _M0L6_2atmpS1864 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1864;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1851 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1850 = 48 + _M0L6_2atmpS1851;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1850;
          int32_t _M0L6_2atmpS1849 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1848 = 48 + _M0L6_2atmpS1849;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1848;
          int32_t _M0L6_2atmpS1844 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1843 = _M0L6_2atmpS1844 - 2;
          int32_t _M0L6_2atmpS1846;
          int32_t _M0L6_2atmpS1845;
          int32_t _M0L6_2atmpS1847;
          _M0L6bufferS381[_M0L6_2atmpS1843] = _M0L5d__hiS388;
          _M0L6_2atmpS1846 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1845 = _M0L6_2atmpS1846 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1845] = _M0L5d__loS389;
          _M0L6_2atmpS1847 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1847;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1859 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1858 = 48 + _M0L6_2atmpS1859;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1858;
          int32_t _M0L6_2atmpS1857 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1856 = 48 + _M0L6_2atmpS1857;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1856;
          int32_t _M0L6_2atmpS1853 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1852 = _M0L6_2atmpS1853 - 2;
          int32_t _M0L6_2atmpS1855;
          int32_t _M0L6_2atmpS1854;
          _M0L6bufferS381[_M0L6_2atmpS1852] = _M0L5d__hiS391;
          _M0L6_2atmpS1855 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1854 = _M0L6_2atmpS1855 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1854] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1863 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1860 = _M0L6_2atmpS1863 - 1;
          int32_t _M0L6_2atmpS1862 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1861 = (uint16_t)_M0L6_2atmpS1862;
          _M0L6bufferS381[_M0L6_2atmpS1860] = _M0L6_2atmpS1861;
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
  int32_t _M0L6_2atmpS1810;
  int32_t _M0L6_2atmpS1809;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1810 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1809 = _M0L5radixS355 & _M0L6_2atmpS1810;
  if (_M0L6_2atmpS1809 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1817;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1817 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1817;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1816 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1816;
        int32_t _M0L6_2atmpS1813 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1811 = _M0L6_2atmpS1813 - 1;
        int32_t _M0L6_2atmpS1812 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1814;
        uint64_t _M0L6_2atmpS1815;
        _M0L6bufferS361[_M0L6_2atmpS1811] = _M0L6_2atmpS1812;
        _M0L6_2atmpS1814 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1815 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1814;
        _M0L1nS359 = _M0L6_2atmpS1815;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1824 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1824;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1823 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1822 = _M0L1nS367 - _M0L6_2atmpS1823;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1822;
        int32_t _M0L6_2atmpS1820 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1818 = _M0L6_2atmpS1820 - 1;
        int32_t _M0L6_2atmpS1819 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1821;
        _M0L6bufferS361[_M0L6_2atmpS1818] = _M0L6_2atmpS1819;
        _M0L6_2atmpS1821 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1821;
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
  int32_t _M0L6_2atmpS1808;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1808 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1808;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1805 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1805;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1799 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1797 = _M0L6_2atmpS1799 - 2;
      int32_t _M0L6_2atmpS1798 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1802;
      int32_t _M0L6_2atmpS1800;
      int32_t _M0L6_2atmpS1801;
      int32_t _M0L6_2atmpS1803;
      uint64_t _M0L6_2atmpS1804;
      _M0L6bufferS348[_M0L6_2atmpS1797] = _M0L6_2atmpS1798;
      _M0L6_2atmpS1802 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1800 = _M0L6_2atmpS1802 - 1;
      _M0L6_2atmpS1801
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1800] = _M0L6_2atmpS1801;
      _M0L6_2atmpS1803 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1804 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1803;
      _M0L1nS344 = _M0L6_2atmpS1804;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1807 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1807;
      int32_t _M0L6_2atmpS1806 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1806;
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
      uint64_t _M0L6_2atmpS1795 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1796 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1795;
      _M0L5countS341 = _M0L6_2atmpS1796;
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
    int32_t _M0L6_2atmpS1794;
    int32_t _M0L6_2atmpS1793;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1794 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1793 = _M0L6_2atmpS1794 / 4;
    return _M0L6_2atmpS1793 + 1;
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
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1792 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1792;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1789;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1789 = 1;
      } else {
        _M0L6_2atmpS1789 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1789;
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
      int32_t _M0L6_2atmpS1790;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1790 = 1;
      } else {
        _M0L6_2atmpS1790 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1790;
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
      int32_t _M0L6_2atmpS1791;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1791 = 1;
      } else {
        _M0L6_2atmpS1791 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1791;
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
      uint32_t _M0L6_2atmpS1787 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1788 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1787;
      _M0L5countS315 = _M0L6_2atmpS1788;
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
    int32_t _M0L6_2atmpS1786;
    int32_t _M0L6_2atmpS1785;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1786 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1785 = _M0L6_2atmpS1786 / 4;
    return _M0L6_2atmpS1785 + 1;
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
  int32_t _M0L6_2atmpS1784;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1784 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1784;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1761 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1761;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1760 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1759 = 48 + _M0L6_2atmpS1760;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1758 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1757 = 48 + _M0L6_2atmpS1758;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1757;
      int32_t _M0L6_2atmpS1756 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1755 = 48 + _M0L6_2atmpS1756;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1755;
      int32_t _M0L6_2atmpS1754 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1753 = 48 + _M0L6_2atmpS1754;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1753;
      int32_t _M0L6_2atmpS1745 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1744 = _M0L6_2atmpS1745 - 4;
      int32_t _M0L6_2atmpS1747;
      int32_t _M0L6_2atmpS1746;
      int32_t _M0L6_2atmpS1749;
      int32_t _M0L6_2atmpS1748;
      int32_t _M0L6_2atmpS1751;
      int32_t _M0L6_2atmpS1750;
      int32_t _M0L6_2atmpS1752;
      _M0L6bufferS294[_M0L6_2atmpS1744] = _M0L6d1__hiS290;
      _M0L6_2atmpS1747 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1746 = _M0L6_2atmpS1747 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1746] = _M0L6d1__loS291;
      _M0L6_2atmpS1749 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1748 = _M0L6_2atmpS1749 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1748] = _M0L6d2__hiS292;
      _M0L6_2atmpS1751 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1750 = _M0L6_2atmpS1751 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1750] = _M0L6d2__loS293;
      _M0L6_2atmpS1752 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1752;
      continue;
    } else {
      int32_t _M0L6_2atmpS1783 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1783;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1770 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1769 = 48 + _M0L6_2atmpS1770;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1769;
          int32_t _M0L6_2atmpS1768 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1767 = 48 + _M0L6_2atmpS1768;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1767;
          int32_t _M0L6_2atmpS1763 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1762 = _M0L6_2atmpS1763 - 2;
          int32_t _M0L6_2atmpS1765;
          int32_t _M0L6_2atmpS1764;
          int32_t _M0L6_2atmpS1766;
          _M0L6bufferS294[_M0L6_2atmpS1762] = _M0L5d__hiS301;
          _M0L6_2atmpS1765 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1764 = _M0L6_2atmpS1765 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1764] = _M0L5d__loS302;
          _M0L6_2atmpS1766 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1766;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1778 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1777 = 48 + _M0L6_2atmpS1778;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1777;
          int32_t _M0L6_2atmpS1776 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1775 = 48 + _M0L6_2atmpS1776;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1775;
          int32_t _M0L6_2atmpS1772 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1771 = _M0L6_2atmpS1772 - 2;
          int32_t _M0L6_2atmpS1774;
          int32_t _M0L6_2atmpS1773;
          _M0L6bufferS294[_M0L6_2atmpS1771] = _M0L5d__hiS304;
          _M0L6_2atmpS1774 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1773 = _M0L6_2atmpS1774 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1773] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1782 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1779 = _M0L6_2atmpS1782 - 1;
          int32_t _M0L6_2atmpS1781 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1780 = (uint16_t)_M0L6_2atmpS1781;
          _M0L6bufferS294[_M0L6_2atmpS1779] = _M0L6_2atmpS1780;
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
  int32_t _M0L6_2atmpS1729;
  int32_t _M0L6_2atmpS1728;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1729 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1728 = _M0L5radixS268 & _M0L6_2atmpS1729;
  if (_M0L6_2atmpS1728 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1736;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1736 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1736;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1735 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1735;
        int32_t _M0L6_2atmpS1732 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1730 = _M0L6_2atmpS1732 - 1;
        int32_t _M0L6_2atmpS1731 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1733;
        uint32_t _M0L6_2atmpS1734;
        _M0L6bufferS274[_M0L6_2atmpS1730] = _M0L6_2atmpS1731;
        _M0L6_2atmpS1733 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1734 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1733;
        _M0L1nS272 = _M0L6_2atmpS1734;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1743 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1743;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1742 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1741 = _M0L1nS280 - _M0L6_2atmpS1742;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1741;
        int32_t _M0L6_2atmpS1739 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1737 = _M0L6_2atmpS1739 - 1;
        int32_t _M0L6_2atmpS1738 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1740;
        _M0L6bufferS274[_M0L6_2atmpS1737] = _M0L6_2atmpS1738;
        _M0L6_2atmpS1740 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1740;
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
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1727 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1727;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1724 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1724;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1718 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1716 = _M0L6_2atmpS1718 - 2;
      int32_t _M0L6_2atmpS1717 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1721;
      int32_t _M0L6_2atmpS1719;
      int32_t _M0L6_2atmpS1720;
      int32_t _M0L6_2atmpS1722;
      uint32_t _M0L6_2atmpS1723;
      _M0L6bufferS261[_M0L6_2atmpS1716] = _M0L6_2atmpS1717;
      _M0L6_2atmpS1721 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1719 = _M0L6_2atmpS1721 - 1;
      _M0L6_2atmpS1720
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1719] = _M0L6_2atmpS1720;
      _M0L6_2atmpS1722 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1723 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1722;
      _M0L1nS257 = _M0L6_2atmpS1723;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1726 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1726;
      int32_t _M0L6_2atmpS1725 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1725;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1715;
  moonbit_string_t _result_3619;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1715
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1715);
  if (_M0L6_2atmpS1715.$1) {
    moonbit_decref(_M0L6_2atmpS1715.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_3619 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_3619;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1712;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1712 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1712);
  moonbit_decref_cycle_free(_M0L6_2atmpS1712);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1713;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1713 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1713);
  moonbit_decref_cycle_free(_M0L6_2atmpS1713);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1714;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1714 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1714);
  moonbit_decref_cycle_free(_M0L6_2atmpS1714);
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
  moonbit_string_t _M0L8_2afieldS3449;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS3449 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3449);
  return _M0L8_2afieldS3449;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1711;
  int64_t _M0L6_2atmpS1710;
  struct _M0TPC16string10StringView _M0L6_2atmpS1709;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1711 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1710 = (int64_t)_M0L6_2atmpS1711;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1709
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1710);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1709);
  moonbit_decref_cycle_free(_M0L6_2atmpS1709.$0);
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
  int32_t _M0L6_2atmpS1693;
  int32_t _if__result_3620;
  int32_t _M0L6_2atmpS1701;
  int32_t _if__result_3621;
  int32_t _M0L6_2atmpS1703;
  int32_t _M0L6_2atmpS1704;
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
  _M0L6_2atmpS1693 = _M0Lm2loS236;
  if (_M0L6_2atmpS1693 > 0) {
    int32_t _M0L6_2atmpS1692 = _M0Lm2loS236;
    if (_M0L6_2atmpS1692 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1691 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1690 = _M0L4selfS235[_M0L6_2atmpS1691];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1690)) {
        int32_t _M0L6_2atmpS1689 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1688 = _M0L6_2atmpS1689 - 1;
        int32_t _M0L6_2atmpS1687 = _M0L4selfS235[_M0L6_2atmpS1688];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3620
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1687);
      } else {
        _if__result_3620 = 0;
      }
    } else {
      _if__result_3620 = 0;
    }
  } else {
    _if__result_3620 = 0;
  }
  if (_if__result_3620) {
    int32_t _M0L6_2atmpS1694 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1694 + 1;
  }
  _M0L6_2atmpS1701 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1701 > 0) {
    int32_t _M0L6_2atmpS1700 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1700 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1699 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1698 = _M0L4selfS235[_M0L6_2atmpS1699];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1698)) {
        int32_t _M0L6_2atmpS1697 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1696 = _M0L6_2atmpS1697 - 1;
        int32_t _M0L6_2atmpS1695 = _M0L4selfS235[_M0L6_2atmpS1696];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3621
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1695);
      } else {
        _if__result_3621 = 0;
      }
    } else {
      _if__result_3621 = 0;
    }
  } else {
    _if__result_3621 = 0;
  }
  if (_if__result_3621) {
    int32_t _M0L6_2atmpS1702 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1702 - 1;
  }
  _M0L6_2atmpS1703 = _M0Lm2loS236;
  _M0L6_2atmpS1704 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1703 >= _M0L6_2atmpS1704) {
    int32_t _M0L6_2atmpS1705 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1706 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1705,
                                                 .$2 = _M0L6_2atmpS1706};
  } else {
    int32_t _M0L6_2atmpS1707 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1708 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1707,
                                                 .$2 = _M0L6_2atmpS1708};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1686;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1686
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1686);
  if (_M0L6_2atmpS1686.$1) {
    moonbit_decref(_M0L6_2atmpS1686.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1685;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1685
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1685);
  if (_M0L6_2atmpS1685.$1) {
    moonbit_decref(_M0L6_2atmpS1685.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1684;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1684 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1684;
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
  int32_t _M0L6_2atmpS1683;
  struct _M0TPC16string10StringView _M0L6_2atmpS1681;
  struct _M0TPB6Logger _M0L6_2atmpS1682;
  moonbit_string_t _result_3622;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1683 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1681
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1683
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1682
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1681, _M0L6_2atmpS1682, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1681.$0);
  if (_M0L6_2atmpS1682.$1) {
    moonbit_decref(_M0L6_2atmpS1682.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3622 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_3622;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1679;
  int32_t _M0L5startS1680;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1679 = _M0L4selfS218.$2;
  _M0L5startS1680 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1679 - _M0L5startS1680;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 132, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1676;
    int32_t _M0L5startS1678;
    int32_t _M0L6_2atmpS1677;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1660;
    int32_t _M0L6_2atmpS1661;
    int32_t _M0L6_2atmpS1662;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1676 = _M0L4selfS218.$0;
    _M0L5startS1678 = _M0L4selfS218.$1;
    _M0L6_2atmpS1677 = _M0L5startS1678 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1676[_M0L6_2atmpS1677];
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
        int32_t _M0L6_2atmpS1663;
        int32_t _M0L6_2atmpS1664;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1663 = _M0L1iS220 + 1;
        _M0L6_2atmpS1664 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1663;
        _M0L3segS221 = _M0L6_2atmpS1664;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1665;
        int32_t _M0L6_2atmpS1666;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1665 = _M0L1iS220 + 1;
        _M0L6_2atmpS1666 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1665;
        _M0L3segS221 = _M0L6_2atmpS1666;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1667;
        int32_t _M0L6_2atmpS1668;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1667 = _M0L1iS220 + 1;
        _M0L6_2atmpS1668 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1667;
        _M0L3segS221 = _M0L6_2atmpS1668;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1669;
        int32_t _M0L6_2atmpS1670;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1669 = _M0L1iS220 + 1;
        _M0L6_2atmpS1670 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1669;
        _M0L3segS221 = _M0L6_2atmpS1670;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1672;
          moonbit_string_t _M0L6_2atmpS1671;
          int32_t _M0L6_2atmpS1673;
          int32_t _M0L6_2atmpS1674;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
          _M0L6_2atmpS1672 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1671 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1672);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1671);
          moonbit_decref_cycle_free(_M0L6_2atmpS1671);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1673 = _M0L1iS220 + 1;
          _M0L6_2atmpS1674 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1673;
          _M0L3segS221 = _M0L6_2atmpS1674;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1675 = _M0L1iS220 + 1;
          int32_t _tmp_3625 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1675;
          _M0L3segS221 = _tmp_3625;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_3624;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1660 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1660);
    _M0L6_2atmpS1661 = _M0L1iS220 + 1;
    _M0L6_2atmpS1662 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1661;
    _M0L3segS221 = _M0L6_2atmpS1662;
    continue;
    joinlet_3624:;
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
    int64_t _M0L6_2atmpS1659 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1658;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1658
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1659);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1658);
    moonbit_decref_cycle_free(_M0L6_2atmpS1658.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1656;
  int32_t _M0L5startS1657;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1634;
  int32_t _if__result_3626;
  int32_t _M0L6_2atmpS1644;
  int32_t _if__result_3627;
  int32_t _M0L6_2atmpS1646;
  int32_t _M0L6_2atmpS1647;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1656 = _M0L4selfS201.$2;
  _M0L5startS1657 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1656 - _M0L5startS1657;
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
  _M0L6_2atmpS1634 = _M0Lm2loS202;
  if (_M0L6_2atmpS1634 > 0) {
    int32_t _M0L6_2atmpS1633 = _M0Lm2loS202;
    if (_M0L6_2atmpS1633 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1632 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1631 = _M0L4baseS209 + _M0L6_2atmpS1632;
      int32_t _M0L6_2atmpS1630 = _M0L3strS208[_M0L6_2atmpS1631];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1630)) {
        int32_t _M0L6_2atmpS1629 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1628 = _M0L4baseS209 + _M0L6_2atmpS1629;
        int32_t _M0L6_2atmpS1627 = _M0L6_2atmpS1628 - 1;
        int32_t _M0L6_2atmpS1626 = _M0L3strS208[_M0L6_2atmpS1627];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3626
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1626);
      } else {
        _if__result_3626 = 0;
      }
    } else {
      _if__result_3626 = 0;
    }
  } else {
    _if__result_3626 = 0;
  }
  if (_if__result_3626) {
    int32_t _M0L6_2atmpS1635 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1635 + 1;
  }
  _M0L6_2atmpS1644 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1644 > 0) {
    int32_t _M0L6_2atmpS1643 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1643 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1642 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1641 = _M0L4baseS209 + _M0L6_2atmpS1642;
      int32_t _M0L6_2atmpS1640 = _M0L3strS208[_M0L6_2atmpS1641];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1640)) {
        int32_t _M0L6_2atmpS1639 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1638 = _M0L4baseS209 + _M0L6_2atmpS1639;
        int32_t _M0L6_2atmpS1637 = _M0L6_2atmpS1638 - 1;
        int32_t _M0L6_2atmpS1636 = _M0L3strS208[_M0L6_2atmpS1637];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3627
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1636);
      } else {
        _if__result_3627 = 0;
      }
    } else {
      _if__result_3627 = 0;
    }
  } else {
    _if__result_3627 = 0;
  }
  if (_if__result_3627) {
    int32_t _M0L6_2atmpS1645 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1645 - 1;
  }
  _M0L6_2atmpS1646 = _M0Lm2loS202;
  _M0L6_2atmpS1647 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1646 >= _M0L6_2atmpS1647) {
    int32_t _M0L6_2atmpS1651 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1648 = _M0L4baseS209 + _M0L6_2atmpS1651;
    int32_t _M0L6_2atmpS1650 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1649 = _M0L4baseS209 + _M0L6_2atmpS1650;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1648,
                                                 .$2 = _M0L6_2atmpS1649};
  } else {
    int32_t _M0L6_2atmpS1655 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1652 = _M0L4baseS209 + _M0L6_2atmpS1655;
    int32_t _M0L6_2atmpS1654 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1653 = _M0L4baseS209 + _M0L6_2atmpS1654;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1652,
                                                 .$2 = _M0L6_2atmpS1653};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1623;
  int32_t _M0L6_2atmpS1622;
  int32_t _M0L6_2atmpS1625;
  int32_t _M0L6_2atmpS1624;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1621;
  moonbit_string_t _result_3628;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1623 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1622
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1623);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1622);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1625 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1624
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1625);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1624);
  _M0L6_2atmpS1621 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3628 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1621);
  moonbit_decref_cycle_free(_M0L6_2atmpS1621);
  return _result_3628;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1618;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1618 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1618);
  } else {
    int32_t _M0L6_2atmpS1620;
    int32_t _M0L6_2atmpS1619;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1620 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1619 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1620, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1619);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1616;
  int32_t _M0L6_2atmpS1617;
  int32_t _M0L6_2atmpS1615;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1616 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1617 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1615 = _M0L6_2atmpS1616 - _M0L6_2atmpS1617;
  return _M0L6_2atmpS1615 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1613;
  int32_t _M0L6_2atmpS1614;
  int32_t _M0L6_2atmpS1612;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1613 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1614 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1612 = _M0L6_2atmpS1613 % _M0L6_2atmpS1614;
  return _M0L6_2atmpS1612 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1610;
  int32_t _M0L6_2atmpS1611;
  int32_t _M0L6_2atmpS1609;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1610 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1611 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1609 = _M0L6_2atmpS1610 / _M0L6_2atmpS1611;
  return _M0L6_2atmpS1609 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1607;
  int32_t _M0L6_2atmpS1608;
  int32_t _M0L6_2atmpS1606;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1607 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1608 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1606 = _M0L6_2atmpS1607 + _M0L6_2atmpS1608;
  return _M0L6_2atmpS1606 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1605;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1605 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1605;
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
  int32_t _M0L3lenS1604;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1599;
  int32_t _M0L6_2atmpS1598;
  int32_t _if__result_3629;
  uint16_t* _M0L4dataS1600;
  int32_t _M0L3lenS1601;
  int32_t _M0L3lenS1603;
  int32_t _M0L6_2atmpS1602;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1604 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1604 + _M0L8str__lenS182;
  _M0L4dataS1599 = _M0L4selfS185->$0;
  _M0L6_2atmpS1598 = Moonbit_array_length(_M0L4dataS1599);
  if (_M0L8requiredS184 > _M0L6_2atmpS1598) {
    _if__result_3629 = 1;
  } else {
    int32_t _M0L3lenS1597 = _M0L4selfS185->$1;
    _if__result_3629 = _M0L8requiredS184 < _M0L3lenS1597;
  }
  if (_if__result_3629) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1600 = _M0L4selfS185->$0;
  _M0L3lenS1601 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1600);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1600, _M0L3lenS1601, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1600);
  _M0L3lenS1603 = _M0L4selfS185->$1;
  _M0L6_2atmpS1602 = _M0L3lenS1603 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1602;
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
      int32_t _M0L6_2atmpS1594 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1596;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1594;
      _M0L6_2atmpS1595 = _M0L1iS176 + 1;
      _M0L6_2atmpS1596 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1595;
      _M0L1jS177 = _M0L6_2atmpS1596;
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
    int32_t _M0L3lenS1565 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1567 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1566 = Moonbit_array_length(_M0L4dataS1567);
    uint16_t* _M0L4dataS1570;
    int32_t _M0L3lenS1571;
    int32_t _M0L6_2atmpS1572;
    int32_t _M0L3lenS1574;
    int32_t _M0L6_2atmpS1573;
    if (_M0L3lenS1565 >= _M0L6_2atmpS1566) {
      int32_t _M0L3lenS1569 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1568 = _M0L3lenS1569 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1568);
    }
    _M0L4dataS1570 = _M0L4selfS171->$0;
    _M0L3lenS1571 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1570);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1572 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1571 < 0
      || _M0L3lenS1571 >= Moonbit_array_length(_M0L4dataS1570)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1570[_M0L3lenS1571] = _M0L6_2atmpS1572;
    moonbit_decref_cycle_free(_M0L4dataS1570);
    _M0L3lenS1574 = _M0L4selfS171->$1;
    _M0L6_2atmpS1573 = _M0L3lenS1574 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1573;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1578 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1576 = Moonbit_array_length(_M0L4dataS1578);
    int32_t _M0L3lenS1577 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1575 = _M0L6_2atmpS1576 - _M0L3lenS1577;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1581;
    int32_t _M0L3lenS1582;
    uint32_t _M0L6_2atmpS1585;
    uint32_t _M0L6_2atmpS1584;
    int32_t _M0L6_2atmpS1583;
    uint16_t* _M0L4dataS1586;
    int32_t _M0L3lenS1591;
    int32_t _M0L6_2atmpS1587;
    uint32_t _M0L6_2atmpS1590;
    uint32_t _M0L6_2atmpS1589;
    int32_t _M0L6_2atmpS1588;
    int32_t _M0L3lenS1593;
    int32_t _M0L6_2atmpS1592;
    if (_M0L6_2atmpS1575 < 2) {
      int32_t _M0L3lenS1580 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1579 = _M0L3lenS1580 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1579);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1581 = _M0L4selfS171->$0;
    _M0L3lenS1582 = _M0L4selfS171->$1;
    _M0L6_2atmpS1585 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1584 = 55296u + _M0L6_2atmpS1585;
    moonbit_incref_cycle_free(_M0L4dataS1581);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1583 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1584);
    if (
      _M0L3lenS1582 < 0
      || _M0L3lenS1582 >= Moonbit_array_length(_M0L4dataS1581)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1581[_M0L3lenS1582] = _M0L6_2atmpS1583;
    moonbit_decref_cycle_free(_M0L4dataS1581);
    _M0L4dataS1586 = _M0L4selfS171->$0;
    _M0L3lenS1591 = _M0L4selfS171->$1;
    _M0L6_2atmpS1587 = _M0L3lenS1591 + 1;
    _M0L6_2atmpS1590 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1589 = 56320u + _M0L6_2atmpS1590;
    moonbit_incref_cycle_free(_M0L4dataS1586);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1588 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1589);
    if (
      _M0L6_2atmpS1587 < 0
      || _M0L6_2atmpS1587 >= Moonbit_array_length(_M0L4dataS1586)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1586[_M0L6_2atmpS1587] = _M0L6_2atmpS1588;
    moonbit_decref_cycle_free(_M0L4dataS1586);
    _M0L3lenS1593 = _M0L4selfS171->$1;
    _M0L6_2atmpS1592 = _M0L3lenS1593 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1592;
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
  uint16_t* _M0L4dataS1564;
  int32_t _M0L6_2atmpS1562;
  int32_t _M0L3lenS1563;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1559;
  int32_t _M0L6_2atmpS1560;
  int32_t _M0L3lenS1561;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS3450;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1564 = _M0L4selfS166->$0;
  _M0L6_2atmpS1562 = Moonbit_array_length(_M0L4dataS1564);
  _M0L3lenS1563 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1562, _M0L3lenS1563, _M0L8requiredS167);
  _M0L4dataS1559 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1559);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1560 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1561 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1559, _M0L13new__capacityS165, _M0L6_2atmpS1560, _M0L3lenS1561, 0, 0);
  _M0L6_2aoldS3450 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3450);
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
  int32_t _M0L6_2atmpS1558;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1558 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1558;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1557;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1557 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1557;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1548;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1548 = _M0L4selfS155->$1;
  if (_M0L3lenS1548 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1549 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1551 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1550 = Moonbit_array_length(_M0L4dataS1551);
    if (_M0L3lenS1549 == _M0L6_2atmpS1550) {
      uint16_t* _M0L4dataS1552 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1552);
      return _M0L4dataS1552;
    } else {
      uint16_t* _M0L4dataS1553 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1554 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1555;
      int32_t _M0L3lenS1556;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1553);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1555 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1556 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1553, _M0L3lenS1554, _M0L6_2atmpS1555, _M0L3lenS1556, 0, 0);
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
  int32_t _if__result_3632;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1544 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1545 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1544 <= _M0L6_2atmpS1545) {
            int32_t _M0L6_2atmpS1543 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_3632 = _M0L6_2atmpS1543 <= _M0L13allocate__lenS148;
          } else {
            _if__result_3632 = 0;
          }
        } else {
          _if__result_3632 = 0;
        }
      } else {
        _if__result_3632 = 0;
      }
    } else {
      _if__result_3632 = 0;
    }
  } else {
    _if__result_3632 = 0;
  }
  if (_if__result_3632) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1547;
    moonbit_string_t _M0L6_2atmpS1546;
    uint16_t* _result_3633;
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
    _M0L6_2atmpS1547 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1547);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1546
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_3633 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1546);
    moonbit_decref_cycle_free(_M0L6_2atmpS1546);
    return _result_3633;
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
  struct _M0TPB13StringBuilder* _block_3634;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1542 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1542 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_3634
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_3634)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 137, 0);
  _block_3634->$0 = _M0L4dataS140;
  _block_3634->$1 = 0;
  return _block_3634;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1541;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1541 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1541;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_3635;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1522 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1523;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1523
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
          if (_M0L6_2atmpS1522 <= _M0L6_2atmpS1523) {
            int32_t _M0L6_2atmpS1521 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_3635 = _M0L6_2atmpS1521 <= _M0L13allocate__lenS113;
          } else {
            _if__result_3635 = 0;
          }
        } else {
          _if__result_3635 = 0;
        }
      } else {
        _if__result_3635 = 0;
      }
    } else {
      _if__result_3635 = 0;
    }
  } else {
    _if__result_3635 = 0;
  }
  if (_if__result_3635) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1525;
    moonbit_string_t _M0L6_2atmpS1524;
    float* _result_3636;
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
    _M0L6_2atmpS1525 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1525);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1524
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3636
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1524);
    moonbit_decref_cycle_free(_M0L6_2atmpS1524);
    return _result_3636;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_3637;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1527 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1528;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1528
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS123);
          if (_M0L6_2atmpS1527 <= _M0L6_2atmpS1528) {
            int32_t _M0L6_2atmpS1526 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_3637 = _M0L6_2atmpS1526 <= _M0L13allocate__lenS119;
          } else {
            _if__result_3637 = 0;
          }
        } else {
          _if__result_3637 = 0;
        }
      } else {
        _if__result_3637 = 0;
      }
    } else {
      _if__result_3637 = 0;
    }
  } else {
    _if__result_3637 = 0;
  }
  if (_if__result_3637) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS123, _M0L13allocate__lenS119, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1530;
    moonbit_string_t _M0L6_2atmpS1529;
    int32_t* _result_3638;
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
    _M0L6_2atmpS1530 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1530);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1529
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3638
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1529);
    moonbit_decref_cycle_free(_M0L6_2atmpS1529);
    return _result_3638;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_3639;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1532 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1533;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1533
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
          if (_M0L6_2atmpS1532 <= _M0L6_2atmpS1533) {
            int32_t _M0L6_2atmpS1531 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_3639 = _M0L6_2atmpS1531 <= _M0L13allocate__lenS125;
          } else {
            _if__result_3639 = 0;
          }
        } else {
          _if__result_3639 = 0;
        }
      } else {
        _if__result_3639 = 0;
      }
    } else {
      _if__result_3639 = 0;
    }
  } else {
    _if__result_3639 = 0;
  }
  if (_if__result_3639) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1535;
    moonbit_string_t _M0L6_2atmpS1534;
    moonbit_string_t* _result_3640;
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
    _M0L6_2atmpS1535 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1535);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1534
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3640
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1534);
    moonbit_decref_cycle_free(_M0L6_2atmpS1534);
    return _result_3640;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_3641;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1537 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1538;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1538
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
          if (_M0L6_2atmpS1537 <= _M0L6_2atmpS1538) {
            int32_t _M0L6_2atmpS1536 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_3641 = _M0L6_2atmpS1536 <= _M0L13allocate__lenS131;
          } else {
            _if__result_3641 = 0;
          }
        } else {
          _if__result_3641 = 0;
        }
      } else {
        _if__result_3641 = 0;
      }
    } else {
      _if__result_3641 = 0;
    }
  } else {
    _if__result_3641 = 0;
  }
  if (_if__result_3641) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS131, 0, _M0L3srcS135, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1540;
    moonbit_string_t _M0L6_2atmpS1539;
    struct _M0TUsiE** _result_3642;
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
    _M0L6_2atmpS1540 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1540);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1539
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3642
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1539);
    moonbit_decref_cycle_free(_M0L6_2atmpS1539);
    return _result_3642;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1518;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1518
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1518);
  if (_M0L6_2atmpS1518.$1) {
    moonbit_decref(_M0L6_2atmpS1518.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1519;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1519
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1519);
  if (_M0L6_2atmpS1519.$1) {
    moonbit_decref(_M0L6_2atmpS1519.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1520;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1520
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1520);
  if (_M0L6_2atmpS1520.$1) {
    moonbit_decref(_M0L6_2atmpS1520.$1);
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  int32_t* _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS90);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  moonbit_string_t* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS96, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  struct _M0TUsiE** _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS102, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
  moonbit_decref_cycle_free(_M0L3srcS104);
  return _M0L3dstS101;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  float* _M0L3srcS65,
  int32_t _M0L11src__offsetS66,
  int32_t _M0L3lenS67
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS65);
  moonbit_incref_cycle_free(_M0L3dstS63);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS63, _M0L11dst__offsetS64, _M0L3srcS65, _M0L11src__offsetS66, _M0L3lenS67, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  int32_t* _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72, sizeof(int32_t));
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS18,
  int32_t _M0L11dst__offsetS20,
  uint16_t* _M0L3srcS19,
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
        int32_t _M0L6_2atmpS1473 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1475 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1474;
        int32_t _M0L6_2atmpS1476;
        if (
          _M0L6_2atmpS1475 < 0
          || _M0L6_2atmpS1475 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1474 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1475];
        if (
          _M0L6_2atmpS1473 < 0
          || _M0L6_2atmpS1473 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1473] = _M0L6_2atmpS1474;
        _M0L6_2atmpS1476 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1476;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1481 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1481;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1477 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1479 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1478;
        int32_t _M0L6_2atmpS1480;
        if (
          _M0L6_2atmpS1479 < 0
          || _M0L6_2atmpS1479 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1478 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1479];
        if (
          _M0L6_2atmpS1477 < 0
          || _M0L6_2atmpS1477 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1477] = _M0L6_2atmpS1478;
        _M0L6_2atmpS1480 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1480;
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
        int32_t _M0L6_2atmpS1482 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1484 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1483;
        int32_t _M0L6_2atmpS1485;
        if (
          _M0L6_2atmpS1484 < 0
          || _M0L6_2atmpS1484 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1483 = (float)_M0L3srcS28[_M0L6_2atmpS1484];
        if (
          _M0L6_2atmpS1482 < 0
          || _M0L6_2atmpS1482 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1482] = _M0L6_2atmpS1483;
        _M0L6_2atmpS1485 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1485;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1490 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1490;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1486 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1488 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1487;
        int32_t _M0L6_2atmpS1489;
        if (
          _M0L6_2atmpS1488 < 0
          || _M0L6_2atmpS1488 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1487 = (float)_M0L3srcS28[_M0L6_2atmpS1488];
        if (
          _M0L6_2atmpS1486 < 0
          || _M0L6_2atmpS1486 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1486] = _M0L6_2atmpS1487;
        _M0L6_2atmpS1489 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1489;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  int32_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1491 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1493 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS1492;
        int32_t _M0L6_2atmpS1494;
        if (
          _M0L6_2atmpS1493 < 0
          || _M0L6_2atmpS1493 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1492 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1493];
        if (
          _M0L6_2atmpS1491 < 0
          || _M0L6_2atmpS1491 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1491] = _M0L6_2atmpS1492;
        _M0L6_2atmpS1494 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1494;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1499 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1499;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1495 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1497 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS1496;
        int32_t _M0L6_2atmpS1498;
        if (
          _M0L6_2atmpS1497 < 0
          || _M0L6_2atmpS1497 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1496 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1497];
        if (
          _M0L6_2atmpS1495 < 0
          || _M0L6_2atmpS1495 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1495] = _M0L6_2atmpS1496;
        _M0L6_2atmpS1498 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1498;
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
        int32_t _M0L6_2atmpS1500 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1502 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1501;
        moonbit_string_t _M0L6_2aoldS3451;
        int32_t _M0L6_2atmpS1503;
        if (
          _M0L6_2atmpS1502 < 0
          || _M0L6_2atmpS1502 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1501 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1502];
        if (
          _M0L6_2atmpS1500 < 0
          || _M0L6_2atmpS1500 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3451 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1500];
        moonbit_incref_cycle_free(_M0L6_2atmpS1501);
        moonbit_decref_cycle_free(_M0L6_2aoldS3451);
        _M0L3dstS45[_M0L6_2atmpS1500] = _M0L6_2atmpS1501;
        _M0L6_2atmpS1503 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1503;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1508 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1508;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1504 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1506 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1505;
        moonbit_string_t _M0L6_2aoldS3452;
        int32_t _M0L6_2atmpS1507;
        if (
          _M0L6_2atmpS1506 < 0
          || _M0L6_2atmpS1506 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1505 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1506];
        if (
          _M0L6_2atmpS1504 < 0
          || _M0L6_2atmpS1504 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3452 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1504];
        moonbit_incref_cycle_free(_M0L6_2atmpS1505);
        moonbit_decref_cycle_free(_M0L6_2aoldS3452);
        _M0L3dstS45[_M0L6_2atmpS1504] = _M0L6_2atmpS1505;
        _M0L6_2atmpS1507 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1507;
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
        int32_t _M0L6_2atmpS1509 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1511 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1510;
        struct _M0TUsiE* _M0L6_2aoldS3453;
        int32_t _M0L6_2atmpS1512;
        if (
          _M0L6_2atmpS1511 < 0
          || _M0L6_2atmpS1511 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1510 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1511];
        if (
          _M0L6_2atmpS1509 < 0
          || _M0L6_2atmpS1509 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3453 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1509];
        if (_M0L6_2atmpS1510) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1510);
        }
        if (_M0L6_2aoldS3453) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3453);
        }
        _M0L3dstS54[_M0L6_2atmpS1509] = _M0L6_2atmpS1510;
        _M0L6_2atmpS1512 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1512;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1517 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1517;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1513 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1515 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1514;
        struct _M0TUsiE* _M0L6_2aoldS3454;
        int32_t _M0L6_2atmpS1516;
        if (
          _M0L6_2atmpS1515 < 0
          || _M0L6_2atmpS1515 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1514 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1515];
        if (
          _M0L6_2atmpS1513 < 0
          || _M0L6_2atmpS1513 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3454 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1513];
        if (_M0L6_2atmpS1514) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1514);
        }
        if (_M0L6_2aoldS3454) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3454);
        }
        _M0L3dstS54[_M0L6_2atmpS1513] = _M0L6_2atmpS1514;
        _M0L6_2atmpS1516 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1516;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1442) {
  switch (Moonbit_object_tag(_M0L4_2aeS1442)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1442);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1468,
  struct _M0TPB4Show _M0L8_2aparamS1467
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1466 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1468;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1466, _M0L8_2aparamS1467);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1465,
  struct _M0TPB4Show _M0L8_2aparamS1464
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1463 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1465;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1463, _M0L8_2aparamS1464);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1462,
  int32_t _M0L8_2aparamS1461
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1460 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1462;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1460, _M0L8_2aparamS1461);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1459,
  struct _M0TPC16string10StringView _M0L8_2aparamS1458
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1457 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1459;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1457, _M0L8_2aparamS1458);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1456,
  moonbit_string_t _M0L8_2aparamS1453,
  int32_t _M0L8_2aparamS1454,
  int32_t _M0L8_2aparamS1455
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1452 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1456;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1452, _M0L8_2aparamS1453, _M0L8_2aparamS1454, _M0L8_2aparamS1455);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1451,
  moonbit_string_t _M0L8_2aparamS1450
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1449 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1451;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1449, _M0L8_2aparamS1450);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_3653 = 9218868437227405311ll;
  int64_t _tmp_3654;
  int64_t _tmp_3655;
  int64_t _tmp_3656;
  int64_t _tmp_3657;
  _M0FPB18double__max__value = *(double*)&_tmp_3653;
  _tmp_3654 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_3654;
  _tmp_3655 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_3655;
  _tmp_3656 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_3656;
  _tmp_3657 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_3657;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1472;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1435;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1436;
  int32_t _M0L7_2abindS1437;
  struct _M0TUsiE** _M0L7_2abindS1438;
  int32_t _M0L6_2acntS3459;
  int32_t _M0L2__S1439;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1472
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1435
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1435)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 140, 0);
  _M0L12async__testsS1435->$0 = _M0L6_2atmpS1472;
  _M0L12async__testsS1435->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1436
  = _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1437 = _M0L7_2abindS1436->$1;
  _M0L7_2abindS1438 = _M0L7_2abindS1436->$0;
  _M0L6_2acntS3459
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1436));
  if (_M0L6_2acntS3459 > 1) {
    int32_t _M0L11_2anew__cntS3460 = _M0L6_2acntS3459 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1436), _M0L11_2anew__cntS3460);
    moonbit_incref_cycle_free(_M0L7_2abindS1438);
  } else if (_M0L6_2acntS3459 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1436);
  }
  _M0L2__S1439 = 0;
  while (1) {
    if (_M0L2__S1439 < _M0L7_2abindS1437) {
      struct _M0TUsiE* _M0L3argS1440 =
        (struct _M0TUsiE*)_M0L7_2abindS1438[_M0L2__S1439];
      moonbit_string_t _M0L6_2atmpS1469 = _M0L3argS1440->$0;
      int32_t _M0L6_2atmpS1470 = _M0L3argS1440->$1;
      int32_t _M0L6_2atmpS1471;
      moonbit_incref_cycle_free(_M0L6_2atmpS1469);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples31tripod__network__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1435, _M0L6_2atmpS1469, _M0L6_2atmpS1470);
      moonbit_decref_cycle_free(_M0L6_2atmpS1469);
      _M0L6_2atmpS1471 = _M0L2__S1439 + 1;
      _M0L2__S1439 = _M0L6_2atmpS1471;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1438);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples31tripod__network__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31tripod__network__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1435);
  moonbit_decref_cycle_free(_M0L12async__testsS1435);
  moonbit_flush_cycles();
  return 0;
}