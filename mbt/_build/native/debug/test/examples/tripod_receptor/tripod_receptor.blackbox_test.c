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
struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TUsiE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TUdiE;

struct _M0BTPB6Logger;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt9Receptors;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod;

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346;

struct _M0TP26RiantR8snn__mbt8Dendrite;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt9TripodHet;

struct _M0TPB8MutLocalGiE;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt16AdExParameterHet;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TP26RiantR8snn__mbt8Receptor;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TUddE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341 {
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

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TUsiE {
  moonbit_string_t $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE {
  struct _M0TP26RiantR8snn__mbt8Receptor** $0;
  int32_t $1;
  
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

struct _M0TP26RiantR8snn__mbt9Receptors {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* $0;
  
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

struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  struct _M0TP26RiantR8snn__mbt9TripodHet* $1;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $2;
  moonbit_string_t $3;
  struct _M0TP26RiantR8snn__mbt9Receptors* $4;
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
};

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0TP26RiantR8snn__mbt8Receptor {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  int32_t $8;
  moonbit_string_t $9;
  
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency {
  float $0;
  float $1;
  float $2;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
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

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1353(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1346(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1341(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1318(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1311(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt32step__receptors__tripod__synapse(
  struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt34forward__receptor__tripod__synapse(
  struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod*,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod* _M0MP26RiantR8snn__mbt21ReceptorSynapseTripod3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  moonbit_string_t,
  struct _M0TP26RiantR8snn__mbt9Receptors*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*
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

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt14step__receptor(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  float
);

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors3new(
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*
);

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*
);

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float,
  float,
  float,
  float,
  moonbit_string_t
);

float _M0FP26RiantR8snn__mbt13norm__synapse(float, float);

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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*
);

#define _M0FP26RiantR8snn__mbt4logf logf

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

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*,
  int32_t
);

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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

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

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

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

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float*,
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 103, 108, 117, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[122]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 121, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    114, 101, 99, 101, 112, 116, 111, 114, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[120]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 119, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 114, 105, 112, 111, 100, 95, 
    114, 101, 99, 101, 112, 116, 111, 114, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1353$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1353
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[147] =
  {
    sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341)
    / 4, 1,
    offsetof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341, $1)
    / 4
    * 2,
    sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346)
    / 4, 1,
    offsetof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod) / 4, 
    10,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $6)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $7)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $8)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod, $9)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet) / 4, 
    9, offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $8) / 4 * 2,
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
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9Receptors) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt9Receptors, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Receptor) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt8Receptor, $9) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS3140
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1374,
  moonbit_string_t _M0L8filenameS1343,
  int32_t _M0L5indexS1345
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341* _closure_3187;
  struct _M0TWEu* _M0L13handle__startS1341;
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346* _closure_3188;
  struct _M0TWssbEu* _M0L14handle__resultS1346;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1353;
  void* _M0L11_2atry__errS1368;
  struct moonbit_result_0 _tmp_3190;
  int32_t _handle__error__result_3191;
  int32_t _M0L6_2atmpS3128;
  void* _M0L3errS1369;
  moonbit_string_t _M0L4nameS1371;
  struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1372;
  moonbit_string_t _M0L7_2anameS1373;
  int32_t _M0L6_2acntS3179;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1343);
  _closure_3187
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341*)moonbit_malloc(sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341));
  Moonbit_object_header(_closure_3187)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_3187->code
  = &_M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1341;
  _closure_3187->$0 = _M0L5indexS1345;
  _closure_3187->$1 = _M0L8filenameS1343;
  _M0L13handle__startS1341 = (struct _M0TWEu*)_closure_3187;
  moonbit_incref_cycle_free(_M0L8filenameS1343);
  _closure_3188
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346*)moonbit_malloc(sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346));
  Moonbit_object_header(_closure_3188)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_3188->code
  = &_M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1346;
  _closure_3188->$0 = _M0L5indexS1345;
  _closure_3188->$1 = _M0L8filenameS1343;
  _M0L14handle__resultS1346 = (struct _M0TWssbEu*)_closure_3188;
  _M0L17error__to__stringS1353
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1353$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _tmp_3190
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1374, _M0L8filenameS1343, _M0L5indexS1345, _M0L13handle__startS1341, _M0L14handle__resultS1346, _M0L17error__to__stringS1353);
  if (_tmp_3190.tag) {
    int32_t const _M0L5_2aokS3137 = _tmp_3190.data.ok;
    _handle__error__result_3191 = _M0L5_2aokS3137;
  } else {
    void* const _M0L6_2aerrS3138 = _tmp_3190.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
    moonbit_decref_cycle_free(_M0L13handle__startS1341);
    _M0L11_2atry__errS1368 = _M0L6_2aerrS3138;
    goto join_1367;
  }
  if (_handle__error__result_3191) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
    moonbit_decref_cycle_free(_M0L13handle__startS1341);
    _M0L6_2atmpS3128 = 1;
  } else {
    struct moonbit_result_0 _tmp_3192;
    int32_t _handle__error__result_3193;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    _tmp_3192
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1374, _M0L8filenameS1343, _M0L5indexS1345, _M0L13handle__startS1341, _M0L14handle__resultS1346, _M0L17error__to__stringS1353);
    if (_tmp_3192.tag) {
      int32_t const _M0L5_2aokS3135 = _tmp_3192.data.ok;
      _handle__error__result_3193 = _M0L5_2aokS3135;
    } else {
      void* const _M0L6_2aerrS3136 = _tmp_3192.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
      moonbit_decref_cycle_free(_M0L13handle__startS1341);
      _M0L11_2atry__errS1368 = _M0L6_2aerrS3136;
      goto join_1367;
    }
    if (_handle__error__result_3193) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
      moonbit_decref_cycle_free(_M0L13handle__startS1341);
      _M0L6_2atmpS3128 = 1;
    } else {
      struct moonbit_result_0 _tmp_3194;
      int32_t _handle__error__result_3195;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _tmp_3194
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1374, _M0L8filenameS1343, _M0L5indexS1345, _M0L13handle__startS1341, _M0L14handle__resultS1346, _M0L17error__to__stringS1353);
      if (_tmp_3194.tag) {
        int32_t const _M0L5_2aokS3133 = _tmp_3194.data.ok;
        _handle__error__result_3195 = _M0L5_2aokS3133;
      } else {
        void* const _M0L6_2aerrS3134 = _tmp_3194.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
        moonbit_decref_cycle_free(_M0L13handle__startS1341);
        _M0L11_2atry__errS1368 = _M0L6_2aerrS3134;
        goto join_1367;
      }
      if (_handle__error__result_3195) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
        moonbit_decref_cycle_free(_M0L13handle__startS1341);
        _M0L6_2atmpS3128 = 1;
      } else {
        struct moonbit_result_0 _tmp_3196;
        int32_t _handle__error__result_3197;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
        _tmp_3196
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1374, _M0L8filenameS1343, _M0L5indexS1345, _M0L13handle__startS1341, _M0L14handle__resultS1346, _M0L17error__to__stringS1353);
        if (_tmp_3196.tag) {
          int32_t const _M0L5_2aokS3131 = _tmp_3196.data.ok;
          _handle__error__result_3197 = _M0L5_2aokS3131;
        } else {
          void* const _M0L6_2aerrS3132 = _tmp_3196.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
          moonbit_decref_cycle_free(_M0L13handle__startS1341);
          _M0L11_2atry__errS1368 = _M0L6_2aerrS3132;
          goto join_1367;
        }
        if (_handle__error__result_3197) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
          moonbit_decref_cycle_free(_M0L13handle__startS1341);
          _M0L6_2atmpS3128 = 1;
        } else {
          struct moonbit_result_0 _tmp_3198;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
          _tmp_3198
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1374, _M0L8filenameS1343, _M0L5indexS1345, _M0L13handle__startS1341, _M0L14handle__resultS1346, _M0L17error__to__stringS1353);
          moonbit_decref_cycle_free(_M0L13handle__startS1341);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1353);
          if (_tmp_3198.tag) {
            int32_t const _M0L5_2aokS3129 = _tmp_3198.data.ok;
            _M0L6_2atmpS3128 = _M0L5_2aokS3129;
          } else {
            void* const _M0L6_2aerrS3130 = _tmp_3198.data.err;
            _M0L11_2atry__errS1368 = _M0L6_2aerrS3130;
            goto join_1367;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS3128) {
    void* _M0L135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3139 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3139)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3139)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1368
    = _M0L135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3139;
    goto join_1367;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1346);
  }
  goto joinlet_3189;
  join_1367:;
  _M0L3errS1369 = _M0L11_2atry__errS1368;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1372
  = (struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1369;
  _M0L7_2anameS1373 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1372->$0;
  _M0L6_2acntS3179
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1372));
  if (_M0L6_2acntS3179 > 1) {
    int32_t _M0L11_2anew__cntS3180 = _M0L6_2acntS3179 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1372), _M0L11_2anew__cntS3180);
    moonbit_incref_cycle_free(_M0L7_2anameS1373);
  } else if (_M0L6_2acntS3179 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1372);
  }
  _M0L4nameS1371 = _M0L7_2anameS1373;
  goto join_1370;
  goto joinlet_3199;
  join_1370:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1346(_M0L14handle__resultS1346, _M0L4nameS1371, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1346);
  moonbit_decref_cycle_free(_M0L4nameS1371);
  joinlet_3199:;
  joinlet_3189:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1353(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS3127,
  void* _M0L3errS1354
) {
  void* _M0L1eS1356;
  moonbit_string_t _M0L1eS1358;
  moonbit_string_t _result_3202;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1354)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1359 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1354;
      moonbit_string_t _M0L4_2aeS1360 = _M0L10_2aFailureS1359->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1360);
      _M0L1eS1358 = _M0L4_2aeS1360;
      goto join_1357;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1361 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1354;
      moonbit_string_t _M0L4_2aeS1362 = _M0L15_2aInspectErrorS1361->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1362);
      _M0L1eS1358 = _M0L4_2aeS1362;
      goto join_1357;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1363 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1354;
      moonbit_string_t _M0L4_2aeS1364 = _M0L16_2aSnapshotErrorS1363->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1364);
      _M0L1eS1358 = _M0L4_2aeS1364;
      goto join_1357;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1365 =
        (struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1354;
      moonbit_string_t _M0L4_2aeS1366 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1365->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1366);
      _M0L1eS1358 = _M0L4_2aeS1366;
      goto join_1357;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1354);
      _M0L1eS1356 = _M0L3errS1354;
      goto join_1355;
      break;
    }
  }
  join_1357:;
  return _M0L1eS1358;
  join_1355:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3202 = _M0FP15Error10to__string(_M0L1eS1356);
  moonbit_decref_cycle_free(_M0L1eS1356);
  return _result_3202;
}

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1346(
  struct _M0TWssbEu* _M0L6_2aenvS3124,
  moonbit_string_t _M0L10__testnameS1347,
  moonbit_string_t _M0L7messageS1348,
  int32_t _M0L7skippedS1349
) {
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346* _M0L14_2acasted__envS3125;
  moonbit_string_t _M0L8filenameS1343;
  int32_t _M0L5indexS1345;
  moonbit_string_t _M0L10file__nameS1350;
  moonbit_string_t _M0L7messageS1351;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1352;
  moonbit_string_t _M0L6_2atmpS3126;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3125
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1346*)_M0L6_2aenvS3124;
  _M0L8filenameS1343 = _M0L14_2acasted__envS3125->$1;
  _M0L5indexS1345 = _M0L14_2acasted__envS3125->$0;
  if (!_M0L7skippedS1349 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1350
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1343, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1351
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1348, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1352
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1352, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1352, _M0L10file__nameS1350);
  moonbit_decref_cycle_free(_M0L10file__nameS1350);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1352, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1352, _M0L5indexS1345);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1352, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1352, _M0L7messageS1351);
  moonbit_decref_cycle_free(_M0L7messageS1351);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1352, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3126
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1352);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1352);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3126);
  moonbit_decref_cycle_free(_M0L6_2atmpS3126);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1341(
  struct _M0TWEu* _M0L6_2aenvS3121
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341* _M0L14_2acasted__envS3122;
  moonbit_string_t _M0L8filenameS1343;
  int32_t _M0L5indexS1345;
  moonbit_string_t _M0L10file__nameS1342;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1344;
  moonbit_string_t _M0L6_2atmpS3123;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3122
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2ftripod__receptor__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1341*)_M0L6_2aenvS3121;
  _M0L8filenameS1343 = _M0L14_2acasted__envS3122->$1;
  _M0L5indexS1345 = _M0L14_2acasted__envS3122->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1342
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1343, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1344
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1344, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1344, _M0L10file__nameS1342);
  moonbit_decref_cycle_free(_M0L10file__nameS1342);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1344, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1344, _M0L5indexS1345);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1344, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3123
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1344);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1344);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3123);
  moonbit_decref_cycle_free(_M0L6_2atmpS3123);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1311;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1318;
  struct _M0TUsiE** _M0L6_2atmpS3120;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1325;
  moonbit_string_t* _M0L9cli__argsS1326;
  moonbit_string_t _M0L6_2atmpS3119;
  moonbit_string_t _M0L6_2atmpS3118;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1327;
  int32_t _M0L7_2abindS1328;
  moonbit_string_t* _M0L7_2abindS1329;
  int32_t _M0L6_2acntS3181;
  int32_t _M0L2__S1330;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1311 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1318 = 0;
  _M0L6_2atmpS3120 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1325
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1325)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1325->$0 = _M0L6_2atmpS3120;
  _M0L16file__and__indexS1325->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1326
  = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1326)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS3119 = (moonbit_string_t)_M0L9cli__argsS1326[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS3119);
  moonbit_decref_cycle_free(_M0L9cli__argsS1326);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3118
  = _M0MP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS3119);
  moonbit_decref_cycle_free(_M0L6_2atmpS3119);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1327
  = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1318(_M0L51moonbit__test__driver__internal__split__mbt__stringS1318, _M0L6_2atmpS3118, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS3118);
  _M0L7_2abindS1328 = _M0L10test__argsS1327->$1;
  _M0L7_2abindS1329 = _M0L10test__argsS1327->$0;
  _M0L6_2acntS3181
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1327));
  if (_M0L6_2acntS3181 > 1) {
    int32_t _M0L11_2anew__cntS3182 = _M0L6_2acntS3181 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1327), _M0L11_2anew__cntS3182);
    moonbit_incref_cycle_free(_M0L7_2abindS1329);
  } else if (_M0L6_2acntS3181 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1327);
  }
  _M0L2__S1330 = 0;
  while (1) {
    if (_M0L2__S1330 < _M0L7_2abindS1328) {
      moonbit_string_t _M0L3argS1331 =
        (moonbit_string_t)_M0L7_2abindS1329[_M0L2__S1330];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1332;
      moonbit_string_t _M0L4fileS1333;
      moonbit_string_t _M0L5rangeS1334;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1335;
      moonbit_string_t _M0L6_2atmpS3116;
      int32_t _M0L5startS1336;
      moonbit_string_t _M0L6_2atmpS3115;
      int32_t _M0L3endS1337;
      int32_t _M0L1iS1338;
      int32_t _M0L6_2atmpS3117;
      moonbit_incref_cycle_free(_M0L3argS1331);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1332
      = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1318(_M0L51moonbit__test__driver__internal__split__mbt__stringS1318, _M0L3argS1331, 58);
      moonbit_decref_cycle_free(_M0L3argS1331);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1333
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1332, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1334
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1332, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1332);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1335
      = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1318(_M0L51moonbit__test__driver__internal__split__mbt__stringS1318, _M0L5rangeS1334, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1334);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3116
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1335, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1336
      = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1311(_M0L45moonbit__test__driver__internal__parse__int__S1311, _M0L6_2atmpS3116);
      moonbit_decref_cycle_free(_M0L6_2atmpS3116);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3115
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1335, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1335);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1337
      = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1311(_M0L45moonbit__test__driver__internal__parse__int__S1311, _M0L6_2atmpS3115);
      moonbit_decref_cycle_free(_M0L6_2atmpS3115);
      _M0L1iS1338 = _M0L5startS1336;
      while (1) {
        if (_M0L1iS1338 < _M0L3endS1337) {
          struct _M0TUsiE* _M0L8_2atupleS3113;
          int32_t _M0L6_2atmpS3114;
          moonbit_incref_cycle_free(_M0L4fileS1333);
          _M0L8_2atupleS3113
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS3113)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS3113->$0 = _M0L4fileS1333;
          _M0L8_2atupleS3113->$1 = _M0L1iS1338;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1325, _M0L8_2atupleS3113);
          _M0L6_2atmpS3114 = _M0L1iS1338 + 1;
          _M0L1iS1338 = _M0L6_2atmpS3114;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1333);
        }
        break;
      }
      _M0L6_2atmpS3117 = _M0L2__S1330 + 1;
      _M0L2__S1330 = _M0L6_2atmpS3117;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1329);
    }
    break;
  }
  return _M0L16file__and__indexS1325;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1318(
  int32_t _M0L6_2aenvS3094,
  moonbit_string_t _M0L1sS1319,
  int32_t _M0L3sepS1320
) {
  moonbit_string_t* _M0L6_2atmpS3112;
  struct _M0TPB5ArrayGsE* _M0L3resS1321;
  struct _M0TPB8MutLocalGiE* _M0L1iS1322;
  struct _M0TPB8MutLocalGiE* _M0L5startS1323;
  int32_t _M0L3valS3107;
  int32_t _M0L6_2atmpS3108;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3112 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1321
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1321)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1321->$0 = _M0L6_2atmpS3112;
  _M0L3resS1321->$1 = 0;
  _M0L1iS1322
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1322)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1322->$0 = 0;
  _M0L5startS1323
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1323)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1323->$0 = 0;
  while (1) {
    int32_t _M0L3valS3095 = _M0L1iS1322->$0;
    int32_t _M0L6_2atmpS3096 = Moonbit_array_length(_M0L1sS1319);
    if (_M0L3valS3095 < _M0L6_2atmpS3096) {
      int32_t _M0L3valS3099 = _M0L1iS1322->$0;
      int32_t _M0L6_2atmpS3098;
      int32_t _M0L6_2atmpS3097;
      int32_t _M0L3valS3106;
      int32_t _M0L6_2atmpS3105;
      if (
        _M0L3valS3099 < 0
        || _M0L3valS3099 >= Moonbit_array_length(_M0L1sS1319)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3098 = _M0L1sS1319[_M0L3valS3099];
      _M0L6_2atmpS3097 = _M0L6_2atmpS3098;
      if (_M0L6_2atmpS3097 == _M0L3sepS1320) {
        int32_t _M0L3valS3101 = _M0L5startS1323->$0;
        int32_t _M0L3valS3102 = _M0L1iS1322->$0;
        moonbit_string_t _M0L6_2atmpS3100;
        int32_t _M0L3valS3104;
        int32_t _M0L6_2atmpS3103;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS3100
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1319, _M0L3valS3101, _M0L3valS3102);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1321, _M0L6_2atmpS3100);
        _M0L3valS3104 = _M0L1iS1322->$0;
        _M0L6_2atmpS3103 = _M0L3valS3104 + 1;
        _M0L5startS1323->$0 = _M0L6_2atmpS3103;
      }
      _M0L3valS3106 = _M0L1iS1322->$0;
      _M0L6_2atmpS3105 = _M0L3valS3106 + 1;
      _M0L1iS1322->$0 = _M0L6_2atmpS3105;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1322);
    }
    break;
  }
  _M0L3valS3107 = _M0L5startS1323->$0;
  _M0L6_2atmpS3108 = Moonbit_array_length(_M0L1sS1319);
  if (_M0L3valS3107 < _M0L6_2atmpS3108) {
    int32_t _M0L3valS3110 = _M0L5startS1323->$0;
    int32_t _M0L6_2atmpS3111;
    moonbit_string_t _M0L6_2atmpS3109;
    moonbit_decref_cycle_free(_M0L5startS1323);
    _M0L6_2atmpS3111 = Moonbit_array_length(_M0L1sS1319);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS3109
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1319, _M0L3valS3110, _M0L6_2atmpS3111);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1321, _M0L6_2atmpS3109);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1323);
  }
  return _M0L3resS1321;
}

int32_t _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1311(
  int32_t _M0L6_2aenvS3087,
  moonbit_string_t _M0L1sS1312
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1313;
  int32_t _M0L3lenS1314;
  int32_t _M0L7_2abindS1315;
  int32_t _M0L1iS1316;
  int32_t _result_3207;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1313
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1313)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1313->$0 = 0;
  _M0L3lenS1314 = Moonbit_array_length(_M0L1sS1312);
  _M0L7_2abindS1315 = 0;
  _M0L1iS1316 = _M0L7_2abindS1315;
  while (1) {
    if (_M0L1iS1316 < _M0L3lenS1314) {
      int32_t _M0L3valS3092 = _M0L3resS1313->$0;
      int32_t _M0L6_2atmpS3089 = _M0L3valS3092 * 10;
      int32_t _M0L6_2atmpS3091;
      int32_t _M0L6_2atmpS3090;
      int32_t _M0L6_2atmpS3088;
      int32_t _M0L6_2atmpS3093;
      if (
        _M0L1iS1316 < 0 || _M0L1iS1316 >= Moonbit_array_length(_M0L1sS1312)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3091 = _M0L1sS1312[_M0L1iS1316];
      _M0L6_2atmpS3090 = _M0L6_2atmpS3091 - 48;
      _M0L6_2atmpS3088 = _M0L6_2atmpS3089 + _M0L6_2atmpS3090;
      _M0L3resS1313->$0 = _M0L6_2atmpS3088;
      _M0L6_2atmpS3093 = _M0L1iS1316 + 1;
      _M0L1iS1316 = _M0L6_2atmpS3093;
      continue;
    }
    break;
  }
  _result_3207 = _M0L3resS1313->$0;
  moonbit_decref_cycle_free(_M0L3resS1313);
  return _result_3207;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1310
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1310);
  return _M0L4selfS1310;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1280,
  moonbit_string_t _M0L12_2adiscard__S1281,
  int32_t _M0L12_2adiscard__S1282,
  struct _M0TWEu* _M0L12_2adiscard__S1283,
  struct _M0TWssbEu* _M0L12_2adiscard__S1284,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1285
) {
  struct moonbit_result_0 _result_3208;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3208.tag = 1;
  _result_3208.data.ok = 0;
  return _result_3208;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1286,
  moonbit_string_t _M0L12_2adiscard__S1287,
  int32_t _M0L12_2adiscard__S1288,
  struct _M0TWEu* _M0L12_2adiscard__S1289,
  struct _M0TWssbEu* _M0L12_2adiscard__S1290,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1291
) {
  struct moonbit_result_0 _result_3209;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3209.tag = 1;
  _result_3209.data.ok = 0;
  return _result_3209;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1292,
  moonbit_string_t _M0L12_2adiscard__S1293,
  int32_t _M0L12_2adiscard__S1294,
  struct _M0TWEu* _M0L12_2adiscard__S1295,
  struct _M0TWssbEu* _M0L12_2adiscard__S1296,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1297
) {
  struct moonbit_result_0 _result_3210;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3210.tag = 1;
  _result_3210.data.ok = 0;
  return _result_3210;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1298,
  moonbit_string_t _M0L12_2adiscard__S1299,
  int32_t _M0L12_2adiscard__S1300,
  struct _M0TWEu* _M0L12_2adiscard__S1301,
  struct _M0TWssbEu* _M0L12_2adiscard__S1302,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1303
) {
  struct moonbit_result_0 _result_3211;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3211.tag = 1;
  _result_3211.data.ok = 0;
  return _result_3211;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1304,
  moonbit_string_t _M0L12_2adiscard__S1305,
  int32_t _M0L12_2adiscard__S1306,
  struct _M0TWEu* _M0L12_2adiscard__S1307,
  struct _M0TWssbEu* _M0L12_2adiscard__S1308,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1309
) {
  struct moonbit_result_0 _result_3212;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _result_3212.tag = 1;
  _result_3212.data.ok = 0;
  return _result_3212;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1279
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt32step__receptors__tripod__synapse(
  struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod* _M0L1sS1231,
  float _M0L2dtS1242
) {
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3086;
  int32_t _M0L7n__postS1230;
  struct _M0TPB8MutLocalGiE* _M0L1rS1232;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L4postS3086 = _M0L1sS1231->$1;
  _M0L7n__postS1230 = _M0L4postS3086->$27;
  _M0L1rS1232
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1rS1232)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1rS1232->$0 = 0;
  while (1) {
    int32_t _M0L3valS3042 = _M0L1rS1232->$0;
    if (_M0L3valS3042 < 4) {
      struct _M0TP26RiantR8snn__mbt9Receptors* _M0L9receptorsS3085 =
        _M0L1sS1231->$4;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L3recS3083 =
        _M0L9receptorsS3085->$0;
      int32_t _M0L3valS3084 = _M0L1rS1232->$0;
      struct _M0TP26RiantR8snn__mbt8Receptor* _M0L3recS1233;
      moonbit_string_t _M0L6targetS3082;
      int32_t _M0L7is__gluS1234;
      moonbit_string_t _M0L7_2abindS1236;
      struct _M0TPB5ArrayGfE* _M0L3bufS1235;
      struct _M0TPB5ArrayGfE* _M0L6g__colS1237;
      struct _M0TPB5ArrayGfE* _M0L6h__colS1238;
      int32_t _M0L7_2abindS1239;
      int32_t _M0L1iS1240;
      int32_t _M0L7_2abindS1243;
      int32_t _M0L1iS1244;
      int32_t _M0L3valS3074;
      int32_t _M0L6_2atmpS3073;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      _M0L3recS1233
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(_M0L3recS3083, _M0L3valS3084);
      _M0L6targetS3082 = _M0L3recS1233->$9;
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      _M0L7is__gluS1234
      = _M0L6targetS3082 == (moonbit_string_t)moonbit_string_literal_9.data
        || Moonbit_array_length(_M0L6targetS3082)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
           && 0
              == memcmp(_M0L6targetS3082, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L6targetS3082) * 2);
      _M0L7_2abindS1236 = _M0L1sS1231->$3;
      if (
        _M0L7_2abindS1236 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L7_2abindS1236) == 4
           && 0
              == memcmp(_M0L7_2abindS1236, (moonbit_string_t)moonbit_string_literal_12.data, 8)
      ) {
        if (_M0L7is__gluS1234) {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3075 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3141 = _M0L4postS3075->$15;
          moonbit_incref_cycle_free(_M0L8_2afieldS3141);
          _M0L3bufS1235 = _M0L8_2afieldS3141;
        } else {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3076 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3142 = _M0L4postS3076->$16;
          moonbit_incref_cycle_free(_M0L8_2afieldS3142);
          _M0L3bufS1235 = _M0L8_2afieldS3142;
        }
      } else if (
               _M0L7_2abindS1236
               == (moonbit_string_t)moonbit_string_literal_11.data
               || Moonbit_array_length(_M0L7_2abindS1236) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1236, (moonbit_string_t)moonbit_string_literal_11.data, 4)
             ) {
        if (_M0L7is__gluS1234) {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3077 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3143 = _M0L4postS3077->$17;
          moonbit_incref_cycle_free(_M0L8_2afieldS3143);
          _M0L3bufS1235 = _M0L8_2afieldS3143;
        } else {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3078 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3144 = _M0L4postS3078->$18;
          moonbit_incref_cycle_free(_M0L8_2afieldS3144);
          _M0L3bufS1235 = _M0L8_2afieldS3144;
        }
      } else if (
               _M0L7_2abindS1236
               == (moonbit_string_t)moonbit_string_literal_10.data
               || Moonbit_array_length(_M0L7_2abindS1236) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1236, (moonbit_string_t)moonbit_string_literal_10.data, 4)
             ) {
        if (_M0L7is__gluS1234) {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3079 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3145 = _M0L4postS3079->$19;
          moonbit_incref_cycle_free(_M0L8_2afieldS3145);
          _M0L3bufS1235 = _M0L8_2afieldS3145;
        } else {
          struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3080 =
            _M0L1sS1231->$1;
          struct _M0TPB5ArrayGfE* _M0L8_2afieldS3146 = _M0L4postS3080->$20;
          moonbit_incref_cycle_free(_M0L8_2afieldS3146);
          _M0L3bufS1235 = _M0L8_2afieldS3146;
        }
      } else {
        struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3081 =
          _M0L1sS1231->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3147 = _M0L4postS3081->$15;
        moonbit_incref_cycle_free(_M0L8_2afieldS3147);
        _M0L3bufS1235 = _M0L8_2afieldS3147;
      }
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      _M0L6g__colS1237
      = _M0MPC15array5Array4makeGfE(_M0L7n__postS1230, 0x0p+0f);
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      _M0L6h__colS1238
      = _M0MPC15array5Array4makeGfE(_M0L7n__postS1230, 0x0p+0f);
      _M0L7_2abindS1239 = 0;
      _M0L1iS1240 = _M0L7_2abindS1239;
      while (1) {
        if (_M0L1iS1240 < _M0L7n__postS1230) {
          struct _M0TPB5ArrayGfE* _M0L8g__stateS3044 = _M0L1sS1231->$6;
          int32_t _M0L6_2atmpS3046 = _M0L1iS1240 * 4;
          int32_t _M0L3valS3047 = _M0L1rS1232->$0;
          int32_t _M0L6_2atmpS3045 = _M0L6_2atmpS3046 + _M0L3valS3047;
          float _M0L6_2atmpS3043;
          struct _M0TPB5ArrayGfE* _M0L8h__stateS3049;
          int32_t _M0L6_2atmpS3051;
          int32_t _M0L3valS3052;
          int32_t _M0L6_2atmpS3050;
          float _M0L6_2atmpS3048;
          int32_t _M0L6_2atmpS3053;
          #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0L6_2atmpS3043
          = _M0MPC15array5Array2atGfE(_M0L8g__stateS3044, _M0L6_2atmpS3045);
          #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0MPC15array5Array3setGfE(_M0L6g__colS1237, _M0L1iS1240, _M0L6_2atmpS3043);
          _M0L8h__stateS3049 = _M0L1sS1231->$7;
          _M0L6_2atmpS3051 = _M0L1iS1240 * 4;
          _M0L3valS3052 = _M0L1rS1232->$0;
          _M0L6_2atmpS3050 = _M0L6_2atmpS3051 + _M0L3valS3052;
          #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0L6_2atmpS3048
          = _M0MPC15array5Array2atGfE(_M0L8h__stateS3049, _M0L6_2atmpS3050);
          #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0MPC15array5Array3setGfE(_M0L6h__colS1238, _M0L1iS1240, _M0L6_2atmpS3048);
          _M0L6_2atmpS3053 = _M0L1iS1240 + 1;
          _M0L1iS1240 = _M0L6_2atmpS3053;
          continue;
        }
        break;
      }
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      _M0FP26RiantR8snn__mbt14step__receptor(_M0L6g__colS1237, _M0L6h__colS1238, _M0L3bufS1235, _M0L3recS1233, _M0L2dtS1242);
      moonbit_decref_cycle_free(_M0L3bufS1235);
      _M0L7_2abindS1243 = 0;
      _M0L1iS1244 = _M0L7_2abindS1243;
      while (1) {
        if (_M0L1iS1244 < _M0L7n__postS1230) {
          struct _M0TPB5ArrayGfE* _M0L8g__stateS3054 = _M0L1sS1231->$6;
          int32_t _M0L6_2atmpS3057 = _M0L1iS1244 * 4;
          int32_t _M0L3valS3058 = _M0L1rS1232->$0;
          int32_t _M0L6_2atmpS3055 = _M0L6_2atmpS3057 + _M0L3valS3058;
          float _M0L6_2atmpS3056;
          struct _M0TPB5ArrayGfE* _M0L8h__stateS3059;
          int32_t _M0L6_2atmpS3062;
          int32_t _M0L3valS3063;
          int32_t _M0L6_2atmpS3060;
          float _M0L6_2atmpS3061;
          int32_t _M0L6_2atmpS3064;
          #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0L6_2atmpS3056
          = _M0MPC15array5Array2atGfE(_M0L6g__colS1237, _M0L1iS1244);
          #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0MPC15array5Array3setGfE(_M0L8g__stateS3054, _M0L6_2atmpS3055, _M0L6_2atmpS3056);
          _M0L8h__stateS3059 = _M0L1sS1231->$7;
          _M0L6_2atmpS3062 = _M0L1iS1244 * 4;
          _M0L3valS3063 = _M0L1rS1232->$0;
          _M0L6_2atmpS3060 = _M0L6_2atmpS3062 + _M0L3valS3063;
          #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0L6_2atmpS3061
          = _M0MPC15array5Array2atGfE(_M0L6h__colS1238, _M0L1iS1244);
          #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
          _M0MPC15array5Array3setGfE(_M0L8h__stateS3059, _M0L6_2atmpS3060, _M0L6_2atmpS3061);
          _M0L6_2atmpS3064 = _M0L1iS1244 + 1;
          _M0L1iS1244 = _M0L6_2atmpS3064;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6h__colS1238);
        }
        break;
      }
      if (_M0L7is__gluS1234) {
        struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3068 =
          _M0L1sS1231->$1;
        struct _M0TPB5ArrayGfE* _M0L4v__sS3065 = _M0L4postS3068->$28;
        struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS3066 =
          _M0L1sS1231->$5;
        struct _M0TPB5ArrayGfE* _M0L7ge__outS3067 = _M0L1sS1231->$8;
        #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
        _M0FP26RiantR8snn__mbt17receptor__current(_M0L6g__colS1237, _M0L4v__sS3065, _M0L3recS1233, _M0L9nmda__depS3066, _M0L7ge__outS3067);
        moonbit_decref_cycle_free(_M0L6g__colS1237);
        moonbit_decref_cycle_free(_M0L3recS1233);
      } else {
        struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3072 =
          _M0L1sS1231->$1;
        struct _M0TPB5ArrayGfE* _M0L4v__sS3069 = _M0L4postS3072->$28;
        struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS3070 =
          _M0L1sS1231->$5;
        struct _M0TPB5ArrayGfE* _M0L7gi__outS3071 = _M0L1sS1231->$9;
        #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
        _M0FP26RiantR8snn__mbt17receptor__current(_M0L6g__colS1237, _M0L4v__sS3069, _M0L3recS1233, _M0L9nmda__depS3070, _M0L7gi__outS3071);
        moonbit_decref_cycle_free(_M0L6g__colS1237);
        moonbit_decref_cycle_free(_M0L3recS1233);
      }
      _M0L3valS3074 = _M0L1rS1232->$0;
      _M0L6_2atmpS3073 = _M0L3valS3074 + 1;
      _M0L1rS1232->$0 = _M0L6_2atmpS3073;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1rS1232);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34forward__receptor__tripod__synapse(
  struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod* _M0L1sS1219,
  int32_t _M0L16target__receptorS1216,
  float _M0L6t__nowS1229
) {
  int32_t _M0L7is__gluS1215;
  moonbit_string_t _M0L7_2abindS1218;
  struct _M0TPB5ArrayGfE* _M0L3bufS1217;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3034;
  struct _M0TPB5ArrayGbE* _M0L4fireS3033;
  int32_t _M0L6n__preS1220;
  struct _M0TPB8MutLocalGiE* _M0L1jS1221;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L7is__gluS1215
  = _M0L16target__receptorS1216 == 0 || _M0L16target__receptorS1216 == 1;
  _M0L7_2abindS1218 = _M0L1sS1219->$3;
  if (
    _M0L7_2abindS1218 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS1218) == 4
       && 0
          == memcmp(_M0L7_2abindS1218, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (_M0L7is__gluS1215) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3035 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3148 = _M0L4postS3035->$15;
      moonbit_incref_cycle_free(_M0L8_2afieldS3148);
      _M0L3bufS1217 = _M0L8_2afieldS3148;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3036 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3149 = _M0L4postS3036->$16;
      moonbit_incref_cycle_free(_M0L8_2afieldS3149);
      _M0L3bufS1217 = _M0L8_2afieldS3149;
    }
  } else if (
           _M0L7_2abindS1218
           == (moonbit_string_t)moonbit_string_literal_11.data
           || Moonbit_array_length(_M0L7_2abindS1218) == 2
              && 0
                 == memcmp(_M0L7_2abindS1218, (moonbit_string_t)moonbit_string_literal_11.data, 4)
         ) {
    if (_M0L7is__gluS1215) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3037 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3150 = _M0L4postS3037->$17;
      moonbit_incref_cycle_free(_M0L8_2afieldS3150);
      _M0L3bufS1217 = _M0L8_2afieldS3150;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3038 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3151 = _M0L4postS3038->$18;
      moonbit_incref_cycle_free(_M0L8_2afieldS3151);
      _M0L3bufS1217 = _M0L8_2afieldS3151;
    }
  } else if (
           _M0L7_2abindS1218
           == (moonbit_string_t)moonbit_string_literal_10.data
           || Moonbit_array_length(_M0L7_2abindS1218) == 2
              && 0
                 == memcmp(_M0L7_2abindS1218, (moonbit_string_t)moonbit_string_literal_10.data, 4)
         ) {
    if (_M0L7is__gluS1215) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3039 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3152 = _M0L4postS3039->$19;
      moonbit_incref_cycle_free(_M0L8_2afieldS3152);
      _M0L3bufS1217 = _M0L8_2afieldS3152;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3040 =
        _M0L1sS1219->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3153 = _M0L4postS3040->$20;
      moonbit_incref_cycle_free(_M0L8_2afieldS3153);
      _M0L3bufS1217 = _M0L8_2afieldS3153;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS3041 = _M0L1sS1219->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS3154 = _M0L4postS3041->$15;
    moonbit_incref_cycle_free(_M0L8_2afieldS3154);
    _M0L3bufS1217 = _M0L8_2afieldS3154;
  }
  _M0L3preS3034 = _M0L1sS1219->$0;
  _M0L4fireS3033 = _M0L3preS3034->$5;
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L6n__preS1220 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3033);
  _M0L1jS1221
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1221)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1221->$0 = 0;
  while (1) {
    int32_t _M0L3valS3009 = _M0L1jS1221->$0;
    if (_M0L3valS3009 < _M0L6n__preS1220) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3012 = _M0L1sS1219->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3010 = _M0L3preS3012->$5;
      int32_t _M0L3valS3011 = _M0L1jS1221->$0;
      int32_t _M0L3valS3032;
      int32_t _M0L6_2atmpS3031;
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3010, _M0L3valS3011)) {
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3030 =
          _M0L1sS1219->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3028 = _M0L6matrixS3030->$2;
        int32_t _M0L3valS3029 = _M0L1jS1221->$0;
        int32_t _M0L5startS1222;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3027;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3024;
        int32_t _M0L3valS3026;
        int32_t _M0L6_2atmpS3025;
        int32_t _M0L3endS1223;
        struct _M0TPB8MutLocalGiE* _M0L3idxS1224;
        #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
        _M0L5startS1222
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3028, _M0L3valS3029);
        _M0L6matrixS3027 = _M0L1sS1219->$2;
        _M0L6rowptrS3024 = _M0L6matrixS3027->$2;
        _M0L3valS3026 = _M0L1jS1221->$0;
        _M0L6_2atmpS3025 = _M0L3valS3026 + 1;
        #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
        _M0L3endS1223
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3024, _M0L6_2atmpS3025);
        _M0L3idxS1224
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L3idxS1224)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L3idxS1224->$0 = _M0L5startS1222;
        while (1) {
          int32_t _M0L3valS3013 = _M0L3idxS1224->$0;
          if (_M0L3valS3013 < _M0L3endS1223) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3023 =
              _M0L1sS1219->$2;
            struct _M0TPB5ArrayGiE* _M0L6colptrS3021 = _M0L6matrixS3023->$3;
            int32_t _M0L3valS3022 = _M0L3idxS1224->$0;
            int32_t _M0L9post__idxS1225;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3020;
            struct _M0TPB5ArrayGfE* _M0L4valsS3018;
            int32_t _M0L3valS3019;
            float _M0L1wS1226;
            float _M0L6_2atmpS3015;
            float _M0L6_2atmpS3014;
            int32_t _M0L3valS3017;
            int32_t _M0L6_2atmpS3016;
            #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
            _M0L9post__idxS1225
            = _M0MPC15array5Array2atGiE(_M0L6colptrS3021, _M0L3valS3022);
            _M0L6matrixS3020 = _M0L1sS1219->$2;
            _M0L4valsS3018 = _M0L6matrixS3020->$4;
            _M0L3valS3019 = _M0L3idxS1224->$0;
            #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
            _M0L1wS1226
            = _M0MPC15array5Array2atGfE(_M0L4valsS3018, _M0L3valS3019);
            #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
            _M0L6_2atmpS3015
            = _M0MPC15array5Array2atGfE(_M0L3bufS1217, _M0L9post__idxS1225);
            _M0L6_2atmpS3014 = _M0L6_2atmpS3015 + _M0L1wS1226;
            #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
            _M0MPC15array5Array3setGfE(_M0L3bufS1217, _M0L9post__idxS1225, _M0L6_2atmpS3014);
            _M0L3valS3017 = _M0L3idxS1224->$0;
            _M0L6_2atmpS3016 = _M0L3valS3017 + 1;
            _M0L3idxS1224->$0 = _M0L6_2atmpS3016;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L3idxS1224);
          }
          break;
        }
      }
      _M0L3valS3032 = _M0L1jS1221->$0;
      _M0L6_2atmpS3031 = _M0L3valS3032 + 1;
      _M0L1jS1221->$0 = _M0L6_2atmpS3031;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1221);
      moonbit_decref_cycle_free(_M0L3bufS1217);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod* _M0MP26RiantR8snn__mbt21ReceptorSynapseTripod3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1201,
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS1202,
  moonbit_string_t _M0L19target__compartmentS1212,
  struct _M0TP26RiantR8snn__mbt9Receptors* _M0L9receptorsS1213,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS1214,
  float _M0L2muS1203,
  float _M0L5sigmaS1204,
  float _M0L1pS1205,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1206
) {
  int32_t _M0L1nS3007;
  int32_t _M0L1nS3008;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1200;
  int32_t _M0L7n__postS1207;
  int32_t _M0L6_2atmpS3006;
  struct _M0TPB5ArrayGfE* _M0L8g__stateS1208;
  int32_t _M0L6_2atmpS3005;
  struct _M0TPB5ArrayGfE* _M0L8h__stateS1209;
  struct _M0TPB5ArrayGfE* _M0L7ge__outS1210;
  struct _M0TPB5ArrayGfE* _M0L7gi__outS1211;
  struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod* _block_3218;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L1nS3007 = _M0L3preS1201->$2;
  _M0L1nS3008 = _M0L4postS1202->$27;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L1mS1200
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS3007, _M0L1nS3008, _M0L2muS1203, _M0L5sigmaS1204, _M0L1pS1205, _M0L3rngS1206);
  _M0L7n__postS1207 = _M0L4postS1202->$27;
  _M0L6_2atmpS3006 = _M0L7n__postS1207 * 4;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L8g__stateS1208 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS3006, 0x0p+0f);
  _M0L6_2atmpS3005 = _M0L7n__postS1207 * 4;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L8h__stateS1209 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS3005, 0x0p+0f);
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L7ge__outS1210 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1207, 0x0p+0f);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_receptor_tripod.mbt"
  _M0L7gi__outS1211 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1207, 0x0p+0f);
  moonbit_incref_cycle_free(_M0L3preS1201);
  moonbit_incref_cycle_free(_M0L4postS1202);
  moonbit_incref_cycle_free(_M0L19target__compartmentS1212);
  moonbit_incref_cycle_free(_M0L9receptorsS1213);
  moonbit_incref_cycle_free(_M0L9nmda__depS1214);
  _block_3218
  = (struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt21ReceptorSynapseTripod));
  Moonbit_object_header(_block_3218)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_3218->$0 = _M0L3preS1201;
  _block_3218->$1 = _M0L4postS1202;
  _block_3218->$2 = _M0L1mS1200;
  _block_3218->$3 = _M0L19target__compartmentS1212;
  _block_3218->$4 = _M0L9receptorsS1213;
  _block_3218->$5 = _M0L9nmda__depS1214;
  _block_3218->$6 = _M0L8g__stateS1208;
  _block_3218->$7 = _M0L8h__stateS1209;
  _block_3218->$8 = _M0L7ge__outS1210;
  _block_3218->$9 = _M0L7gi__outS1211;
  return _block_3218;
}

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t _M0L1nS1190,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L1pS1191
) {
  float _M0L2vtS3004;
  struct _M0TPB5ArrayGfE* _M0L7vt__arrS1189;
  float _M0L2vrS3003;
  struct _M0TPB5ArrayGfE* _M0L7vr__arrS1192;
  float _M0L2elS3002;
  struct _M0TPB5ArrayGfE* _M0L7el__arrS1193;
  float _M0L2tmS3001;
  struct _M0TPB5ArrayGfE* _M0L7tm__arrS1194;
  float _M0L1rS3000;
  struct _M0TPB5ArrayGfE* _M0L6r__arrS1195;
  float _M0L9dt__slopeS2999;
  struct _M0TPB5ArrayGfE* _M0L14dt__slope__arrS1196;
  float _M0L2twS2998;
  struct _M0TPB5ArrayGfE* _M0L7tw__arrS1197;
  float _M0L1aS2997;
  struct _M0TPB5ArrayGfE* _M0L6a__arrS1198;
  float _M0L1bS2996;
  struct _M0TPB5ArrayGfE* _M0L6b__arrS1199;
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _block_3219;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2vtS3004 = _M0L1pS1191->$2;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vt__arrS1189 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L2vtS3004);
  _M0L2vrS3003 = _M0L1pS1191->$3;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vr__arrS1192 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L2vrS3003);
  _M0L2elS3002 = _M0L1pS1191->$4;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7el__arrS1193 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L2elS3002);
  _M0L2tmS3001 = _M0L1pS1191->$5;
  #line 302 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tm__arrS1194 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L2tmS3001);
  _M0L1rS3000 = _M0L1pS1191->$6;
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6r__arrS1195 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L1rS3000);
  _M0L9dt__slopeS2999 = _M0L1pS1191->$7;
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L14dt__slope__arrS1196
  = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L9dt__slopeS2999);
  _M0L2twS2998 = _M0L1pS1191->$8;
  #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tw__arrS1197 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L2twS2998);
  _M0L1aS2997 = _M0L1pS1191->$9;
  #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6a__arrS1198 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L1aS2997);
  _M0L1bS2996 = _M0L1pS1191->$10;
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6b__arrS1199 = _M0MPC15array5Array4makeGfE(_M0L1nS1190, _M0L1bS2996);
  _block_3219
  = (struct _M0TP26RiantR8snn__mbt16AdExParameterHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet));
  Moonbit_object_header(_block_3219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_3219->$0 = _M0L7vt__arrS1189;
  _block_3219->$1 = _M0L7vr__arrS1192;
  _block_3219->$2 = _M0L7el__arrS1193;
  _block_3219->$3 = _M0L7tm__arrS1194;
  _block_3219->$4 = _M0L6r__arrS1195;
  _block_3219->$5 = _M0L14dt__slope__arrS1196;
  _block_3219->$6 = _M0L7tw__arrS1197;
  _block_3219->$7 = _M0L6a__arrS1198;
  _block_3219->$8 = _M0L6b__arrS1199;
  return _block_3219;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1187;
  float _M0L2glS1188;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_3220;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1187 = -0x1p+0f;
  _M0L2glS1188 = -0x1p+0f;
  _block_3220
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_3220)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3220->$0 = _M0L1cS1187;
  _block_3220->$1 = _M0L2glS1188;
  _block_3220->$2 = 0x1.ep+3f;
  _block_3220->$3 = -0x1.9p+5f;
  _block_3220->$4 = -0x1.ep+5f;
  _block_3220->$5 = -0x1.18p+6f;
  _block_3220->$6 = 0x1.eb851eb851eb8p-5f;
  _block_3220->$7 = 0x1p+1f;
  _block_3220->$8 = 0x0p+0f;
  _block_3220->$9 = 0x0p+0f;
  _block_3220->$10 = 0x0p+0f;
  return _block_3220;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1161,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1163,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1166
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1160;
  float _M0L2vtS2994;
  float _M0L2vrS2995;
  float _M0L6spreadS1162;
  int32_t _M0L7_2abindS1164;
  int32_t _M0L1kS1165;
  struct _M0TPB5ArrayGfE* _M0L1wS1168;
  struct _M0TPB5ArrayGbE* _M0L4fireS1169;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1170;
  struct _M0TPB5ArrayGfE* _M0L1iS1171;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1172;
  struct _M0TPB5ArrayGfE* _M0L2geS1173;
  struct _M0TPB5ArrayGfE* _M0L2giS1174;
  struct _M0TPB5ArrayGfE* _M0L2heS1175;
  struct _M0TPB5ArrayGfE* _M0L2hiS1176;
  struct _M0TPB5ArrayGfE* _M0L3gluS1177;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1178;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1179;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1180;
  float _M0L4e__eS1181;
  float _M0L4e__iS1182;
  float _M0L3treS1183;
  float _M0L3tdeS1184;
  float _M0L3triS1185;
  float _M0L3tdiS1186;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2993;
  struct _M0TP26RiantR8snn__mbt2IF* _block_3222;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1160 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  _M0L2vtS2994 = _M0L5paramS1163->$3;
  _M0L2vrS2995 = _M0L5paramS1163->$4;
  _M0L6spreadS1162 = _M0L2vtS2994 - _M0L2vrS2995;
  _M0L7_2abindS1164 = 0;
  _M0L1kS1165 = _M0L7_2abindS1164;
  while (1) {
    if (_M0L1kS1165 < _M0L1nS1161) {
      float _M0L2vrS2989 = _M0L5paramS1163->$4;
      float _M0L6_2atmpS2991;
      float _M0L6_2atmpS2990;
      float _M0L6_2atmpS2988;
      int32_t _M0L6_2atmpS2992;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2991 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1166);
      _M0L6_2atmpS2990 = _M0L6_2atmpS2991 * _M0L6spreadS1162;
      _M0L6_2atmpS2988 = _M0L2vrS2989 + _M0L6_2atmpS2990;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1160, _M0L1kS1165, _M0L6_2atmpS2988);
      _M0L6_2atmpS2992 = _M0L1kS1165 + 1;
      _M0L1kS1165 = _M0L6_2atmpS2992;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1168 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1169 = _M0MPC15array5Array4makeGbE(_M0L1nS1161, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1170 = _M0MPC15array5Array4makeGiE(_M0L1nS1161, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1171 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1172 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1173 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1174 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1175 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1176 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1177 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1178 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1179 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1180 = _M0MPC15array5Array4makeGfE(_M0L1nS1161, 0x1p+0f);
  _M0L4e__eS1181 = 0x0p+0f;
  _M0L4e__iS1182 = -0x1.2cp+6f;
  _M0L3treS1183 = 0x1p+0f;
  _M0L3tdeS1184 = 0x1.8p+2f;
  _M0L3triS1185 = 0x1p-1f;
  _M0L3tdiS1186 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2993 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1163);
  _block_3222
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_3222)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _block_3222->$0 = _M0L5paramS1163;
  _block_3222->$1 = _M0L6_2atmpS2993;
  _block_3222->$2 = _M0L1nS1161;
  _block_3222->$3 = _M0L1vS1160;
  _block_3222->$4 = _M0L1wS1168;
  _block_3222->$5 = _M0L4fireS1169;
  _block_3222->$6 = _M0L4tabsS1170;
  _block_3222->$7 = _M0L1iS1171;
  _block_3222->$8 = _M0L9syn__currS1172;
  _block_3222->$9 = _M0L2geS1173;
  _block_3222->$10 = _M0L2giS1174;
  _block_3222->$11 = _M0L2heS1175;
  _block_3222->$12 = _M0L2hiS1176;
  _block_3222->$13 = _M0L3gluS1177;
  _block_3222->$14 = _M0L4gabaS1178;
  _block_3222->$15 = _M0L7gsyn__eS1179;
  _block_3222->$16 = _M0L7gsyn__iS1180;
  _block_3222->$17 = _M0L4e__eS1181;
  _block_3222->$18 = _M0L4e__iS1182;
  _block_3222->$19 = _M0L3treS1183;
  _block_3222->$20 = _M0L3tdeS1184;
  _block_3222->$21 = _M0L3triS1185;
  _block_3222->$22 = _M0L3tdiS1186;
  return _block_3222;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_3223;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_3223
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_3223)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3223->$0 = 0x1p+1f;
  return _block_3223;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS1156;
  float _M0L2glS1157;
  float _M0L2tmS1158;
  float _M0L1rS1159;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_3224;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS1156 = 0x1.19p+8f;
  _M0L2glS1157 = 0x1.4p+5f;
  _M0L2tmS1158 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS1159 = 0x1p+0f / 0x1.4p+5f;
  _block_3224
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_3224)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3224->$0 = _M0L1cS1156;
  _block_3224->$1 = _M0L2glS1157;
  _block_3224->$2 = -0x1.9p+5f;
  _block_3224->$3 = -0x1.1a66666666666p+6f;
  _block_3224->$4 = -0x1.1a66666666666p+6f;
  _block_3224->$5 = _M0L2tmS1158;
  _block_3224->$6 = _M0L1rS1159;
  _block_3224->$7 = 0x1p+1f;
  _block_3224->$8 = 0x1.2p+7f;
  _block_3224->$9 = 0x1p+2f;
  _block_3224->$10 = 0x1.42p+6f;
  return _block_3224;
}

int32_t _M0FP26RiantR8snn__mbt17step__tripod__het(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1137,
  float _M0L2dtS1144
) {
  int32_t _M0L1nS1136;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2987;
  float _M0L2atS1138;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2986;
  float _M0L6tau__aS1139;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2985;
  float _M0L11tabs__constS1140;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2984;
  float _M0L2upS1141;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2983;
  float _M0L12ap__membraneS1142;
  float _M0L6_2atmpS2982;
  float _M0L6_2atmpS2981;
  int32_t _M0L11tabs__stepsS1143;
  int32_t _M0L7_2abindS1145;
  int32_t _M0L7_2abindS1146;
  int32_t _M0L1iS1147;
  int32_t _M0L7_2abindS1149;
  int32_t _M0L1kS1150;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1136 = _M0L1pS1137->$27;
  _M0L11soma__spikeS2987 = _M0L1pS1137->$1;
  _M0L2atS1138 = _M0L11soma__spikeS2987->$0;
  _M0L11soma__spikeS2986 = _M0L1pS1137->$1;
  _M0L6tau__aS1139 = _M0L11soma__spikeS2986->$1;
  _M0L11soma__spikeS2985 = _M0L1pS1137->$1;
  _M0L11tabs__constS1140 = _M0L11soma__spikeS2985->$3;
  _M0L11soma__spikeS2984 = _M0L1pS1137->$1;
  _M0L2upS1141 = _M0L11soma__spikeS2984->$4;
  _M0L11soma__spikeS2983 = _M0L1pS1137->$1;
  _M0L12ap__membraneS1142 = _M0L11soma__spikeS2983->$2;
  _M0L6_2atmpS2982 = _M0L2upS1141 + _M0L11tabs__constS1140;
  _M0L6_2atmpS2981 = _M0L6_2atmpS2982 / _M0L2dtS1144;
  #line 278 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L11tabs__stepsS1143 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2981);
  #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(_M0L1pS1137, _M0L2dtS1144);
  #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(_M0L1pS1137, _M0L2dtS1144);
  #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(_M0L1pS1137);
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(_M0L1pS1137);
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS1137, _M0L2dtS1144, 0);
  _M0L7_2abindS1145 = 0;
  _M0L7_2abindS1146 = _M0L1nS1136 * 4;
  _M0L1iS1147 = _M0L7_2abindS1145;
  while (1) {
    if (_M0L1iS1147 < _M0L7_2abindS1146) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2835 = _M0L1pS1137->$36;
      struct _M0TPB5ArrayGfE* _M0L2dvS2837 = _M0L1pS1137->$35;
      float _M0L6_2atmpS2836;
      int32_t _M0L6_2atmpS2838;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2836 = _M0MPC15array5Array2atGfE(_M0L2dvS2837, _M0L1iS1147);
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2835, _M0L1iS1147, _M0L6_2atmpS2836);
      _M0L6_2atmpS2838 = _M0L1iS1147 + 1;
      _M0L1iS1147 = _M0L6_2atmpS2838;
      continue;
    }
    break;
  }
  #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS1137, _M0L2dtS1144, 1);
  _M0L7_2abindS1149 = 0;
  _M0L1kS1150 = _M0L7_2abindS1149;
  while (1) {
    if (_M0L1kS1150 < _M0L1nS1136) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2980 =
        _M0L1pS1137->$0;
      struct _M0TPB5ArrayGfE* _M0L2vrS2979 = _M0L11soma__paramS2980->$1;
      float _M0L5vr__kS1153;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2978;
      struct _M0TPB5ArrayGfE* _M0L1bS2977;
      float _M0L4b__kS1154;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2840;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2843;
      int32_t _M0L6_2atmpS2842;
      int32_t _M0L6_2atmpS2841;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2844;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2855;
      float _M0L6_2atmpS2846;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2854;
      struct _M0TPB5ArrayGfE* _M0L2vtS2853;
      float _M0L6_2atmpS2850;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2852;
      float _M0L6_2atmpS2851;
      float _M0L6_2atmpS2849;
      float _M0L6_2atmpS2848;
      float _M0L6_2atmpS2847;
      float _M0L6_2atmpS2845;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2857;
      int32_t _M0L6_2atmpS2856;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2976;
      float _M0L6_2atmpS2971;
      struct _M0TPB5ArrayGfE* _M0L2dvS2974;
      int32_t _M0L6_2atmpS2975;
      float _M0L6_2atmpS2973;
      float _M0L6_2atmpS2972;
      float _M0L10v__s__predS1155;
      struct _M0TPB5ArrayGbE* _M0L4fireS2895;
      int32_t _M0L6_2atmpS2896;
      struct _M0TPB5ArrayGbE* _M0L4fireS2897;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2913;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2925;
      float _M0L6_2atmpS2915;
      float _M0L6_2atmpS2917;
      struct _M0TPB5ArrayGfE* _M0L2dvS2923;
      int32_t _M0L6_2atmpS2924;
      float _M0L6_2atmpS2919;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2921;
      int32_t _M0L6_2atmpS2922;
      float _M0L6_2atmpS2920;
      float _M0L6_2atmpS2918;
      float _M0L6_2atmpS2916;
      float _M0L6_2atmpS2914;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2926;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2940;
      float _M0L6_2atmpS2928;
      float _M0L6_2atmpS2930;
      struct _M0TPB5ArrayGfE* _M0L2dvS2937;
      int32_t _M0L6_2atmpS2939;
      int32_t _M0L6_2atmpS2938;
      float _M0L6_2atmpS2932;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2934;
      int32_t _M0L6_2atmpS2936;
      int32_t _M0L6_2atmpS2935;
      float _M0L6_2atmpS2933;
      float _M0L6_2atmpS2931;
      float _M0L6_2atmpS2929;
      float _M0L6_2atmpS2927;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2941;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2955;
      float _M0L6_2atmpS2943;
      float _M0L6_2atmpS2945;
      struct _M0TPB5ArrayGfE* _M0L2dvS2952;
      int32_t _M0L6_2atmpS2954;
      int32_t _M0L6_2atmpS2953;
      float _M0L6_2atmpS2947;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2949;
      int32_t _M0L6_2atmpS2951;
      int32_t _M0L6_2atmpS2950;
      float _M0L6_2atmpS2948;
      float _M0L6_2atmpS2946;
      float _M0L6_2atmpS2944;
      float _M0L6_2atmpS2942;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2956;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2970;
      float _M0L6_2atmpS2958;
      float _M0L6_2atmpS2960;
      struct _M0TPB5ArrayGfE* _M0L2dvS2967;
      int32_t _M0L6_2atmpS2969;
      int32_t _M0L6_2atmpS2968;
      float _M0L6_2atmpS2962;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2964;
      int32_t _M0L6_2atmpS2966;
      int32_t _M0L6_2atmpS2965;
      float _M0L6_2atmpS2963;
      float _M0L6_2atmpS2961;
      float _M0L6_2atmpS2959;
      float _M0L6_2atmpS2957;
      int32_t _M0L6_2atmpS2839;
      #line 297 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L5vr__kS1153 = _M0MPC15array5Array2atGfE(_M0L2vrS2979, _M0L1kS1150);
      _M0L11soma__paramS2978 = _M0L1pS1137->$0;
      _M0L1bS2977 = _M0L11soma__paramS2978->$8;
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L4b__kS1154 = _M0MPC15array5Array2atGfE(_M0L1bS2977, _M0L1kS1150);
      _M0L4tabsS2840 = _M0L1pS1137->$34;
      _M0L4tabsS2843 = _M0L1pS1137->$34;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2842
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2843, _M0L1kS1150);
      _M0L6_2atmpS2841 = _M0L6_2atmpS2842 - 1;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2840, _M0L1kS1150, _M0L6_2atmpS2841);
      _M0L9thresholdS2844 = _M0L1pS1137->$33;
      _M0L9thresholdS2855 = _M0L1pS1137->$33;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2846
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2855, _M0L1kS1150);
      _M0L11soma__paramS2854 = _M0L1pS1137->$0;
      _M0L2vtS2853 = _M0L11soma__paramS2854->$0;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2850 = _M0MPC15array5Array2atGfE(_M0L2vtS2853, _M0L1kS1150);
      _M0L9thresholdS2852 = _M0L1pS1137->$33;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2851
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2852, _M0L1kS1150);
      _M0L6_2atmpS2849 = _M0L6_2atmpS2850 - _M0L6_2atmpS2851;
      _M0L6_2atmpS2848 = _M0L2dtS1144 * _M0L6_2atmpS2849;
      _M0L6_2atmpS2847 = _M0L6_2atmpS2848 / _M0L6tau__aS1139;
      _M0L6_2atmpS2845 = _M0L6_2atmpS2846 + _M0L6_2atmpS2847;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2844, _M0L1kS1150, _M0L6_2atmpS2845);
      _M0L4tabsS2857 = _M0L1pS1137->$34;
      #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2856
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2857, _M0L1kS1150);
      if (_M0L6_2atmpS2856 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2858 = _M0L1pS1137->$28;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2859;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2876;
        float _M0L6_2atmpS2861;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2875;
        float _M0L6_2atmpS2872;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2874;
        float _M0L6_2atmpS2873;
        float _M0L6_2atmpS2871;
        float _M0L6_2atmpS2867;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2870;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2869;
        float _M0L6_2atmpS2868;
        float _M0L6_2atmpS2863;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2866;
        struct _M0TPB5ArrayGfE* _M0L1cS2865;
        float _M0L6_2atmpS2864;
        float _M0L6_2atmpS2862;
        float _M0L6_2atmpS2860;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2877;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2894;
        float _M0L6_2atmpS2879;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2893;
        float _M0L6_2atmpS2890;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2892;
        float _M0L6_2atmpS2891;
        float _M0L6_2atmpS2889;
        float _M0L6_2atmpS2885;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2888;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2887;
        float _M0L6_2atmpS2886;
        float _M0L6_2atmpS2881;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2884;
        struct _M0TPB5ArrayGfE* _M0L1cS2883;
        float _M0L6_2atmpS2882;
        float _M0L6_2atmpS2880;
        float _M0L6_2atmpS2878;
        #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2858, _M0L1kS1150, _M0L5vr__kS1153);
        _M0L5v__d1S2859 = _M0L1pS1137->$30;
        _M0L5v__d1S2876 = _M0L1pS1137->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2861
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2876, _M0L1kS1150);
        _M0L4v__sS2875 = _M0L1pS1137->$28;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2872
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2875, _M0L1kS1150);
        _M0L5v__d1S2874 = _M0L1pS1137->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2873
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2874, _M0L1kS1150);
        _M0L6_2atmpS2871 = _M0L6_2atmpS2872 - _M0L6_2atmpS2873;
        _M0L6_2atmpS2867 = _M0L2dtS1144 * _M0L6_2atmpS2871;
        _M0L2d1S2870 = _M0L1pS1137->$4;
        _M0L3gaxS2869 = _M0L2d1S2870->$3;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2868
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2869, _M0L1kS1150);
        _M0L6_2atmpS2863 = _M0L6_2atmpS2867 * _M0L6_2atmpS2868;
        _M0L2d1S2866 = _M0L1pS1137->$4;
        _M0L1cS2865 = _M0L2d1S2866->$2;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2864
        = _M0MPC15array5Array2atGfE(_M0L1cS2865, _M0L1kS1150);
        _M0L6_2atmpS2862 = _M0L6_2atmpS2863 / _M0L6_2atmpS2864;
        _M0L6_2atmpS2860 = _M0L6_2atmpS2861 + _M0L6_2atmpS2862;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2859, _M0L1kS1150, _M0L6_2atmpS2860);
        _M0L5v__d2S2877 = _M0L1pS1137->$31;
        _M0L5v__d2S2894 = _M0L1pS1137->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2879
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2894, _M0L1kS1150);
        _M0L4v__sS2893 = _M0L1pS1137->$28;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2890
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2893, _M0L1kS1150);
        _M0L5v__d2S2892 = _M0L1pS1137->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2891
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2892, _M0L1kS1150);
        _M0L6_2atmpS2889 = _M0L6_2atmpS2890 - _M0L6_2atmpS2891;
        _M0L6_2atmpS2885 = _M0L2dtS1144 * _M0L6_2atmpS2889;
        _M0L2d2S2888 = _M0L1pS1137->$5;
        _M0L3gaxS2887 = _M0L2d2S2888->$3;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2886
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2887, _M0L1kS1150);
        _M0L6_2atmpS2881 = _M0L6_2atmpS2885 * _M0L6_2atmpS2886;
        _M0L2d2S2884 = _M0L1pS1137->$5;
        _M0L1cS2883 = _M0L2d2S2884->$2;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2882
        = _M0MPC15array5Array2atGfE(_M0L1cS2883, _M0L1kS1150);
        _M0L6_2atmpS2880 = _M0L6_2atmpS2881 / _M0L6_2atmpS2882;
        _M0L6_2atmpS2878 = _M0L6_2atmpS2879 + _M0L6_2atmpS2880;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2877, _M0L1kS1150, _M0L6_2atmpS2878);
        goto join_1151;
      }
      _M0L4v__sS2976 = _M0L1pS1137->$28;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2971
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2976, _M0L1kS1150);
      _M0L2dvS2974 = _M0L1pS1137->$35;
      _M0L6_2atmpS2975 = _M0L1kS1150 * 4;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2973
      = _M0MPC15array5Array2atGfE(_M0L2dvS2974, _M0L6_2atmpS2975);
      _M0L6_2atmpS2972 = _M0L6_2atmpS2973 * _M0L2dtS1144;
      _M0L10v__s__predS1155 = _M0L6_2atmpS2971 + _M0L6_2atmpS2972;
      _M0L4fireS2895 = _M0L1pS1137->$32;
      _M0L6_2atmpS2896 = _M0L10v__s__predS1155 >= -0x1.4p+3f;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2895, _M0L1kS1150, _M0L6_2atmpS2896);
      _M0L4fireS2897 = _M0L1pS1137->$32;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2897, _M0L1kS1150)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2898 = _M0L1pS1137->$35;
        int32_t _M0L6_2atmpS2899 = _M0L1kS1150 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2902 = _M0L1pS1137->$28;
        float _M0L6_2atmpS2901;
        float _M0L6_2atmpS2900;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2903;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2904;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2907;
        float _M0L6_2atmpS2906;
        float _M0L6_2atmpS2905;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2908;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2911;
        float _M0L6_2atmpS2910;
        float _M0L6_2atmpS2909;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2912;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2901
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2902, _M0L1kS1150);
        _M0L6_2atmpS2900 = _M0L12ap__membraneS1142 - _M0L6_2atmpS2901;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2898, _M0L6_2atmpS2899, _M0L6_2atmpS2900);
        _M0L4v__sS2903 = _M0L1pS1137->$28;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2903, _M0L1kS1150, _M0L12ap__membraneS1142);
        _M0L4w__sS2904 = _M0L1pS1137->$29;
        _M0L4w__sS2907 = _M0L1pS1137->$29;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2906
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2907, _M0L1kS1150);
        _M0L6_2atmpS2905 = _M0L6_2atmpS2906 + _M0L4b__kS1154;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2904, _M0L1kS1150, _M0L6_2atmpS2905);
        _M0L9thresholdS2908 = _M0L1pS1137->$33;
        _M0L9thresholdS2911 = _M0L1pS1137->$33;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2910
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2911, _M0L1kS1150);
        _M0L6_2atmpS2909 = _M0L6_2atmpS2910 + _M0L2atS1138;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2908, _M0L1kS1150, _M0L6_2atmpS2909);
        _M0L4tabsS2912 = _M0L1pS1137->$34;
        #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2912, _M0L1kS1150, _M0L11tabs__stepsS1143);
        goto join_1151;
      }
      _M0L4v__sS2913 = _M0L1pS1137->$28;
      _M0L4v__sS2925 = _M0L1pS1137->$28;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2915
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2925, _M0L1kS1150);
      _M0L6_2atmpS2917 = 0x1p-1f * _M0L2dtS1144;
      _M0L2dvS2923 = _M0L1pS1137->$35;
      _M0L6_2atmpS2924 = _M0L1kS1150 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2919
      = _M0MPC15array5Array2atGfE(_M0L2dvS2923, _M0L6_2atmpS2924);
      _M0L8dv__tempS2921 = _M0L1pS1137->$36;
      _M0L6_2atmpS2922 = _M0L1kS1150 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2920
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2921, _M0L6_2atmpS2922);
      _M0L6_2atmpS2918 = _M0L6_2atmpS2919 + _M0L6_2atmpS2920;
      _M0L6_2atmpS2916 = _M0L6_2atmpS2917 * _M0L6_2atmpS2918;
      _M0L6_2atmpS2914 = _M0L6_2atmpS2915 + _M0L6_2atmpS2916;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS2913, _M0L1kS1150, _M0L6_2atmpS2914);
      _M0L5v__d1S2926 = _M0L1pS1137->$30;
      _M0L5v__d1S2940 = _M0L1pS1137->$30;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2928
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2940, _M0L1kS1150);
      _M0L6_2atmpS2930 = 0x1p-1f * _M0L2dtS1144;
      _M0L2dvS2937 = _M0L1pS1137->$35;
      _M0L6_2atmpS2939 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2938 = _M0L6_2atmpS2939 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2932
      = _M0MPC15array5Array2atGfE(_M0L2dvS2937, _M0L6_2atmpS2938);
      _M0L8dv__tempS2934 = _M0L1pS1137->$36;
      _M0L6_2atmpS2936 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2935 = _M0L6_2atmpS2936 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2933
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2934, _M0L6_2atmpS2935);
      _M0L6_2atmpS2931 = _M0L6_2atmpS2932 + _M0L6_2atmpS2933;
      _M0L6_2atmpS2929 = _M0L6_2atmpS2930 * _M0L6_2atmpS2931;
      _M0L6_2atmpS2927 = _M0L6_2atmpS2928 + _M0L6_2atmpS2929;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S2926, _M0L1kS1150, _M0L6_2atmpS2927);
      _M0L5v__d2S2941 = _M0L1pS1137->$31;
      _M0L5v__d2S2955 = _M0L1pS1137->$31;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2943
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2955, _M0L1kS1150);
      _M0L6_2atmpS2945 = 0x1p-1f * _M0L2dtS1144;
      _M0L2dvS2952 = _M0L1pS1137->$35;
      _M0L6_2atmpS2954 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2953 = _M0L6_2atmpS2954 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2947
      = _M0MPC15array5Array2atGfE(_M0L2dvS2952, _M0L6_2atmpS2953);
      _M0L8dv__tempS2949 = _M0L1pS1137->$36;
      _M0L6_2atmpS2951 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2950 = _M0L6_2atmpS2951 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2948
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2949, _M0L6_2atmpS2950);
      _M0L6_2atmpS2946 = _M0L6_2atmpS2947 + _M0L6_2atmpS2948;
      _M0L6_2atmpS2944 = _M0L6_2atmpS2945 * _M0L6_2atmpS2946;
      _M0L6_2atmpS2942 = _M0L6_2atmpS2943 + _M0L6_2atmpS2944;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S2941, _M0L1kS1150, _M0L6_2atmpS2942);
      _M0L4w__sS2956 = _M0L1pS1137->$29;
      _M0L4w__sS2970 = _M0L1pS1137->$29;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2958
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2970, _M0L1kS1150);
      _M0L6_2atmpS2960 = 0x1p-1f * _M0L2dtS1144;
      _M0L2dvS2967 = _M0L1pS1137->$35;
      _M0L6_2atmpS2969 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2968 = _M0L6_2atmpS2969 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2962
      = _M0MPC15array5Array2atGfE(_M0L2dvS2967, _M0L6_2atmpS2968);
      _M0L8dv__tempS2964 = _M0L1pS1137->$36;
      _M0L6_2atmpS2966 = _M0L1kS1150 * 4;
      _M0L6_2atmpS2965 = _M0L6_2atmpS2966 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2963
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2964, _M0L6_2atmpS2965);
      _M0L6_2atmpS2961 = _M0L6_2atmpS2962 + _M0L6_2atmpS2963;
      _M0L6_2atmpS2959 = _M0L6_2atmpS2960 * _M0L6_2atmpS2961;
      _M0L6_2atmpS2957 = _M0L6_2atmpS2958 + _M0L6_2atmpS2959;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS2956, _M0L1kS1150, _M0L6_2atmpS2957);
      goto join_1151;
      goto joinlet_3227;
      join_1151:;
      _M0L6_2atmpS2839 = _M0L1kS1150 + 1;
      _M0L1kS1150 = _M0L6_2atmpS2839;
      continue;
      joinlet_3227:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__het__heun__step(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1111,
  float _M0L2dtS1124,
  int32_t _M0L11store__tempS1123
) {
  int32_t _M0L1nS1110;
  struct _M0TPB8MutLocalGiE* _M0L1kS1112;
  #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1110 = _M0L1pS1111->$27;
  _M0L1kS1112
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1112)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1112->$0 = 0;
  while (1) {
    int32_t _M0L3valS2642 = _M0L1kS1112->$0;
    if (_M0L3valS2642 < _M0L1nS1110) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2834 =
        _M0L1pS1111->$0;
      struct _M0TPB5ArrayGfE* _M0L2vtS2832 = _M0L11soma__paramS2834->$0;
      int32_t _M0L3valS2833 = _M0L1kS1112->$0;
      float _M0L2vtS1113;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2831;
      struct _M0TPB5ArrayGfE* _M0L2elS2829;
      int32_t _M0L3valS2830;
      float _M0L2elS1114;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2828;
      struct _M0TPB5ArrayGfE* _M0L2tmS2826;
      int32_t _M0L3valS2827;
      float _M0L2tmS1115;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2825;
      struct _M0TPB5ArrayGfE* _M0L1rS2823;
      int32_t _M0L3valS2824;
      float _M0L6r__valS1116;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2822;
      struct _M0TPB5ArrayGfE* _M0L9dt__slopeS2820;
      int32_t _M0L3valS2821;
      float _M0L9dt__slopeS1117;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2819;
      struct _M0TPB5ArrayGfE* _M0L2twS2817;
      int32_t _M0L3valS2818;
      float _M0L2twS1118;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2816;
      struct _M0TPB5ArrayGfE* _M0L1aS2814;
      int32_t _M0L3valS2815;
      float _M0L1aS1119;
      struct _M0TPB5ArrayGfE* _M0L1cS2812;
      int32_t _M0L3valS2813;
      float _M0L6c__valS1120;
      struct _M0TPB5ArrayGfE* _M0L2glS2810;
      int32_t _M0L3valS2811;
      float _M0L7gl__valS1121;
      float _M0L2dsS1122;
      float _M0L3dd1S1125;
      float _M0L3dd2S1126;
      float _M0L2dwS1127;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2790;
      int32_t _M0L3valS2791;
      float _M0L6_2atmpS2789;
      float _M0L6_2atmpS2785;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2787;
      int32_t _M0L3valS2788;
      float _M0L6_2atmpS2786;
      float _M0L6_2atmpS2784;
      float _M0L6_2atmpS2783;
      float _M0L6_2atmpS2778;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2782;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2780;
      int32_t _M0L3valS2781;
      float _M0L6_2atmpS2779;
      float _M0L3ic1S1128;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2776;
      int32_t _M0L3valS2777;
      float _M0L6_2atmpS2775;
      float _M0L6_2atmpS2771;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2773;
      int32_t _M0L3valS2774;
      float _M0L6_2atmpS2772;
      float _M0L6_2atmpS2770;
      float _M0L6_2atmpS2769;
      float _M0L6_2atmpS2764;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2768;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2766;
      int32_t _M0L3valS2767;
      float _M0L6_2atmpS2765;
      float _M0L3ic2S1129;
      float _M0L9exp__termS1130;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2752;
      int32_t _M0L3valS2753;
      float _M0L6_2atmpS2751;
      float _M0L6_2atmpS2750;
      float _M0L6_2atmpS2749;
      float _M0L6_2atmpS2748;
      float _M0L6_2atmpS2744;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2746;
      int32_t _M0L3valS2747;
      float _M0L6_2atmpS2745;
      float _M0L6_2atmpS2743;
      float _M0L6_2atmpS2739;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2741;
      int32_t _M0L3valS2742;
      float _M0L6_2atmpS2740;
      float _M0L6_2atmpS2737;
      float _M0L6_2atmpS2738;
      float _M0L6_2atmpS2733;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2735;
      int32_t _M0L3valS2736;
      float _M0L6_2atmpS2734;
      float _M0L6_2atmpS2732;
      float _M0L10dv__s__valS1131;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2730;
      int32_t _M0L3valS2731;
      float _M0L6_2atmpS2729;
      float _M0L6_2atmpS2728;
      float _M0L6_2atmpS2723;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2727;
      struct _M0TPB5ArrayGfE* _M0L2gmS2725;
      int32_t _M0L3valS2726;
      float _M0L6_2atmpS2724;
      float _M0L6_2atmpS2719;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2721;
      int32_t _M0L3valS2722;
      float _M0L6_2atmpS2720;
      float _M0L6_2atmpS2718;
      float _M0L6_2atmpS2714;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2716;
      int32_t _M0L3valS2717;
      float _M0L6_2atmpS2715;
      float _M0L6_2atmpS2709;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2713;
      struct _M0TPB5ArrayGfE* _M0L1cS2711;
      int32_t _M0L3valS2712;
      float _M0L6_2atmpS2710;
      float _M0L11dv__d1__valS1132;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2707;
      int32_t _M0L3valS2708;
      float _M0L6_2atmpS2706;
      float _M0L6_2atmpS2705;
      float _M0L6_2atmpS2700;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2704;
      struct _M0TPB5ArrayGfE* _M0L2gmS2702;
      int32_t _M0L3valS2703;
      float _M0L6_2atmpS2701;
      float _M0L6_2atmpS2696;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2698;
      int32_t _M0L3valS2699;
      float _M0L6_2atmpS2697;
      float _M0L6_2atmpS2695;
      float _M0L6_2atmpS2691;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2693;
      int32_t _M0L3valS2694;
      float _M0L6_2atmpS2692;
      float _M0L6_2atmpS2686;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2690;
      struct _M0TPB5ArrayGfE* _M0L1cS2688;
      int32_t _M0L3valS2689;
      float _M0L6_2atmpS2687;
      float _M0L11dv__d2__valS1133;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2684;
      int32_t _M0L3valS2685;
      float _M0L6_2atmpS2683;
      float _M0L6_2atmpS2682;
      float _M0L6_2atmpS2681;
      float _M0L6_2atmpS2676;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2679;
      int32_t _M0L3valS2680;
      float _M0L6_2atmpS2678;
      float _M0L6_2atmpS2677;
      float _M0L6_2atmpS2675;
      float _M0L7dw__valS1134;
      int32_t _M0L3valS2674;
      int32_t _M0L6_2atmpS2673;
      #line 216 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2vtS1113 = _M0MPC15array5Array2atGfE(_M0L2vtS2832, _M0L3valS2833);
      _M0L11soma__paramS2831 = _M0L1pS1111->$0;
      _M0L2elS2829 = _M0L11soma__paramS2831->$2;
      _M0L3valS2830 = _M0L1kS1112->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2elS1114 = _M0MPC15array5Array2atGfE(_M0L2elS2829, _M0L3valS2830);
      _M0L11soma__paramS2828 = _M0L1pS1111->$0;
      _M0L2tmS2826 = _M0L11soma__paramS2828->$3;
      _M0L3valS2827 = _M0L1kS1112->$0;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2tmS1115 = _M0MPC15array5Array2atGfE(_M0L2tmS2826, _M0L3valS2827);
      _M0L11soma__paramS2825 = _M0L1pS1111->$0;
      _M0L1rS2823 = _M0L11soma__paramS2825->$4;
      _M0L3valS2824 = _M0L1kS1112->$0;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6r__valS1116
      = _M0MPC15array5Array2atGfE(_M0L1rS2823, _M0L3valS2824);
      _M0L11soma__paramS2822 = _M0L1pS1111->$0;
      _M0L9dt__slopeS2820 = _M0L11soma__paramS2822->$5;
      _M0L3valS2821 = _M0L1kS1112->$0;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L9dt__slopeS1117
      = _M0MPC15array5Array2atGfE(_M0L9dt__slopeS2820, _M0L3valS2821);
      _M0L11soma__paramS2819 = _M0L1pS1111->$0;
      _M0L2twS2817 = _M0L11soma__paramS2819->$6;
      _M0L3valS2818 = _M0L1kS1112->$0;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2twS1118 = _M0MPC15array5Array2atGfE(_M0L2twS2817, _M0L3valS2818);
      _M0L11soma__paramS2816 = _M0L1pS1111->$0;
      _M0L1aS2814 = _M0L11soma__paramS2816->$7;
      _M0L3valS2815 = _M0L1kS1112->$0;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L1aS1119 = _M0MPC15array5Array2atGfE(_M0L1aS2814, _M0L3valS2815);
      _M0L1cS2812 = _M0L1pS1111->$2;
      _M0L3valS2813 = _M0L1kS1112->$0;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6c__valS1120
      = _M0MPC15array5Array2atGfE(_M0L1cS2812, _M0L3valS2813);
      _M0L2glS2810 = _M0L1pS1111->$3;
      _M0L3valS2811 = _M0L1kS1112->$0;
      #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L7gl__valS1121
      = _M0MPC15array5Array2atGfE(_M0L2glS2810, _M0L3valS2811);
      if (_M0L11store__tempS1123) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2807 = _M0L1pS1111->$36;
        int32_t _M0L3valS2809 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2808 = _M0L3valS2809 * 4;
        float _M0L6_2atmpS2806;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2806
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2807, _M0L6_2atmpS2808);
        _M0L2dsS1122 = _M0L6_2atmpS2806 * _M0L2dtS1124;
      } else {
        _M0L2dsS1122 = 0x0p+0f;
      }
      if (_M0L11store__tempS1123) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2802 = _M0L1pS1111->$36;
        int32_t _M0L3valS2805 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2804 = _M0L3valS2805 * 4;
        int32_t _M0L6_2atmpS2803 = _M0L6_2atmpS2804 + 1;
        float _M0L6_2atmpS2801;
        #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2801
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2802, _M0L6_2atmpS2803);
        _M0L3dd1S1125 = _M0L6_2atmpS2801 * _M0L2dtS1124;
      } else {
        _M0L3dd1S1125 = 0x0p+0f;
      }
      if (_M0L11store__tempS1123) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2797 = _M0L1pS1111->$36;
        int32_t _M0L3valS2800 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2799 = _M0L3valS2800 * 4;
        int32_t _M0L6_2atmpS2798 = _M0L6_2atmpS2799 + 2;
        float _M0L6_2atmpS2796;
        #line 228 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2796
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2797, _M0L6_2atmpS2798);
        _M0L3dd2S1126 = _M0L6_2atmpS2796 * _M0L2dtS1124;
      } else {
        _M0L3dd2S1126 = 0x0p+0f;
      }
      if (_M0L11store__tempS1123) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2792 = _M0L1pS1111->$36;
        int32_t _M0L3valS2795 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2794 = _M0L3valS2795 * 4;
        int32_t _M0L6_2atmpS2793 = _M0L6_2atmpS2794 + 3;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L2dwS1127
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2792, _M0L6_2atmpS2793);
      } else {
        _M0L2dwS1127 = 0x0p+0f;
      }
      _M0L5v__d1S2790 = _M0L1pS1111->$30;
      _M0L3valS2791 = _M0L1kS1112->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2789
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2790, _M0L3valS2791);
      _M0L6_2atmpS2785 = _M0L6_2atmpS2789 + _M0L3dd1S1125;
      _M0L4v__sS2787 = _M0L1pS1111->$28;
      _M0L3valS2788 = _M0L1kS1112->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2786
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2787, _M0L3valS2788);
      _M0L6_2atmpS2784 = _M0L6_2atmpS2785 - _M0L6_2atmpS2786;
      _M0L6_2atmpS2783 = _M0L6_2atmpS2784 - _M0L2dsS1122;
      _M0L6_2atmpS2778 = -_M0L6_2atmpS2783;
      _M0L2d1S2782 = _M0L1pS1111->$4;
      _M0L3gaxS2780 = _M0L2d1S2782->$3;
      _M0L3valS2781 = _M0L1kS1112->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2779
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2780, _M0L3valS2781);
      _M0L3ic1S1128 = _M0L6_2atmpS2778 * _M0L6_2atmpS2779;
      _M0L5v__d2S2776 = _M0L1pS1111->$31;
      _M0L3valS2777 = _M0L1kS1112->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2775
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2776, _M0L3valS2777);
      _M0L6_2atmpS2771 = _M0L6_2atmpS2775 + _M0L3dd2S1126;
      _M0L4v__sS2773 = _M0L1pS1111->$28;
      _M0L3valS2774 = _M0L1kS1112->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2772
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2773, _M0L3valS2774);
      _M0L6_2atmpS2770 = _M0L6_2atmpS2771 - _M0L6_2atmpS2772;
      _M0L6_2atmpS2769 = _M0L6_2atmpS2770 - _M0L2dsS1122;
      _M0L6_2atmpS2764 = -_M0L6_2atmpS2769;
      _M0L2d2S2768 = _M0L1pS1111->$5;
      _M0L3gaxS2766 = _M0L2d2S2768->$3;
      _M0L3valS2767 = _M0L1kS1112->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2765
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2766, _M0L3valS2767);
      _M0L3ic2S1129 = _M0L6_2atmpS2764 * _M0L6_2atmpS2765;
      if (_M0L9dt__slopeS1117 < 0x0p+0f) {
        _M0L9exp__termS1130 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2762 = _M0L1pS1111->$28;
        int32_t _M0L3valS2763 = _M0L1kS1112->$0;
        float _M0L6_2atmpS2761;
        float _M0L6_2atmpS2757;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2759;
        int32_t _M0L3valS2760;
        float _M0L6_2atmpS2758;
        float _M0L6_2atmpS2756;
        float _M0L6_2atmpS2755;
        float _M0L6_2atmpS2754;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2761
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2762, _M0L3valS2763);
        _M0L6_2atmpS2757 = _M0L6_2atmpS2761 + _M0L2dsS1122;
        _M0L9thresholdS2759 = _M0L1pS1111->$33;
        _M0L3valS2760 = _M0L1kS1112->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2758
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2759, _M0L3valS2760);
        _M0L6_2atmpS2756 = _M0L6_2atmpS2757 - _M0L6_2atmpS2758;
        _M0L6_2atmpS2755 = _M0L6_2atmpS2756 / _M0L9dt__slopeS1117;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2754 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2755);
        _M0L9exp__termS1130 = _M0L9dt__slopeS1117 * _M0L6_2atmpS2754;
      }
      _M0L4v__sS2752 = _M0L1pS1111->$28;
      _M0L3valS2753 = _M0L1kS1112->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2751
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2752, _M0L3valS2753);
      _M0L6_2atmpS2750 = _M0L2elS1114 - _M0L6_2atmpS2751;
      _M0L6_2atmpS2749 = _M0L6_2atmpS2750 - _M0L2dsS1122;
      _M0L6_2atmpS2748 = _M0L7gl__valS1121 * _M0L6_2atmpS2749;
      _M0L6_2atmpS2744 = _M0L6_2atmpS2748 + _M0L9exp__termS1130;
      _M0L4w__sS2746 = _M0L1pS1111->$29;
      _M0L3valS2747 = _M0L1kS1112->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2745
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2746, _M0L3valS2747);
      _M0L6_2atmpS2743 = _M0L6_2atmpS2744 - _M0L6_2atmpS2745;
      _M0L6_2atmpS2739 = _M0L6_2atmpS2743 - _M0L2dwS1127;
      _M0L12syn__curr__sS2741 = _M0L1pS1111->$37;
      _M0L3valS2742 = _M0L1kS1112->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2740
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2741, _M0L3valS2742);
      _M0L6_2atmpS2737 = _M0L6_2atmpS2739 - _M0L6_2atmpS2740;
      _M0L6_2atmpS2738 = _M0L3ic1S1128 + _M0L3ic2S1129;
      _M0L6_2atmpS2733 = _M0L6_2atmpS2737 - _M0L6_2atmpS2738;
      _M0L4i__sS2735 = _M0L1pS1111->$6;
      _M0L3valS2736 = _M0L1kS1112->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2734
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2735, _M0L3valS2736);
      _M0L6_2atmpS2732 = _M0L6_2atmpS2733 + _M0L6_2atmpS2734;
      _M0L10dv__s__valS1131 = _M0L6_2atmpS2732 / _M0L6c__valS1120;
      _M0L5v__d1S2730 = _M0L1pS1111->$30;
      _M0L3valS2731 = _M0L1kS1112->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2729
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2730, _M0L3valS2731);
      _M0L6_2atmpS2728 = _M0L2elS1114 - _M0L6_2atmpS2729;
      _M0L6_2atmpS2723 = _M0L6_2atmpS2728 - _M0L3dd1S1125;
      _M0L2d1S2727 = _M0L1pS1111->$4;
      _M0L2gmS2725 = _M0L2d1S2727->$4;
      _M0L3valS2726 = _M0L1kS1112->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2724
      = _M0MPC15array5Array2atGfE(_M0L2gmS2725, _M0L3valS2726);
      _M0L6_2atmpS2719 = _M0L6_2atmpS2723 * _M0L6_2atmpS2724;
      _M0L13syn__curr__d1S2721 = _M0L1pS1111->$38;
      _M0L3valS2722 = _M0L1kS1112->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2720
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2721, _M0L3valS2722);
      _M0L6_2atmpS2718 = _M0L6_2atmpS2719 - _M0L6_2atmpS2720;
      _M0L6_2atmpS2714 = _M0L6_2atmpS2718 + _M0L3ic1S1128;
      _M0L5i__d1S2716 = _M0L1pS1111->$7;
      _M0L3valS2717 = _M0L1kS1112->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2715
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2716, _M0L3valS2717);
      _M0L6_2atmpS2709 = _M0L6_2atmpS2714 + _M0L6_2atmpS2715;
      _M0L2d1S2713 = _M0L1pS1111->$4;
      _M0L1cS2711 = _M0L2d1S2713->$2;
      _M0L3valS2712 = _M0L1kS1112->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2710
      = _M0MPC15array5Array2atGfE(_M0L1cS2711, _M0L3valS2712);
      _M0L11dv__d1__valS1132 = _M0L6_2atmpS2709 / _M0L6_2atmpS2710;
      _M0L5v__d2S2707 = _M0L1pS1111->$31;
      _M0L3valS2708 = _M0L1kS1112->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2706
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2707, _M0L3valS2708);
      _M0L6_2atmpS2705 = _M0L2elS1114 - _M0L6_2atmpS2706;
      _M0L6_2atmpS2700 = _M0L6_2atmpS2705 - _M0L3dd2S1126;
      _M0L2d2S2704 = _M0L1pS1111->$5;
      _M0L2gmS2702 = _M0L2d2S2704->$4;
      _M0L3valS2703 = _M0L1kS1112->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2701
      = _M0MPC15array5Array2atGfE(_M0L2gmS2702, _M0L3valS2703);
      _M0L6_2atmpS2696 = _M0L6_2atmpS2700 * _M0L6_2atmpS2701;
      _M0L13syn__curr__d2S2698 = _M0L1pS1111->$39;
      _M0L3valS2699 = _M0L1kS1112->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2697
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2698, _M0L3valS2699);
      _M0L6_2atmpS2695 = _M0L6_2atmpS2696 - _M0L6_2atmpS2697;
      _M0L6_2atmpS2691 = _M0L6_2atmpS2695 + _M0L3ic2S1129;
      _M0L5i__d2S2693 = _M0L1pS1111->$8;
      _M0L3valS2694 = _M0L1kS1112->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2692
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2693, _M0L3valS2694);
      _M0L6_2atmpS2686 = _M0L6_2atmpS2691 + _M0L6_2atmpS2692;
      _M0L2d2S2690 = _M0L1pS1111->$5;
      _M0L1cS2688 = _M0L2d2S2690->$2;
      _M0L3valS2689 = _M0L1kS1112->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2687
      = _M0MPC15array5Array2atGfE(_M0L1cS2688, _M0L3valS2689);
      _M0L11dv__d2__valS1133 = _M0L6_2atmpS2686 / _M0L6_2atmpS2687;
      _M0L4v__sS2684 = _M0L1pS1111->$28;
      _M0L3valS2685 = _M0L1kS1112->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2683
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2684, _M0L3valS2685);
      _M0L6_2atmpS2682 = _M0L6_2atmpS2683 + _M0L2dsS1122;
      _M0L6_2atmpS2681 = _M0L6_2atmpS2682 - _M0L2elS1114;
      _M0L6_2atmpS2676 = _M0L1aS1119 * _M0L6_2atmpS2681;
      _M0L4w__sS2679 = _M0L1pS1111->$29;
      _M0L3valS2680 = _M0L1kS1112->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2678
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2679, _M0L3valS2680);
      _M0L6_2atmpS2677 = _M0L6_2atmpS2678 + _M0L2dwS1127;
      _M0L6_2atmpS2675 = _M0L6_2atmpS2676 - _M0L6_2atmpS2677;
      _M0L7dw__valS1134 = _M0L6_2atmpS2675 / _M0L2twS1118;
      if (_M0L11store__tempS1123) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2643 = _M0L1pS1111->$36;
        int32_t _M0L3valS2645 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2644 = _M0L3valS2645 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2646;
        int32_t _M0L3valS2649;
        int32_t _M0L6_2atmpS2648;
        int32_t _M0L6_2atmpS2647;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2650;
        int32_t _M0L3valS2653;
        int32_t _M0L6_2atmpS2652;
        int32_t _M0L6_2atmpS2651;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2654;
        int32_t _M0L3valS2657;
        int32_t _M0L6_2atmpS2656;
        int32_t _M0L6_2atmpS2655;
        #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2643, _M0L6_2atmpS2644, _M0L10dv__s__valS1131);
        _M0L8dv__tempS2646 = _M0L1pS1111->$36;
        _M0L3valS2649 = _M0L1kS1112->$0;
        _M0L6_2atmpS2648 = _M0L3valS2649 * 4;
        _M0L6_2atmpS2647 = _M0L6_2atmpS2648 + 1;
        #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2646, _M0L6_2atmpS2647, _M0L11dv__d1__valS1132);
        _M0L8dv__tempS2650 = _M0L1pS1111->$36;
        _M0L3valS2653 = _M0L1kS1112->$0;
        _M0L6_2atmpS2652 = _M0L3valS2653 * 4;
        _M0L6_2atmpS2651 = _M0L6_2atmpS2652 + 2;
        #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2650, _M0L6_2atmpS2651, _M0L11dv__d2__valS1133);
        _M0L8dv__tempS2654 = _M0L1pS1111->$36;
        _M0L3valS2657 = _M0L1kS1112->$0;
        _M0L6_2atmpS2656 = _M0L3valS2657 * 4;
        _M0L6_2atmpS2655 = _M0L6_2atmpS2656 + 3;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2654, _M0L6_2atmpS2655, _M0L7dw__valS1134);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2658 = _M0L1pS1111->$35;
        int32_t _M0L3valS2660 = _M0L1kS1112->$0;
        int32_t _M0L6_2atmpS2659 = _M0L3valS2660 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2661;
        int32_t _M0L3valS2664;
        int32_t _M0L6_2atmpS2663;
        int32_t _M0L6_2atmpS2662;
        struct _M0TPB5ArrayGfE* _M0L2dvS2665;
        int32_t _M0L3valS2668;
        int32_t _M0L6_2atmpS2667;
        int32_t _M0L6_2atmpS2666;
        struct _M0TPB5ArrayGfE* _M0L2dvS2669;
        int32_t _M0L3valS2672;
        int32_t _M0L6_2atmpS2671;
        int32_t _M0L6_2atmpS2670;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2658, _M0L6_2atmpS2659, _M0L10dv__s__valS1131);
        _M0L2dvS2661 = _M0L1pS1111->$35;
        _M0L3valS2664 = _M0L1kS1112->$0;
        _M0L6_2atmpS2663 = _M0L3valS2664 * 4;
        _M0L6_2atmpS2662 = _M0L6_2atmpS2663 + 1;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2661, _M0L6_2atmpS2662, _M0L11dv__d1__valS1132);
        _M0L2dvS2665 = _M0L1pS1111->$35;
        _M0L3valS2668 = _M0L1kS1112->$0;
        _M0L6_2atmpS2667 = _M0L3valS2668 * 4;
        _M0L6_2atmpS2666 = _M0L6_2atmpS2667 + 2;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2665, _M0L6_2atmpS2666, _M0L11dv__d2__valS1133);
        _M0L2dvS2669 = _M0L1pS1111->$35;
        _M0L3valS2672 = _M0L1kS1112->$0;
        _M0L6_2atmpS2671 = _M0L3valS2672 * 4;
        _M0L6_2atmpS2670 = _M0L6_2atmpS2671 + 3;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2669, _M0L6_2atmpS2670, _M0L7dw__valS1134);
      }
      _M0L3valS2674 = _M0L1kS1112->$0;
      _M0L6_2atmpS2673 = _M0L3valS2674 + 1;
      _M0L1kS1112->$0 = _M0L6_2atmpS2673;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1112);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1106
) {
  int32_t _M0L1nS1105;
  int32_t _M0L7_2abindS1107;
  int32_t _M0L1iS1108;
  #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1105 = _M0L1pS1106->$27;
  _M0L7_2abindS1107 = 0;
  _M0L1iS1108 = _M0L7_2abindS1107;
  while (1) {
    if (_M0L1iS1108 < _M0L1nS1105) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2601 = _M0L1pS1106->$38;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2620 = _M0L1pS1106->$11;
      float _M0L6_2atmpS2615;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2619;
      float _M0L6_2atmpS2617;
      float _M0L4e__eS2618;
      float _M0L6_2atmpS2616;
      float _M0L6_2atmpS2613;
      float _M0L7gsyn__eS2614;
      float _M0L6_2atmpS2603;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2612;
      float _M0L6_2atmpS2607;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2611;
      float _M0L6_2atmpS2609;
      float _M0L4e__iS2610;
      float _M0L6_2atmpS2608;
      float _M0L6_2atmpS2605;
      float _M0L7gsyn__iS2606;
      float _M0L6_2atmpS2604;
      float _M0L6_2atmpS2602;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2621;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2640;
      float _M0L6_2atmpS2635;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2639;
      float _M0L6_2atmpS2637;
      float _M0L4e__eS2638;
      float _M0L6_2atmpS2636;
      float _M0L6_2atmpS2633;
      float _M0L7gsyn__eS2634;
      float _M0L6_2atmpS2623;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2632;
      float _M0L6_2atmpS2627;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2631;
      float _M0L6_2atmpS2629;
      float _M0L4e__iS2630;
      float _M0L6_2atmpS2628;
      float _M0L6_2atmpS2625;
      float _M0L7gsyn__iS2626;
      float _M0L6_2atmpS2624;
      float _M0L6_2atmpS2622;
      int32_t _M0L6_2atmpS2641;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2615
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2620, _M0L1iS1108);
      _M0L5v__d1S2619 = _M0L1pS1106->$30;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2617
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2619, _M0L1iS1108);
      _M0L4e__eS2618 = _M0L1pS1106->$21;
      _M0L6_2atmpS2616 = _M0L6_2atmpS2617 - _M0L4e__eS2618;
      _M0L6_2atmpS2613 = _M0L6_2atmpS2615 * _M0L6_2atmpS2616;
      _M0L7gsyn__eS2614 = _M0L1pS1106->$25;
      _M0L6_2atmpS2603 = _M0L6_2atmpS2613 * _M0L7gsyn__eS2614;
      _M0L6gi__d1S2612 = _M0L1pS1106->$12;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2607
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2612, _M0L1iS1108);
      _M0L5v__d1S2611 = _M0L1pS1106->$30;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2609
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2611, _M0L1iS1108);
      _M0L4e__iS2610 = _M0L1pS1106->$22;
      _M0L6_2atmpS2608 = _M0L6_2atmpS2609 - _M0L4e__iS2610;
      _M0L6_2atmpS2605 = _M0L6_2atmpS2607 * _M0L6_2atmpS2608;
      _M0L7gsyn__iS2606 = _M0L1pS1106->$26;
      _M0L6_2atmpS2604 = _M0L6_2atmpS2605 * _M0L7gsyn__iS2606;
      _M0L6_2atmpS2602 = _M0L6_2atmpS2603 + _M0L6_2atmpS2604;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S2601, _M0L1iS1108, _M0L6_2atmpS2602);
      _M0L13syn__curr__d2S2621 = _M0L1pS1106->$39;
      _M0L6ge__d2S2640 = _M0L1pS1106->$13;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2635
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2640, _M0L1iS1108);
      _M0L5v__d2S2639 = _M0L1pS1106->$31;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2637
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2639, _M0L1iS1108);
      _M0L4e__eS2638 = _M0L1pS1106->$21;
      _M0L6_2atmpS2636 = _M0L6_2atmpS2637 - _M0L4e__eS2638;
      _M0L6_2atmpS2633 = _M0L6_2atmpS2635 * _M0L6_2atmpS2636;
      _M0L7gsyn__eS2634 = _M0L1pS1106->$25;
      _M0L6_2atmpS2623 = _M0L6_2atmpS2633 * _M0L7gsyn__eS2634;
      _M0L6gi__d2S2632 = _M0L1pS1106->$14;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2627
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2632, _M0L1iS1108);
      _M0L5v__d2S2631 = _M0L1pS1106->$31;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2629
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2631, _M0L1iS1108);
      _M0L4e__iS2630 = _M0L1pS1106->$22;
      _M0L6_2atmpS2628 = _M0L6_2atmpS2629 - _M0L4e__iS2630;
      _M0L6_2atmpS2625 = _M0L6_2atmpS2627 * _M0L6_2atmpS2628;
      _M0L7gsyn__iS2626 = _M0L1pS1106->$26;
      _M0L6_2atmpS2624 = _M0L6_2atmpS2625 * _M0L7gsyn__iS2626;
      _M0L6_2atmpS2622 = _M0L6_2atmpS2623 + _M0L6_2atmpS2624;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S2621, _M0L1iS1108, _M0L6_2atmpS2622);
      _M0L6_2atmpS2641 = _M0L1iS1108 + 1;
      _M0L1iS1108 = _M0L6_2atmpS2641;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1101
) {
  int32_t _M0L1nS1100;
  int32_t _M0L7_2abindS1102;
  int32_t _M0L1iS1103;
  #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1100 = _M0L1pS1101->$27;
  _M0L7_2abindS1102 = 0;
  _M0L1iS1103 = _M0L7_2abindS1102;
  while (1) {
    if (_M0L1iS1103 < _M0L1nS1100) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2580 = _M0L1pS1101->$37;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2599 = _M0L1pS1101->$9;
      float _M0L6_2atmpS2594;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2598;
      float _M0L6_2atmpS2596;
      float _M0L4e__eS2597;
      float _M0L6_2atmpS2595;
      float _M0L6_2atmpS2592;
      float _M0L7gsyn__eS2593;
      float _M0L6_2atmpS2582;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2591;
      float _M0L6_2atmpS2586;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2590;
      float _M0L6_2atmpS2588;
      float _M0L4e__iS2589;
      float _M0L6_2atmpS2587;
      float _M0L6_2atmpS2584;
      float _M0L7gsyn__iS2585;
      float _M0L6_2atmpS2583;
      float _M0L6_2atmpS2581;
      int32_t _M0L6_2atmpS2600;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2594
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2599, _M0L1iS1103);
      _M0L4v__sS2598 = _M0L1pS1101->$28;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2596
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2598, _M0L1iS1103);
      _M0L4e__eS2597 = _M0L1pS1101->$21;
      _M0L6_2atmpS2595 = _M0L6_2atmpS2596 - _M0L4e__eS2597;
      _M0L6_2atmpS2592 = _M0L6_2atmpS2594 * _M0L6_2atmpS2595;
      _M0L7gsyn__eS2593 = _M0L1pS1101->$25;
      _M0L6_2atmpS2582 = _M0L6_2atmpS2592 * _M0L7gsyn__eS2593;
      _M0L5gi__sS2591 = _M0L1pS1101->$10;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2586
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2591, _M0L1iS1103);
      _M0L4v__sS2590 = _M0L1pS1101->$28;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2588
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2590, _M0L1iS1103);
      _M0L4e__iS2589 = _M0L1pS1101->$22;
      _M0L6_2atmpS2587 = _M0L6_2atmpS2588 - _M0L4e__iS2589;
      _M0L6_2atmpS2584 = _M0L6_2atmpS2586 * _M0L6_2atmpS2587;
      _M0L7gsyn__iS2585 = _M0L1pS1101->$26;
      _M0L6_2atmpS2583 = _M0L6_2atmpS2584 * _M0L7gsyn__iS2585;
      _M0L6_2atmpS2581 = _M0L6_2atmpS2582 + _M0L6_2atmpS2583;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2580, _M0L1iS1103, _M0L6_2atmpS2581);
      _M0L6_2atmpS2600 = _M0L1iS1103 + 1;
      _M0L1iS1103 = _M0L6_2atmpS2600;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1092,
  float _M0L2dtS1095
) {
  int32_t _M0L1nS1091;
  int32_t _M0L7_2abindS1093;
  int32_t _M0L1iS1094;
  int32_t _M0L7_2abindS1097;
  int32_t _M0L1iS1098;
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1091 = _M0L1pS1092->$27;
  _M0L7_2abindS1093 = 0;
  _M0L1iS1094 = _M0L7_2abindS1093;
  while (1) {
    if (_M0L1iS1094 < _M0L1nS1091) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2510 = _M0L1pS1092->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2515 = _M0L1pS1092->$11;
      float _M0L6_2atmpS2512;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2514;
      float _M0L6_2atmpS2513;
      float _M0L6_2atmpS2511;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2516;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2521;
      float _M0L6_2atmpS2518;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2520;
      float _M0L6_2atmpS2519;
      float _M0L6_2atmpS2517;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2522;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2531;
      float _M0L6_2atmpS2524;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2530;
      float _M0L6_2atmpS2529;
      float _M0L6_2atmpS2527;
      float _M0L6tau__eS2528;
      float _M0L6_2atmpS2526;
      float _M0L6_2atmpS2525;
      float _M0L6_2atmpS2523;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2532;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2541;
      float _M0L6_2atmpS2534;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2540;
      float _M0L6_2atmpS2539;
      float _M0L6_2atmpS2537;
      float _M0L6tau__iS2538;
      float _M0L6_2atmpS2536;
      float _M0L6_2atmpS2535;
      float _M0L6_2atmpS2533;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2542;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2547;
      float _M0L6_2atmpS2544;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2546;
      float _M0L6_2atmpS2545;
      float _M0L6_2atmpS2543;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2548;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2553;
      float _M0L6_2atmpS2550;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2552;
      float _M0L6_2atmpS2551;
      float _M0L6_2atmpS2549;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2554;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2563;
      float _M0L6_2atmpS2556;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2562;
      float _M0L6_2atmpS2561;
      float _M0L6_2atmpS2559;
      float _M0L6tau__eS2560;
      float _M0L6_2atmpS2558;
      float _M0L6_2atmpS2557;
      float _M0L6_2atmpS2555;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2564;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2573;
      float _M0L6_2atmpS2566;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2572;
      float _M0L6_2atmpS2571;
      float _M0L6_2atmpS2569;
      float _M0L6tau__iS2570;
      float _M0L6_2atmpS2568;
      float _M0L6_2atmpS2567;
      float _M0L6_2atmpS2565;
      int32_t _M0L6_2atmpS2574;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2512
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2515, _M0L1iS1094);
      _M0L7glu__d1S2514 = _M0L1pS1092->$17;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2513
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S2514, _M0L1iS1094);
      _M0L6_2atmpS2511 = _M0L6_2atmpS2512 + _M0L6_2atmpS2513;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2510, _M0L1iS1094, _M0L6_2atmpS2511);
      _M0L6gi__d1S2516 = _M0L1pS1092->$12;
      _M0L6gi__d1S2521 = _M0L1pS1092->$12;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2518
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2521, _M0L1iS1094);
      _M0L8gaba__d1S2520 = _M0L1pS1092->$18;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2519
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S2520, _M0L1iS1094);
      _M0L6_2atmpS2517 = _M0L6_2atmpS2518 + _M0L6_2atmpS2519;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2516, _M0L1iS1094, _M0L6_2atmpS2517);
      _M0L6ge__d1S2522 = _M0L1pS1092->$11;
      _M0L6ge__d1S2531 = _M0L1pS1092->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2524
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2531, _M0L1iS1094);
      _M0L6ge__d1S2530 = _M0L1pS1092->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2529
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2530, _M0L1iS1094);
      _M0L6_2atmpS2527 = -_M0L6_2atmpS2529;
      _M0L6tau__eS2528 = _M0L1pS1092->$23;
      _M0L6_2atmpS2526 = _M0L6_2atmpS2527 / _M0L6tau__eS2528;
      _M0L6_2atmpS2525 = _M0L2dtS1095 * _M0L6_2atmpS2526;
      _M0L6_2atmpS2523 = _M0L6_2atmpS2524 + _M0L6_2atmpS2525;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2522, _M0L1iS1094, _M0L6_2atmpS2523);
      _M0L6gi__d1S2532 = _M0L1pS1092->$12;
      _M0L6gi__d1S2541 = _M0L1pS1092->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2534
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2541, _M0L1iS1094);
      _M0L6gi__d1S2540 = _M0L1pS1092->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2539
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2540, _M0L1iS1094);
      _M0L6_2atmpS2537 = -_M0L6_2atmpS2539;
      _M0L6tau__iS2538 = _M0L1pS1092->$24;
      _M0L6_2atmpS2536 = _M0L6_2atmpS2537 / _M0L6tau__iS2538;
      _M0L6_2atmpS2535 = _M0L2dtS1095 * _M0L6_2atmpS2536;
      _M0L6_2atmpS2533 = _M0L6_2atmpS2534 + _M0L6_2atmpS2535;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2532, _M0L1iS1094, _M0L6_2atmpS2533);
      _M0L6ge__d2S2542 = _M0L1pS1092->$13;
      _M0L6ge__d2S2547 = _M0L1pS1092->$13;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2544
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2547, _M0L1iS1094);
      _M0L7glu__d2S2546 = _M0L1pS1092->$19;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2545
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S2546, _M0L1iS1094);
      _M0L6_2atmpS2543 = _M0L6_2atmpS2544 + _M0L6_2atmpS2545;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2542, _M0L1iS1094, _M0L6_2atmpS2543);
      _M0L6gi__d2S2548 = _M0L1pS1092->$14;
      _M0L6gi__d2S2553 = _M0L1pS1092->$14;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2550
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2553, _M0L1iS1094);
      _M0L8gaba__d2S2552 = _M0L1pS1092->$20;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2551
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S2552, _M0L1iS1094);
      _M0L6_2atmpS2549 = _M0L6_2atmpS2550 + _M0L6_2atmpS2551;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2548, _M0L1iS1094, _M0L6_2atmpS2549);
      _M0L6ge__d2S2554 = _M0L1pS1092->$13;
      _M0L6ge__d2S2563 = _M0L1pS1092->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2556
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2563, _M0L1iS1094);
      _M0L6ge__d2S2562 = _M0L1pS1092->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2561
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2562, _M0L1iS1094);
      _M0L6_2atmpS2559 = -_M0L6_2atmpS2561;
      _M0L6tau__eS2560 = _M0L1pS1092->$23;
      _M0L6_2atmpS2558 = _M0L6_2atmpS2559 / _M0L6tau__eS2560;
      _M0L6_2atmpS2557 = _M0L2dtS1095 * _M0L6_2atmpS2558;
      _M0L6_2atmpS2555 = _M0L6_2atmpS2556 + _M0L6_2atmpS2557;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2554, _M0L1iS1094, _M0L6_2atmpS2555);
      _M0L6gi__d2S2564 = _M0L1pS1092->$14;
      _M0L6gi__d2S2573 = _M0L1pS1092->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2566
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2573, _M0L1iS1094);
      _M0L6gi__d2S2572 = _M0L1pS1092->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2571
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2572, _M0L1iS1094);
      _M0L6_2atmpS2569 = -_M0L6_2atmpS2571;
      _M0L6tau__iS2570 = _M0L1pS1092->$24;
      _M0L6_2atmpS2568 = _M0L6_2atmpS2569 / _M0L6tau__iS2570;
      _M0L6_2atmpS2567 = _M0L2dtS1095 * _M0L6_2atmpS2568;
      _M0L6_2atmpS2565 = _M0L6_2atmpS2566 + _M0L6_2atmpS2567;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2564, _M0L1iS1094, _M0L6_2atmpS2565);
      _M0L6_2atmpS2574 = _M0L1iS1094 + 1;
      _M0L1iS1094 = _M0L6_2atmpS2574;
      continue;
    }
    break;
  }
  _M0L7_2abindS1097 = 0;
  _M0L1iS1098 = _M0L7_2abindS1097;
  while (1) {
    if (_M0L1iS1098 < _M0L1nS1091) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2575 = _M0L1pS1092->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2576;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2577;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2578;
      int32_t _M0L6_2atmpS2579;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S2575, _M0L1iS1098, 0x0p+0f);
      _M0L8gaba__d1S2576 = _M0L1pS1092->$18;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S2576, _M0L1iS1098, 0x0p+0f);
      _M0L7glu__d2S2577 = _M0L1pS1092->$19;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S2577, _M0L1iS1098, 0x0p+0f);
      _M0L8gaba__d2S2578 = _M0L1pS1092->$20;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S2578, _M0L1iS1098, 0x0p+0f);
      _M0L6_2atmpS2579 = _M0L1iS1098 + 1;
      _M0L1iS1098 = _M0L6_2atmpS2579;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS1083,
  float _M0L2dtS1086
) {
  int32_t _M0L1nS1082;
  int32_t _M0L7_2abindS1084;
  int32_t _M0L1iS1085;
  int32_t _M0L7_2abindS1088;
  int32_t _M0L1iS1089;
  #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS1082 = _M0L1pS1083->$27;
  _M0L7_2abindS1084 = 0;
  _M0L1iS1085 = _M0L7_2abindS1084;
  while (1) {
    if (_M0L1iS1085 < _M0L1nS1082) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2474 = _M0L1pS1083->$9;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2479 = _M0L1pS1083->$9;
      float _M0L6_2atmpS2476;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2478;
      float _M0L6_2atmpS2477;
      float _M0L6_2atmpS2475;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2480;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2485;
      float _M0L6_2atmpS2482;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2484;
      float _M0L6_2atmpS2483;
      float _M0L6_2atmpS2481;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2486;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2495;
      float _M0L6_2atmpS2488;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2494;
      float _M0L6_2atmpS2493;
      float _M0L6_2atmpS2491;
      float _M0L6tau__eS2492;
      float _M0L6_2atmpS2490;
      float _M0L6_2atmpS2489;
      float _M0L6_2atmpS2487;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2496;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2505;
      float _M0L6_2atmpS2498;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2504;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2501;
      float _M0L6tau__iS2502;
      float _M0L6_2atmpS2500;
      float _M0L6_2atmpS2499;
      float _M0L6_2atmpS2497;
      int32_t _M0L6_2atmpS2506;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2476
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2479, _M0L1iS1085);
      _M0L6glu__sS2478 = _M0L1pS1083->$15;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2477
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2478, _M0L1iS1085);
      _M0L6_2atmpS2475 = _M0L6_2atmpS2476 + _M0L6_2atmpS2477;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2474, _M0L1iS1085, _M0L6_2atmpS2475);
      _M0L5gi__sS2480 = _M0L1pS1083->$10;
      _M0L5gi__sS2485 = _M0L1pS1083->$10;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2482
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2485, _M0L1iS1085);
      _M0L7gaba__sS2484 = _M0L1pS1083->$16;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2483
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2484, _M0L1iS1085);
      _M0L6_2atmpS2481 = _M0L6_2atmpS2482 + _M0L6_2atmpS2483;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2480, _M0L1iS1085, _M0L6_2atmpS2481);
      _M0L5ge__sS2486 = _M0L1pS1083->$9;
      _M0L5ge__sS2495 = _M0L1pS1083->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2488
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2495, _M0L1iS1085);
      _M0L5ge__sS2494 = _M0L1pS1083->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2493
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2494, _M0L1iS1085);
      _M0L6_2atmpS2491 = -_M0L6_2atmpS2493;
      _M0L6tau__eS2492 = _M0L1pS1083->$23;
      _M0L6_2atmpS2490 = _M0L6_2atmpS2491 / _M0L6tau__eS2492;
      _M0L6_2atmpS2489 = _M0L2dtS1086 * _M0L6_2atmpS2490;
      _M0L6_2atmpS2487 = _M0L6_2atmpS2488 + _M0L6_2atmpS2489;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2486, _M0L1iS1085, _M0L6_2atmpS2487);
      _M0L5gi__sS2496 = _M0L1pS1083->$10;
      _M0L5gi__sS2505 = _M0L1pS1083->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2498
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2505, _M0L1iS1085);
      _M0L5gi__sS2504 = _M0L1pS1083->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2503
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2504, _M0L1iS1085);
      _M0L6_2atmpS2501 = -_M0L6_2atmpS2503;
      _M0L6tau__iS2502 = _M0L1pS1083->$24;
      _M0L6_2atmpS2500 = _M0L6_2atmpS2501 / _M0L6tau__iS2502;
      _M0L6_2atmpS2499 = _M0L2dtS1086 * _M0L6_2atmpS2500;
      _M0L6_2atmpS2497 = _M0L6_2atmpS2498 + _M0L6_2atmpS2499;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2496, _M0L1iS1085, _M0L6_2atmpS2497);
      _M0L6_2atmpS2506 = _M0L1iS1085 + 1;
      _M0L1iS1085 = _M0L6_2atmpS2506;
      continue;
    }
    break;
  }
  _M0L7_2abindS1088 = 0;
  _M0L1iS1089 = _M0L7_2abindS1088;
  while (1) {
    if (_M0L1iS1089 < _M0L1nS1082) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2507 = _M0L1pS1083->$15;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2508;
      int32_t _M0L6_2atmpS2509;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2507, _M0L1iS1089, 0x0p+0f);
      _M0L7gaba__sS2508 = _M0L1pS1083->$16;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2508, _M0L1iS1089, 0x0p+0f);
      _M0L6_2atmpS2509 = _M0L1iS1089 + 1;
      _M0L1iS1089 = _M0L6_2atmpS2509;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9TripodHet* _M0MP26RiantR8snn__mbt9TripodHet3new(
  int32_t _M0L1nS1038,
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS1042,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1050
) {
  struct _M0TPB5ArrayGfE* _M0L1cS1037;
  struct _M0TPB5ArrayGfE* _M0L2glS1039;
  int32_t _M0L7_2abindS1040;
  int32_t _M0L1kS1041;
  struct _M0TPB5ArrayGfE* _M0L4v__sS1044;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S1045;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S1046;
  int32_t _M0L7_2abindS1047;
  int32_t _M0L1kS1048;
  struct _M0TPB5ArrayGfE* _M0L4w__sS1052;
  struct _M0TPB5ArrayGbE* _M0L4fireS1053;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1054;
  int32_t _M0L7_2abindS1055;
  int32_t _M0L1kS1056;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1058;
  struct _M0TPB5ArrayGfE* _M0L4i__sS1059;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S1060;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S1061;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS1062;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS1063;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S1064;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S1065;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S1066;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S1067;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS1068;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS1069;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S1070;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S1071;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S1072;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S1073;
  int32_t _M0L6total4S1074;
  struct _M0TPB5ArrayGfE* _M0L2dvS1075;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS1076;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1077;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S1078;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S1079;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S1080;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S1081;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2473;
  struct _M0TP26RiantR8snn__mbt9TripodHet* _block_3238;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1cS1037 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2glS1039 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  _M0L7_2abindS1040 = 0;
  _M0L1kS1041 = _M0L7_2abindS1040;
  while (1) {
    if (_M0L1kS1041 < _M0L1nS1038) {
      struct _M0TPB5ArrayGfE* _M0L2tmS2445 = _M0L11soma__paramS1042->$3;
      float _M0L6_2atmpS2441;
      struct _M0TPB5ArrayGfE* _M0L1rS2444;
      float _M0L6_2atmpS2443;
      float _M0L6_2atmpS2442;
      float _M0L6_2atmpS2440;
      struct _M0TPB5ArrayGfE* _M0L1rS2448;
      float _M0L6_2atmpS2447;
      float _M0L6_2atmpS2446;
      int32_t _M0L6_2atmpS2449;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2441 = _M0MPC15array5Array2atGfE(_M0L2tmS2445, _M0L1kS1041);
      _M0L1rS2444 = _M0L11soma__paramS1042->$4;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2443 = _M0MPC15array5Array2atGfE(_M0L1rS2444, _M0L1kS1041);
      _M0L6_2atmpS2442 = 0x1p+0f / _M0L6_2atmpS2443;
      _M0L6_2atmpS2440 = _M0L6_2atmpS2441 * _M0L6_2atmpS2442;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L1cS1037, _M0L1kS1041, _M0L6_2atmpS2440);
      _M0L1rS2448 = _M0L11soma__paramS1042->$4;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2447 = _M0MPC15array5Array2atGfE(_M0L1rS2448, _M0L1kS1041);
      _M0L6_2atmpS2446 = 0x1p+0f / _M0L6_2atmpS2447;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L2glS1039, _M0L1kS1041, _M0L6_2atmpS2446);
      _M0L6_2atmpS2449 = _M0L1kS1041 + 1;
      _M0L1kS1041 = _M0L6_2atmpS2449;
      continue;
    }
    break;
  }
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4v__sS1044 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d1S1045 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d2S1046 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  _M0L7_2abindS1047 = 0;
  _M0L1kS1048 = _M0L7_2abindS1047;
  while (1) {
    if (_M0L1kS1048 < _M0L1nS1038) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2468 = _M0L11soma__paramS1042->$0;
      float _M0L6_2atmpS2465;
      struct _M0TPB5ArrayGfE* _M0L2vrS2467;
      float _M0L6_2atmpS2466;
      float _M0L6spreadS1049;
      struct _M0TPB5ArrayGfE* _M0L2vrS2454;
      float _M0L6_2atmpS2451;
      float _M0L6_2atmpS2453;
      float _M0L6_2atmpS2452;
      float _M0L6_2atmpS2450;
      struct _M0TPB5ArrayGfE* _M0L2vrS2459;
      float _M0L6_2atmpS2456;
      float _M0L6_2atmpS2458;
      float _M0L6_2atmpS2457;
      float _M0L6_2atmpS2455;
      struct _M0TPB5ArrayGfE* _M0L2vrS2464;
      float _M0L6_2atmpS2461;
      float _M0L6_2atmpS2463;
      float _M0L6_2atmpS2462;
      float _M0L6_2atmpS2460;
      int32_t _M0L6_2atmpS2469;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2465 = _M0MPC15array5Array2atGfE(_M0L2vtS2468, _M0L1kS1048);
      _M0L2vrS2467 = _M0L11soma__paramS1042->$1;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2466 = _M0MPC15array5Array2atGfE(_M0L2vrS2467, _M0L1kS1048);
      _M0L6spreadS1049 = _M0L6_2atmpS2465 - _M0L6_2atmpS2466;
      _M0L2vrS2454 = _M0L11soma__paramS1042->$1;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2451 = _M0MPC15array5Array2atGfE(_M0L2vrS2454, _M0L1kS1048);
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2453 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1050);
      _M0L6_2atmpS2452 = _M0L6_2atmpS2453 * _M0L6spreadS1049;
      _M0L6_2atmpS2450 = _M0L6_2atmpS2451 + _M0L6_2atmpS2452;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS1044, _M0L1kS1048, _M0L6_2atmpS2450);
      _M0L2vrS2459 = _M0L11soma__paramS1042->$1;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2456 = _M0MPC15array5Array2atGfE(_M0L2vrS2459, _M0L1kS1048);
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2458 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1050);
      _M0L6_2atmpS2457 = _M0L6_2atmpS2458 * _M0L6spreadS1049;
      _M0L6_2atmpS2455 = _M0L6_2atmpS2456 + _M0L6_2atmpS2457;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S1045, _M0L1kS1048, _M0L6_2atmpS2455);
      _M0L2vrS2464 = _M0L11soma__paramS1042->$1;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2461 = _M0MPC15array5Array2atGfE(_M0L2vrS2464, _M0L1kS1048);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2463 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1050);
      _M0L6_2atmpS2462 = _M0L6_2atmpS2463 * _M0L6spreadS1049;
      _M0L6_2atmpS2460 = _M0L6_2atmpS2461 + _M0L6_2atmpS2462;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S1046, _M0L1kS1048, _M0L6_2atmpS2460);
      _M0L6_2atmpS2469 = _M0L1kS1048 + 1;
      _M0L1kS1048 = _M0L6_2atmpS2469;
      continue;
    }
    break;
  }
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4w__sS1052 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4fireS1053 = _M0MPC15array5Array4makeGbE(_M0L1nS1038, 0);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L9thresholdS1054 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  _M0L7_2abindS1055 = 0;
  _M0L1kS1056 = _M0L7_2abindS1055;
  while (1) {
    if (_M0L1kS1056 < _M0L1nS1038) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2471 = _M0L11soma__paramS1042->$0;
      float _M0L6_2atmpS2470;
      int32_t _M0L6_2atmpS2472;
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2470 = _M0MPC15array5Array2atGfE(_M0L2vtS2471, _M0L1kS1056);
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS1054, _M0L1kS1056, _M0L6_2atmpS2470);
      _M0L6_2atmpS2472 = _M0L1kS1056 + 1;
      _M0L1kS1056 = _M0L6_2atmpS2472;
      continue;
    }
    break;
  }
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4tabsS1058 = _M0MPC15array5Array4makeGiE(_M0L1nS1038, 1);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4i__sS1059 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d1S1060 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d2S1061 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5ge__sS1062 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5gi__sS1063 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d1S1064 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d1S1065 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d2S1066 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d2S1067 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6glu__sS1068 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7gaba__sS1069 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d1S1070 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d1S1071 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d2S1072 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d2S1073 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  _M0L6total4S1074 = _M0L1nS1038 * 4;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2dvS1075 = _M0MPC15array5Array4makeGfE(_M0L6total4S1074, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8dv__tempS1076 = _M0MPC15array5Array4makeGfE(_M0L6total4S1074, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L12syn__curr__sS1077 = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d1S1078
  = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d2S1079
  = _M0MPC15array5Array4makeGfE(_M0L1nS1038, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d1S1080 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1038);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d2S1081 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1038);
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6_2atmpS2473 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS1042);
  _block_3238
  = (struct _M0TP26RiantR8snn__mbt9TripodHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9TripodHet));
  Moonbit_object_header(_block_3238)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _block_3238->$0 = _M0L11soma__paramS1042;
  _block_3238->$1 = _M0L6_2atmpS2473;
  _block_3238->$2 = _M0L1cS1037;
  _block_3238->$3 = _M0L2glS1039;
  _block_3238->$4 = _M0L2d1S1080;
  _block_3238->$5 = _M0L2d2S1081;
  _block_3238->$6 = _M0L4i__sS1059;
  _block_3238->$7 = _M0L5i__d1S1060;
  _block_3238->$8 = _M0L5i__d2S1061;
  _block_3238->$9 = _M0L5ge__sS1062;
  _block_3238->$10 = _M0L5gi__sS1063;
  _block_3238->$11 = _M0L6ge__d1S1064;
  _block_3238->$12 = _M0L6gi__d1S1065;
  _block_3238->$13 = _M0L6ge__d2S1066;
  _block_3238->$14 = _M0L6gi__d2S1067;
  _block_3238->$15 = _M0L6glu__sS1068;
  _block_3238->$16 = _M0L7gaba__sS1069;
  _block_3238->$17 = _M0L7glu__d1S1070;
  _block_3238->$18 = _M0L8gaba__d1S1071;
  _block_3238->$19 = _M0L7glu__d2S1072;
  _block_3238->$20 = _M0L8gaba__d2S1073;
  _block_3238->$21 = 0x0p+0f;
  _block_3238->$22 = -0x1.2cp+6f;
  _block_3238->$23 = 0x1.8p+2f;
  _block_3238->$24 = 0x1p+1f;
  _block_3238->$25 = 0x1p+0f;
  _block_3238->$26 = 0x1p+0f;
  _block_3238->$27 = _M0L1nS1038;
  _block_3238->$28 = _M0L4v__sS1044;
  _block_3238->$29 = _M0L4w__sS1052;
  _block_3238->$30 = _M0L5v__d1S1045;
  _block_3238->$31 = _M0L5v__d2S1046;
  _block_3238->$32 = _M0L4fireS1053;
  _block_3238->$33 = _M0L9thresholdS1054;
  _block_3238->$34 = _M0L4tabsS1058;
  _block_3238->$35 = _M0L2dvS1075;
  _block_3238->$36 = _M0L8dv__tempS1076;
  _block_3238->$37 = _M0L12syn__curr__sS1077;
  _block_3238->$38 = _M0L13syn__curr__d1S1078;
  _block_3238->$39 = _M0L13syn__curr__d2S1079;
  return _block_3238;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS1030
) {
  struct _M0TPB5ArrayGfE* _M0L2elS1029;
  struct _M0TPB5ArrayGfE* _M0L1cS1031;
  struct _M0TPB5ArrayGfE* _M0L3gaxS1032;
  struct _M0TPB5ArrayGfE* _M0L2gmS1033;
  struct _M0TPB5ArrayGfE* _M0L1lS1034;
  struct _M0TPB5ArrayGfE* _M0L1dS1035;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS1036;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_3239;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS1029
  = _M0MPC15array5Array4makeGfE(_M0L1nS1030, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS1031 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS1032 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS1033 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS1034 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS1035 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS1036 = _M0MPC15array5Array4makeGfE(_M0L1nS1030, 0x0p+0f);
  _block_3239
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_3239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 94, 0);
  _block_3239->$0 = _M0L1nS1030;
  _block_3239->$1 = _M0L2elS1029;
  _block_3239->$2 = _M0L1cS1031;
  _block_3239->$3 = _M0L3gaxS1032;
  _block_3239->$4 = _M0L2gmS1033;
  _block_3239->$5 = _M0L1lS1034;
  _block_3239->$6 = _M0L1dS1035;
  _block_3239->$7 = _M0L11gax__parentS1036;
  return _block_3239;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_3240;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_3240
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_3240)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3240->$0 = 0x0p+0f;
  _block_3240->$1 = 0x1.4p+3f;
  _block_3240->$2 = 0x1.4p+3f;
  _block_3240->$3 = 0x1p+0f;
  _block_3240->$4 = 0x1p+0f;
  return _block_3240;
}

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE* _M0L1gS1015,
  struct _M0TPB5ArrayGfE* _M0L1vS1022,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS1017,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS1023,
  struct _M0TPB5ArrayGfE* _M0L3outS1024
) {
  int32_t _M0L1nS1014;
  float _M0L4gsynS1016;
  float _M0L6e__revS1018;
  #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS1014 = _M0MPC15array5Array6lengthGfE(_M0L1gS1015);
  _M0L4gsynS1016 = _M0L1rS1017->$4;
  _M0L6e__revS1018 = _M0L1rS1017->$0;
  if (_M0L1rS1017->$8) {
    int32_t _M0L7_2abindS1019 = 0;
    int32_t _M0L1iS1020 = _M0L7_2abindS1019;
    while (1) {
      if (_M0L1iS1020 < _M0L1nS1014) {
        float _M0L6_2atmpS2430;
        float _M0L1bS1021;
        float _M0L6_2atmpS2423;
        float _M0L6_2atmpS2429;
        float _M0L6_2atmpS2426;
        float _M0L6_2atmpS2428;
        float _M0L6_2atmpS2427;
        float _M0L6_2atmpS2425;
        float _M0L6_2atmpS2424;
        float _M0L6_2atmpS2422;
        int32_t _M0L6_2atmpS2431;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2430
        = _M0MPC15array5Array2atGfE(_M0L1vS1022, _M0L1iS1020);
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L1bS1021
        = _M0FP26RiantR8snn__mbt12nmda__gating(_M0L6_2atmpS2430, _M0L9nmda__depS1023);
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2423
        = _M0MPC15array5Array2atGfE(_M0L3outS1024, _M0L1iS1020);
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2429
        = _M0MPC15array5Array2atGfE(_M0L1gS1015, _M0L1iS1020);
        _M0L6_2atmpS2426 = _M0L4gsynS1016 * _M0L6_2atmpS2429;
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2428
        = _M0MPC15array5Array2atGfE(_M0L1vS1022, _M0L1iS1020);
        _M0L6_2atmpS2427 = _M0L6_2atmpS2428 - _M0L6e__revS1018;
        _M0L6_2atmpS2425 = _M0L6_2atmpS2426 * _M0L6_2atmpS2427;
        _M0L6_2atmpS2424 = _M0L6_2atmpS2425 * _M0L1bS1021;
        _M0L6_2atmpS2422 = _M0L6_2atmpS2423 + _M0L6_2atmpS2424;
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS1024, _M0L1iS1020, _M0L6_2atmpS2422);
        _M0L6_2atmpS2431 = _M0L1iS1020 + 1;
        _M0L1iS1020 = _M0L6_2atmpS2431;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L7_2abindS1026 = 0;
    int32_t _M0L1iS1027 = _M0L7_2abindS1026;
    while (1) {
      if (_M0L1iS1027 < _M0L1nS1014) {
        float _M0L6_2atmpS2433;
        float _M0L6_2atmpS2438;
        float _M0L6_2atmpS2435;
        float _M0L6_2atmpS2437;
        float _M0L6_2atmpS2436;
        float _M0L6_2atmpS2434;
        float _M0L6_2atmpS2432;
        int32_t _M0L6_2atmpS2439;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2433
        = _M0MPC15array5Array2atGfE(_M0L3outS1024, _M0L1iS1027);
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2438
        = _M0MPC15array5Array2atGfE(_M0L1gS1015, _M0L1iS1027);
        _M0L6_2atmpS2435 = _M0L4gsynS1016 * _M0L6_2atmpS2438;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS2437
        = _M0MPC15array5Array2atGfE(_M0L1vS1022, _M0L1iS1027);
        _M0L6_2atmpS2436 = _M0L6_2atmpS2437 - _M0L6e__revS1018;
        _M0L6_2atmpS2434 = _M0L6_2atmpS2435 * _M0L6_2atmpS2436;
        _M0L6_2atmpS2432 = _M0L6_2atmpS2433 + _M0L6_2atmpS2434;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS1024, _M0L1iS1027, _M0L6_2atmpS2432);
        _M0L6_2atmpS2439 = _M0L1iS1027 + 1;
        _M0L1iS1027 = _M0L6_2atmpS2439;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__receptor(
  struct _M0TPB5ArrayGfE* _M0L1gS1001,
  struct _M0TPB5ArrayGfE* _M0L1hS1011,
  struct _M0TPB5ArrayGfE* _M0L6targetS1012,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS1003,
  float _M0L2dtS1007
) {
  int32_t _M0L1nS1000;
  float _M0L5alphaS1002;
  float _M0L7tr__invS1004;
  float _M0L7td__invS1005;
  float _M0L6_2atmpS2421;
  float _M0L6_2atmpS2420;
  float _M0L8decay__dS1006;
  float _M0L6_2atmpS2419;
  float _M0L6_2atmpS2418;
  float _M0L8decay__rS1008;
  int32_t _M0L7_2abindS1009;
  int32_t _M0L1iS1010;
  #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS1000 = _M0MPC15array5Array6lengthGfE(_M0L1gS1001);
  _M0L5alphaS1002 = _M0L1rS1003->$5;
  _M0L7tr__invS1004 = _M0L1rS1003->$6;
  _M0L7td__invS1005 = _M0L1rS1003->$7;
  _M0L6_2atmpS2421 = -_M0L2dtS1007;
  _M0L6_2atmpS2420 = _M0L6_2atmpS2421 * _M0L7td__invS1005;
  #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L8decay__dS1006 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2420);
  _M0L6_2atmpS2419 = -_M0L2dtS1007;
  _M0L6_2atmpS2418 = _M0L6_2atmpS2419 * _M0L7tr__invS1004;
  #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L8decay__rS1008 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2418);
  _M0L7_2abindS1009 = 0;
  _M0L1iS1010 = _M0L7_2abindS1009;
  while (1) {
    if (_M0L1iS1010 < _M0L1nS1000) {
      float _M0L6_2atmpS2407;
      float _M0L6_2atmpS2409;
      float _M0L6_2atmpS2408;
      float _M0L6_2atmpS2406;
      float _M0L6_2atmpS2412;
      float _M0L6_2atmpS2414;
      float _M0L6_2atmpS2413;
      float _M0L6_2atmpS2411;
      float _M0L6_2atmpS2410;
      float _M0L6_2atmpS2416;
      float _M0L6_2atmpS2415;
      int32_t _M0L6_2atmpS2417;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS2407 = _M0MPC15array5Array2atGfE(_M0L1hS1011, _M0L1iS1010);
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS2409
      = _M0MPC15array5Array2atGfE(_M0L6targetS1012, _M0L1iS1010);
      _M0L6_2atmpS2408 = _M0L6_2atmpS2409 * _M0L5alphaS1002;
      _M0L6_2atmpS2406 = _M0L6_2atmpS2407 + _M0L6_2atmpS2408;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS1011, _M0L1iS1010, _M0L6_2atmpS2406);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS2412 = _M0MPC15array5Array2atGfE(_M0L1gS1001, _M0L1iS1010);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS2414 = _M0MPC15array5Array2atGfE(_M0L1hS1011, _M0L1iS1010);
      _M0L6_2atmpS2413 = _M0L2dtS1007 * _M0L6_2atmpS2414;
      _M0L6_2atmpS2411 = _M0L6_2atmpS2412 + _M0L6_2atmpS2413;
      _M0L6_2atmpS2410 = _M0L8decay__dS1006 * _M0L6_2atmpS2411;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS1001, _M0L1iS1010, _M0L6_2atmpS2410);
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS2416 = _M0MPC15array5Array2atGfE(_M0L1hS1011, _M0L1iS1010);
      _M0L6_2atmpS2415 = _M0L8decay__rS1008 * _M0L6_2atmpS2416;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS1011, _M0L1iS1010, _M0L6_2atmpS2415);
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0MPC15array5Array3setGfE(_M0L6targetS1012, _M0L1iS1010, 0x0p+0f);
      _M0L6_2atmpS2417 = _M0L1iS1010 + 1;
      _M0L1iS1010 = _M0L6_2atmpS2417;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors3new(
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS996,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L9nmda__recS997,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS998,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS999
) {
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS2405;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L6_2atmpS2404;
  struct _M0TP26RiantR8snn__mbt9Receptors* _block_3244;
  #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  moonbit_incref_cycle_free(_M0L4ampaS996);
  moonbit_incref_cycle_free(_M0L9nmda__recS997);
  moonbit_incref_cycle_free(_M0L5gabaaS998);
  moonbit_incref_cycle_free(_M0L5gababS999);
  _M0L6_2atmpS2405
  = (struct _M0TP26RiantR8snn__mbt8Receptor**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS2405[0] = _M0L4ampaS996;
  _M0L6_2atmpS2405[1] = _M0L9nmda__recS997;
  _M0L6_2atmpS2405[2] = _M0L5gabaaS998;
  _M0L6_2atmpS2405[3] = _M0L5gababS999;
  _M0L6_2atmpS2404
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE));
  Moonbit_object_header(_M0L6_2atmpS2404)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 103, 0);
  _M0L6_2atmpS2404->$0 = _M0L6_2atmpS2405;
  _M0L6_2atmpS2404->$1 = 4;
  _block_3244
  = (struct _M0TP26RiantR8snn__mbt9Receptors*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9Receptors));
  Moonbit_object_header(_block_3244)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 106, 0);
  _block_3244->$0 = _M0L6_2atmpS2404;
  return _block_3244;
}

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float _M0L1vS993,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L4nmdaS992
) {
  float _M0L1kS2403;
  float _M0L3argS991;
  float _M0L8exp__argS994;
  float _M0L2mgS2401;
  float _M0L1bS2402;
  float _M0L6_2atmpS2400;
  float _M0L6_2atmpS2399;
  float _M0L5denomS995;
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1kS2403 = _M0L4nmdaS992->$1;
  _M0L3argS991 = _M0L1kS2403 * _M0L1vS993;
  if (_M0L3argS991 < -0x1.5cp+6f) {
    _M0L8exp__argS994 = 0x0p+0f;
  } else if (_M0L3argS991 > 0x1.6p+6f) {
    _M0L8exp__argS994 = 0x1.2ced32a16a1b1p+126f;
  } else {
    #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    _M0L8exp__argS994 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS991);
  }
  _M0L2mgS2401 = _M0L4nmdaS992->$2;
  _M0L1bS2402 = _M0L4nmdaS992->$0;
  _M0L6_2atmpS2400 = _M0L2mgS2401 / _M0L1bS2402;
  _M0L6_2atmpS2399 = _M0L6_2atmpS2400 * _M0L8exp__argS994;
  _M0L5denomS995 = 0x1p+0f + _M0L6_2atmpS2399;
  return 0x1p+0f / _M0L5denomS995;
}

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
) {
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _block_3245;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _block_3245
  = (struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency));
  Moonbit_object_header(_block_3245)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3245->$0 = 0x1.ae147ae147ae1p+1f;
  _block_3245->$1 = -0x1.3b645a1cac083p-4f;
  _block_3245->$2 = 0x1p+0f;
  return _block_3245;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float _M0L6e__revS987,
  float _M0L6tau__rS988,
  float _M0L6tau__dS989,
  float _M0L2g0S990
) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS986;
  float _M0L11_2afield__0S2390;
  float _M0L11_2afield__1S2391;
  float _M0L11_2afield__2S2392;
  float _M0L11_2afield__3S2393;
  float _M0L11_2afield__4S2394;
  float _M0L11_2afield__5S2395;
  float _M0L11_2afield__6S2396;
  float _M0L11_2afield__7S2397;
  moonbit_string_t _M0L11_2afield__9S2398;
  int32_t _M0L6_2acntS3183;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_3246;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1rS986
  = _M0MP26RiantR8snn__mbt8Receptor6simple(_M0L6e__revS987, _M0L6tau__rS988, _M0L6tau__dS989, _M0L2g0S990, (moonbit_string_t)moonbit_string_literal_9.data);
  _M0L11_2afield__0S2390 = _M0L1rS986->$0;
  _M0L11_2afield__1S2391 = _M0L1rS986->$1;
  _M0L11_2afield__2S2392 = _M0L1rS986->$2;
  _M0L11_2afield__3S2393 = _M0L1rS986->$3;
  _M0L11_2afield__4S2394 = _M0L1rS986->$4;
  _M0L11_2afield__5S2395 = _M0L1rS986->$5;
  _M0L11_2afield__6S2396 = _M0L1rS986->$6;
  _M0L11_2afield__7S2397 = _M0L1rS986->$7;
  _M0L11_2afield__9S2398 = _M0L1rS986->$9;
  _M0L6_2acntS3183 = Moonbit_rc_count(Moonbit_object_header(_M0L1rS986));
  if (_M0L6_2acntS3183 > 1) {
    int32_t _M0L11_2anew__cntS3184 = _M0L6_2acntS3183 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS986), _M0L11_2anew__cntS3184);
    moonbit_incref_cycle_free(_M0L11_2afield__9S2398);
  } else if (_M0L6_2acntS3183 == 1) {
    #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    moonbit_free(_M0L1rS986);
  }
  _block_3246
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_3246)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 109, 0);
  _block_3246->$0 = _M0L11_2afield__0S2390;
  _block_3246->$1 = _M0L11_2afield__1S2391;
  _block_3246->$2 = _M0L11_2afield__2S2392;
  _block_3246->$3 = _M0L11_2afield__3S2393;
  _block_3246->$4 = _M0L11_2afield__4S2394;
  _block_3246->$5 = _M0L11_2afield__5S2395;
  _block_3246->$6 = _M0L11_2afield__6S2396;
  _block_3246->$7 = _M0L11_2afield__7S2397;
  _block_3246->$8 = 1;
  _block_3246->$9 = _M0L11_2afield__9S2398;
  return _block_3246;
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float _M0L6e__revS984,
  float _M0L6tau__rS978,
  float _M0L6tau__dS977,
  float _M0L2g0S983,
  moonbit_string_t _M0L6targetS985
) {
  float _M0L6_2atmpS2388;
  float _M0L6_2atmpS2389;
  float _M0L5alphaS976;
  float _M0L11tau__r__invS979;
  float _M0L11tau__d__invS980;
  float _M0L4normS981;
  float _M0L4gsynS982;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_3247;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS2388 = _M0L6tau__dS977 - _M0L6tau__rS978;
  _M0L6_2atmpS2389 = _M0L6tau__dS977 * _M0L6tau__rS978;
  _M0L5alphaS976 = _M0L6_2atmpS2388 / _M0L6_2atmpS2389;
  if (_M0L6tau__rS978 > 0x0p+0f) {
    _M0L11tau__r__invS979 = 0x1p+0f / _M0L6tau__rS978;
  } else {
    _M0L11tau__r__invS979 = 0x0p+0f;
  }
  if (_M0L6tau__dS977 > 0x0p+0f) {
    _M0L11tau__d__invS980 = 0x1p+0f / _M0L6tau__dS977;
  } else {
    _M0L11tau__d__invS980 = 0x0p+0f;
  }
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L4normS981
  = _M0FP26RiantR8snn__mbt13norm__synapse(_M0L6tau__rS978, _M0L6tau__dS977);
  if (_M0L2g0S983 > 0x0p+0f) {
    _M0L4gsynS982 = _M0L2g0S983 * _M0L4normS981;
  } else {
    _M0L4gsynS982 = 0x0p+0f;
  }
  moonbit_incref_cycle_free(_M0L6targetS985);
  _block_3247
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_3247)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 109, 0);
  _block_3247->$0 = _M0L6e__revS984;
  _block_3247->$1 = _M0L6tau__rS978;
  _block_3247->$2 = _M0L6tau__dS977;
  _block_3247->$3 = _M0L2g0S983;
  _block_3247->$4 = _M0L4gsynS982;
  _block_3247->$5 = _M0L5alphaS976;
  _block_3247->$6 = _M0L11tau__r__invS979;
  _block_3247->$7 = _M0L11tau__d__invS980;
  _block_3247->$8 = 0;
  _block_3247->$9 = _M0L6targetS985;
  return _block_3247;
}

float _M0FP26RiantR8snn__mbt13norm__synapse(
  float _M0L6tau__rS974,
  float _M0L6tau__dS975
) {
  float _M0L6_2atmpS2386;
  float _M0L6_2atmpS2387;
  float _M0L6_2atmpS2383;
  float _M0L6_2atmpS2385;
  float _M0L6_2atmpS2384;
  float _M0L4t__pS973;
  float _M0L6_2atmpS2382;
  float _M0L6_2atmpS2381;
  float _M0L6_2atmpS2380;
  float _M0L6_2atmpS2376;
  float _M0L6_2atmpS2379;
  float _M0L6_2atmpS2378;
  float _M0L6_2atmpS2377;
  float _M0L6_2atmpS2375;
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS2386 = _M0L6tau__rS974 * _M0L6tau__dS975;
  _M0L6_2atmpS2387 = _M0L6tau__dS975 - _M0L6tau__rS974;
  _M0L6_2atmpS2383 = _M0L6_2atmpS2386 / _M0L6_2atmpS2387;
  _M0L6_2atmpS2385 = _M0L6tau__dS975 / _M0L6tau__rS974;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS2384 = _M0FP26RiantR8snn__mbt4logf(_M0L6_2atmpS2385);
  _M0L4t__pS973 = _M0L6_2atmpS2383 * _M0L6_2atmpS2384;
  _M0L6_2atmpS2382 = -_M0L4t__pS973;
  _M0L6_2atmpS2381 = _M0L6_2atmpS2382 / _M0L6tau__rS974;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS2380 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2381);
  _M0L6_2atmpS2376 = -_M0L6_2atmpS2380;
  _M0L6_2atmpS2379 = -_M0L4t__pS973;
  _M0L6_2atmpS2378 = _M0L6_2atmpS2379 / _M0L6tau__dS975;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS2377 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2378);
  _M0L6_2atmpS2375 = _M0L6_2atmpS2376 + _M0L6_2atmpS2377;
  return 0x1p+0f / _M0L6_2atmpS2375;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS967,
  int32_t _M0L4colsS968,
  float _M0L2muS969,
  float _M0L5sigmaS970,
  float _M0L1pS971,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS972
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS967, _M0L4colsS968, _M0L2muS969, _M0L5sigmaS970, _M0L1pS971, 0, _M0L3rngS972);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS881,
  int32_t _M0L4colsS885,
  float _M0L2muS891,
  float _M0L5sigmaS892,
  float _M0L1pS904,
  int32_t _M0L4ruleS898,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS894
) {
  float* _M0L6_2atmpS2374;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2373;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS880;
  int32_t _M0L7_2abindS882;
  int32_t _M0L1iS883;
  int32_t _M0L6_2atmpS2372;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS957;
  int32_t* _M0L6_2atmpS2371;
  struct _M0TPB5ArrayGiE* _M0L6colptrS958;
  float* _M0L6_2atmpS2370;
  struct _M0TPB5ArrayGfE* _M0L4valsS959;
  int32_t _M0L7_2abindS960;
  int32_t _M0L1iS961;
  int32_t _M0L6_2atmpS2369;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_3267;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2374 = moonbit_empty_float_array;
  _M0L6_2atmpS2373
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2373)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _M0L6_2atmpS2373->$0 = _M0L6_2atmpS2374;
  _M0L6_2atmpS2373->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS880
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS881, _M0L6_2atmpS2373);
  _M0L7_2abindS882 = 0;
  _M0L1iS883 = _M0L7_2abindS882;
  while (1) {
    if (_M0L1iS883 < _M0L4rowsS881) {
      struct _M0TPB5ArrayGfE* _M0L3rowS884;
      int32_t _M0L7_2abindS886;
      int32_t _M0L1jS887;
      int32_t _M0L6_2atmpS2325;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS884 = _M0MPC15array5Array4makeGfE(_M0L4colsS885, 0x0p+0f);
      _M0L7_2abindS886 = 0;
      _M0L1jS887 = _M0L7_2abindS886;
      while (1) {
        if (_M0L1jS887 < _M0L4colsS885) {
          double _M0L2z1S889;
          struct _M0TUddE* _M0L7_2abindS893;
          double _M0L5_2az1S895;
          float _M0L6_2atmpS2323;
          float _M0L6_2atmpS2322;
          float _M0L1wS890;
          int32_t _M0L6_2atmpS2324;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS893
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS894);
          _M0L5_2az1S895 = _M0L7_2abindS893->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS893);
          _M0L2z1S889 = _M0L5_2az1S895;
          goto join_888;
          goto joinlet_3250;
          join_888:;
          _M0L6_2atmpS2323 = (float)_M0L2z1S889;
          _M0L6_2atmpS2322 = _M0L5sigmaS892 * _M0L6_2atmpS2323;
          _M0L1wS890 = _M0L2muS891 + _M0L6_2atmpS2322;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS884, _M0L1jS887, _M0L1wS890);
          joinlet_3250:;
          _M0L6_2atmpS2324 = _M0L1jS887 + 1;
          _M0L1jS887 = _M0L6_2atmpS2324;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS883, _M0L3rowS884);
      _M0L6_2atmpS2325 = _M0L1iS883 + 1;
      _M0L1iS883 = _M0L6_2atmpS2325;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS898) {
    case 0: {
      int32_t _M0L7_2abindS899 = 0;
      int32_t _M0L1iS900 = _M0L7_2abindS899;
      while (1) {
        if (_M0L1iS900 < _M0L4rowsS881) {
          int32_t _M0L7_2abindS901 = 0;
          int32_t _M0L1jS902 = _M0L7_2abindS901;
          int32_t _M0L6_2atmpS2328;
          while (1) {
            if (_M0L1jS902 < _M0L4colsS885) {
              float _M0L1uS903;
              int32_t _M0L6_2atmpS2327;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS903 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS894);
              if (_M0L1uS903 >= _M0L1pS904) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2326;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2326
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS900);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2326, _M0L1jS902, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2326);
              }
              _M0L6_2atmpS2327 = _M0L1jS902 + 1;
              _M0L1jS902 = _M0L6_2atmpS2327;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2328 = _M0L1iS900 + 1;
          _M0L1iS900 = _M0L6_2atmpS2328;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2346 = (float)_M0L4rowsS881;
      float _M0L6_2atmpS2345 = _M0L6_2atmpS2346 * _M0L1pS904;
      int32_t _M0L7n__keepS907;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS907 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2345);
      if (_M0L7n__keepS907 > 0 && _M0L7n__keepS907 <= _M0L4rowsS881) {
        int32_t _M0L7_2abindS908 = 0;
        int32_t _M0L1jS909 = _M0L7_2abindS908;
        while (1) {
          if (_M0L1jS909 < _M0L4colsS885) {
            int32_t* _M0L6_2atmpS2340 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS910 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS911;
            int32_t _M0L1kS912;
            int32_t _M0L7n__dropS914;
            int32_t _M0L7_2abindS915;
            int32_t _M0L1kS916;
            int32_t _M0L7_2abindS922;
            int32_t _M0L1kS923;
            int32_t _M0L6_2atmpS2341;
            Moonbit_object_header(_M0L8pre__idxS910)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 115, 0);
            _M0L8pre__idxS910->$0 = _M0L6_2atmpS2340;
            _M0L8pre__idxS910->$1 = 0;
            _M0L7_2abindS911 = 0;
            _M0L1kS912 = _M0L7_2abindS911;
            while (1) {
              if (_M0L1kS912 < _M0L4rowsS881) {
                int32_t _M0L6_2atmpS2329;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS910, _M0L1kS912);
                _M0L6_2atmpS2329 = _M0L1kS912 + 1;
                _M0L1kS912 = _M0L6_2atmpS2329;
                continue;
              }
              break;
            }
            _M0L7n__dropS914 = _M0L4rowsS881 - _M0L7n__keepS907;
            _M0L7_2abindS915 = 0;
            _M0L1kS916 = _M0L7_2abindS915;
            while (1) {
              if (_M0L1kS916 < _M0L7n__dropS914) {
                float _M0L1uS917;
                float _M0L6_2atmpS2333;
                float _M0L6_2atmpS2335;
                float _M0L6_2atmpS2334;
                float _M0L6_2atmpS2332;
                int32_t _M0L6_2atmpS2331;
                int32_t _M0L6r__idxS918;
                int32_t _M0L10r__clampedS919;
                int32_t _M0L3tmpS920;
                int32_t _M0L6_2atmpS2330;
                int32_t _M0L6_2atmpS2336;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS917 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS894);
                _M0L6_2atmpS2333 = (float)_M0L4rowsS881;
                _M0L6_2atmpS2335 = (float)_M0L1kS916;
                _M0L6_2atmpS2334 = _M0L6_2atmpS2335 * _M0L1uS917;
                _M0L6_2atmpS2332 = _M0L6_2atmpS2333 - _M0L6_2atmpS2334;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2331
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2332);
                _M0L6r__idxS918 = _M0L1kS916 + _M0L6_2atmpS2331;
                if (_M0L6r__idxS918 >= _M0L4rowsS881) {
                  _M0L10r__clampedS919 = _M0L4rowsS881 - 1;
                } else {
                  _M0L10r__clampedS919 = _M0L6r__idxS918;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS920
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS910, _M0L1kS916);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2330
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS910, _M0L10r__clampedS919);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS910, _M0L1kS916, _M0L6_2atmpS2330);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS910, _M0L10r__clampedS919, _M0L3tmpS920);
                _M0L6_2atmpS2336 = _M0L1kS916 + 1;
                _M0L1kS916 = _M0L6_2atmpS2336;
                continue;
              }
              break;
            }
            _M0L7_2abindS922 = 0;
            _M0L1kS923 = _M0L7_2abindS922;
            while (1) {
              if (_M0L1kS923 < _M0L7n__dropS914) {
                int32_t _M0L6_2atmpS2338;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2337;
                int32_t _M0L6_2atmpS2339;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2338
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS910, _M0L1kS923);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2337
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L6_2atmpS2338);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2337, _M0L1jS909, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2337);
                _M0L6_2atmpS2339 = _M0L1kS923 + 1;
                _M0L1kS923 = _M0L6_2atmpS2339;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS910);
              }
              break;
            }
            _M0L6_2atmpS2341 = _M0L1jS909 + 1;
            _M0L1jS909 = _M0L6_2atmpS2341;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS907 == 0) {
        int32_t _M0L7_2abindS926 = 0;
        int32_t _M0L1iS927 = _M0L7_2abindS926;
        while (1) {
          if (_M0L1iS927 < _M0L4rowsS881) {
            int32_t _M0L7_2abindS928 = 0;
            int32_t _M0L1jS929 = _M0L7_2abindS928;
            int32_t _M0L6_2atmpS2344;
            while (1) {
              if (_M0L1jS929 < _M0L4colsS885) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2342;
                int32_t _M0L6_2atmpS2343;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2342
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS927);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2342, _M0L1jS929, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2342);
                _M0L6_2atmpS2343 = _M0L1jS929 + 1;
                _M0L1jS929 = _M0L6_2atmpS2343;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2344 = _M0L1iS927 + 1;
            _M0L1iS927 = _M0L6_2atmpS2344;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2364 = (float)_M0L4colsS885;
      float _M0L6_2atmpS2363 = _M0L6_2atmpS2364 * _M0L1pS904;
      int32_t _M0L7n__keepS932;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS932 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2363);
      if (_M0L7n__keepS932 > 0 && _M0L7n__keepS932 <= _M0L4colsS885) {
        int32_t _M0L7_2abindS933 = 0;
        int32_t _M0L1iS934 = _M0L7_2abindS933;
        while (1) {
          if (_M0L1iS934 < _M0L4rowsS881) {
            int32_t* _M0L6_2atmpS2358 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS935 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS936;
            int32_t _M0L1kS937;
            int32_t _M0L7n__dropS939;
            int32_t _M0L7_2abindS940;
            int32_t _M0L1kS941;
            int32_t _M0L7_2abindS947;
            int32_t _M0L1kS948;
            int32_t _M0L6_2atmpS2359;
            Moonbit_object_header(_M0L9post__idxS935)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 115, 0);
            _M0L9post__idxS935->$0 = _M0L6_2atmpS2358;
            _M0L9post__idxS935->$1 = 0;
            _M0L7_2abindS936 = 0;
            _M0L1kS937 = _M0L7_2abindS936;
            while (1) {
              if (_M0L1kS937 < _M0L4colsS885) {
                int32_t _M0L6_2atmpS2347;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS935, _M0L1kS937);
                _M0L6_2atmpS2347 = _M0L1kS937 + 1;
                _M0L1kS937 = _M0L6_2atmpS2347;
                continue;
              }
              break;
            }
            _M0L7n__dropS939 = _M0L4colsS885 - _M0L7n__keepS932;
            _M0L7_2abindS940 = 0;
            _M0L1kS941 = _M0L7_2abindS940;
            while (1) {
              if (_M0L1kS941 < _M0L7n__dropS939) {
                float _M0L1uS942;
                float _M0L6_2atmpS2351;
                float _M0L6_2atmpS2353;
                float _M0L6_2atmpS2352;
                float _M0L6_2atmpS2350;
                int32_t _M0L6_2atmpS2349;
                int32_t _M0L6r__idxS943;
                int32_t _M0L10r__clampedS944;
                int32_t _M0L3tmpS945;
                int32_t _M0L6_2atmpS2348;
                int32_t _M0L6_2atmpS2354;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS942 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS894);
                _M0L6_2atmpS2351 = (float)_M0L4colsS885;
                _M0L6_2atmpS2353 = (float)_M0L1kS941;
                _M0L6_2atmpS2352 = _M0L6_2atmpS2353 * _M0L1uS942;
                _M0L6_2atmpS2350 = _M0L6_2atmpS2351 - _M0L6_2atmpS2352;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2349
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2350);
                _M0L6r__idxS943 = _M0L1kS941 + _M0L6_2atmpS2349;
                if (_M0L6r__idxS943 >= _M0L4colsS885) {
                  _M0L10r__clampedS944 = _M0L4colsS885 - 1;
                } else {
                  _M0L10r__clampedS944 = _M0L6r__idxS943;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS945
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS935, _M0L1kS941);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2348
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS935, _M0L10r__clampedS944);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS935, _M0L1kS941, _M0L6_2atmpS2348);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS935, _M0L10r__clampedS944, _M0L3tmpS945);
                _M0L6_2atmpS2354 = _M0L1kS941 + 1;
                _M0L1kS941 = _M0L6_2atmpS2354;
                continue;
              }
              break;
            }
            _M0L7_2abindS947 = 0;
            _M0L1kS948 = _M0L7_2abindS947;
            while (1) {
              if (_M0L1kS948 < _M0L7n__dropS939) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2355;
                int32_t _M0L6_2atmpS2356;
                int32_t _M0L6_2atmpS2357;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2355
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS934);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2356
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS935, _M0L1kS948);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2355, _M0L6_2atmpS2356, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2355);
                _M0L6_2atmpS2357 = _M0L1kS948 + 1;
                _M0L1kS948 = _M0L6_2atmpS2357;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS935);
              }
              break;
            }
            _M0L6_2atmpS2359 = _M0L1iS934 + 1;
            _M0L1iS934 = _M0L6_2atmpS2359;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS932 == 0) {
        int32_t _M0L7_2abindS951 = 0;
        int32_t _M0L1iS952 = _M0L7_2abindS951;
        while (1) {
          if (_M0L1iS952 < _M0L4rowsS881) {
            int32_t _M0L7_2abindS953 = 0;
            int32_t _M0L1jS954 = _M0L7_2abindS953;
            int32_t _M0L6_2atmpS2362;
            while (1) {
              if (_M0L1jS954 < _M0L4colsS885) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2360;
                int32_t _M0L6_2atmpS2361;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2360
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS952);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2360, _M0L1jS954, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2360);
                _M0L6_2atmpS2361 = _M0L1jS954 + 1;
                _M0L1jS954 = _M0L6_2atmpS2361;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2362 = _M0L1iS952 + 1;
            _M0L1iS952 = _M0L6_2atmpS2362;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2372 = _M0L4rowsS881 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS957 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2372, 0);
  _M0L6_2atmpS2371 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS958
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS958)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 115, 0);
  _M0L6colptrS958->$0 = _M0L6_2atmpS2371;
  _M0L6colptrS958->$1 = 0;
  _M0L6_2atmpS2370 = moonbit_empty_float_array;
  _M0L4valsS959
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS959)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _M0L4valsS959->$0 = _M0L6_2atmpS2370;
  _M0L4valsS959->$1 = 0;
  _M0L7_2abindS960 = 0;
  _M0L1iS961 = _M0L7_2abindS960;
  while (1) {
    if (_M0L1iS961 < _M0L4rowsS881) {
      int32_t _M0L6_2atmpS2365;
      int32_t _M0L7_2abindS962;
      int32_t _M0L1jS963;
      int32_t _M0L6_2atmpS2368;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2365 = _M0MPC15array5Array6lengthGfE(_M0L4valsS959);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS957, _M0L1iS961, _M0L6_2atmpS2365);
      _M0L7_2abindS962 = 0;
      _M0L1jS963 = _M0L7_2abindS962;
      while (1) {
        if (_M0L1jS963 < _M0L4colsS885) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2366;
          float _M0L1vS964;
          int32_t _M0L6_2atmpS2367;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2366
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS880, _M0L1iS961);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS964
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2366, _M0L1jS963);
          moonbit_decref_cycle_free(_M0L6_2atmpS2366);
          if (_M0L1vS964 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS958, _M0L1jS963);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS959, _M0L1vS964);
          }
          _M0L6_2atmpS2367 = _M0L1jS963 + 1;
          _M0L1jS963 = _M0L6_2atmpS2367;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2368 = _M0L1iS961 + 1;
      _M0L1iS961 = _M0L6_2atmpS2368;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS880);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2369 = _M0MPC15array5Array6lengthGfE(_M0L4valsS959);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS957, _M0L4rowsS881, _M0L6_2atmpS2369);
  _block_3267
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_3267)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 118, 0);
  _block_3267->$0 = _M0L4rowsS881;
  _block_3267->$1 = _M0L4colsS885;
  _block_3267->$2 = _M0L6rowptrS957;
  _block_3267->$3 = _M0L6colptrS958;
  _block_3267->$4 = _M0L4valsS959;
  return _block_3267;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS879
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2321;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2321 = _M0L1mS879->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2321);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS877
) {
  struct _M0TUmmmmE* _M0L1sS876;
  uint64_t _M0L6_2atmpS2320;
  struct _M0TUmmmmE* _M0L1tS878;
  uint64_t _M0L6_2atmpS2316;
  uint64_t _M0L6_2atmpS2317;
  uint64_t _M0L6_2atmpS2318;
  uint64_t _M0L6_2atmpS2319;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_3268;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS876 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS877);
  _M0L6_2atmpS2320 = _M0L1sS876->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS878 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2320);
  _M0L6_2atmpS2316 = _M0L1sS876->$0;
  _M0L6_2atmpS2317 = _M0L1sS876->$1;
  _M0L6_2atmpS2318 = _M0L1sS876->$2;
  moonbit_decref_cycle_free(_M0L1sS876);
  _M0L6_2atmpS2319 = _M0L1tS878->$0;
  moonbit_decref_cycle_free(_M0L1tS878);
  _block_3268
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_3268)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3268->$0 = _M0L6_2atmpS2316;
  _block_3268->$1 = _M0L6_2atmpS2317;
  _block_3268->$2 = _M0L6_2atmpS2318;
  _block_3268->$3 = _M0L6_2atmpS2319;
  return _block_3268;
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
  struct _M0TUmmmmE* _block_3269;
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
  _block_3269 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_3269)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3269->$0 = _M0L2z1S869;
  _block_3269->$1 = _M0L2z2S871;
  _block_3269->$2 = _M0L2z3S873;
  _block_3269->$3 = _M0L2z4S875;
  return _block_3269;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS865) {
  uint64_t _M0L6_2atmpS2315;
  uint64_t _M0L6_2atmpS2314;
  uint64_t _M0L1zS864;
  uint64_t _M0L6_2atmpS2313;
  uint64_t _M0L6_2atmpS2312;
  uint64_t _M0L1zS866;
  uint64_t _M0L6_2atmpS2311;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2315 = _M0L1zS865 >> 30;
  _M0L6_2atmpS2314 = _M0L1zS865 ^ _M0L6_2atmpS2315;
  _M0L1zS864 = _M0L6_2atmpS2314 * 13787848793156543929ull;
  _M0L6_2atmpS2313 = _M0L1zS864 >> 27;
  _M0L6_2atmpS2312 = _M0L1zS864 ^ _M0L6_2atmpS2313;
  _M0L1zS866 = _M0L6_2atmpS2312 * 10723151780598845931ull;
  _M0L6_2atmpS2311 = _M0L1zS866 >> 31;
  return _M0L1zS866 ^ _M0L6_2atmpS2311;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS859
) {
  double _M0L2u1S858;
  double _M0L8u1__safeS860;
  double _M0L2u2S861;
  double _M0L6_2atmpS2310;
  double _M0L6_2atmpS2309;
  double _M0L1rS862;
  double _M0L5thetaS863;
  double _M0L6_2atmpS2308;
  double _M0L6_2atmpS2305;
  double _M0L6_2atmpS2307;
  double _M0L6_2atmpS2306;
  struct _M0TUddE* _block_3270;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S858 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS859);
  if (_M0L2u1S858 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS860 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS860 = _M0L2u1S858;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S861 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS859);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2310 = _M0FPC14math2ln(_M0L8u1__safeS860);
  _M0L6_2atmpS2309 = -0x1p+1 * _M0L6_2atmpS2310;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS862 = sqrt(_M0L6_2atmpS2309);
  _M0L5thetaS863 = 0x1.921fb54442d18p+2 * _M0L2u2S861;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2308 = _M0FPC14math3cos(_M0L5thetaS863);
  _M0L6_2atmpS2305 = _M0L1rS862 * _M0L6_2atmpS2308;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2307 = _M0FPC14math3sin(_M0L5thetaS863);
  _M0L6_2atmpS2306 = _M0L1rS862 * _M0L6_2atmpS2307;
  _block_3270 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_3270)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3270->$0 = _M0L6_2atmpS2305;
  _block_3270->$1 = _M0L6_2atmpS2306;
  return _block_3270;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS856
) {
  uint64_t _M0L1uS855;
  uint64_t _M0L4bitsS857;
  double _M0L6_2atmpS2304;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS855 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS856);
  _M0L4bitsS857 = _M0L1uS855 >> 11;
  _M0L6_2atmpS2304 = (double)_M0L4bitsS857;
  return _M0L6_2atmpS2304 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS853
) {
  uint32_t _M0L1uS852;
  uint32_t _M0L4bitsS854;
  double _M0L6_2atmpS2303;
  double _M0L6_2atmpS2302;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS852 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS853);
  _M0L4bitsS854 = _M0L1uS852 >> 8;
  _M0L6_2atmpS2303 = (double)_M0L4bitsS854;
  _M0L6_2atmpS2302 = _M0L6_2atmpS2303 * 0x1p-24;
  return (float)_M0L6_2atmpS2302;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS851
) {
  uint64_t _M0L1uS850;
  uint64_t _M0L6_2atmpS2301;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS850 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS851);
  _M0L6_2atmpS2301 = _M0L1uS850 >> 32;
  return (uint32_t)_M0L6_2atmpS2301;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS843
) {
  uint64_t _M0L2s0S842;
  uint64_t _M0L2s1S844;
  uint64_t _M0L2s2S845;
  uint64_t _M0L2s3S846;
  uint64_t _M0L3tmpS847;
  uint64_t _M0L6_2atmpS2300;
  uint64_t _M0L3resS848;
  uint64_t _M0L1tS849;
  uint64_t _M0L6_2atmpS2290;
  uint64_t _M0L6_2atmpS2291;
  uint64_t _M0L2s2S2293;
  uint64_t _M0L6_2atmpS2292;
  uint64_t _M0L2s3S2295;
  uint64_t _M0L6_2atmpS2294;
  uint64_t _M0L2s2S2297;
  uint64_t _M0L6_2atmpS2296;
  uint64_t _M0L2s3S2299;
  uint64_t _M0L6_2atmpS2298;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S842 = _M0L1rS843->$0;
  _M0L2s1S844 = _M0L1rS843->$1;
  _M0L2s2S845 = _M0L1rS843->$2;
  _M0L2s3S846 = _M0L1rS843->$3;
  _M0L3tmpS847 = _M0L2s0S842 + _M0L2s3S846;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2300 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS847, 23);
  _M0L3resS848 = _M0L6_2atmpS2300 + _M0L2s0S842;
  _M0L1tS849 = _M0L2s1S844 << 17;
  _M0L6_2atmpS2290 = _M0L2s2S845 ^ _M0L2s0S842;
  _M0L1rS843->$2 = _M0L6_2atmpS2290;
  _M0L6_2atmpS2291 = _M0L2s3S846 ^ _M0L2s1S844;
  _M0L1rS843->$3 = _M0L6_2atmpS2291;
  _M0L2s2S2293 = _M0L1rS843->$2;
  _M0L6_2atmpS2292 = _M0L2s1S844 ^ _M0L2s2S2293;
  _M0L1rS843->$1 = _M0L6_2atmpS2292;
  _M0L2s3S2295 = _M0L1rS843->$3;
  _M0L6_2atmpS2294 = _M0L2s0S842 ^ _M0L2s3S2295;
  _M0L1rS843->$0 = _M0L6_2atmpS2294;
  _M0L2s2S2297 = _M0L1rS843->$2;
  _M0L6_2atmpS2296 = _M0L2s2S2297 ^ _M0L1tS849;
  _M0L1rS843->$2 = _M0L6_2atmpS2296;
  _M0L2s3S2299 = _M0L1rS843->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2298 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2299, 45);
  _M0L1rS843->$3 = _M0L6_2atmpS2298;
  return _M0L3resS848;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS840, int32_t _M0L1kS841) {
  uint64_t _M0L6_2atmpS2287;
  int32_t _M0L6_2atmpS2289;
  uint64_t _M0L6_2atmpS2288;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2287 = _M0L1xS840 << (_M0L1kS841 & 63);
  _M0L6_2atmpS2289 = 64 - _M0L1kS841;
  _M0L6_2atmpS2288 = _M0L1xS840 >> (_M0L6_2atmpS2289 & 63);
  return _M0L6_2atmpS2287 | _M0L6_2atmpS2288;
}

double _M0FPC14math2ln(double _M0L1xS826) {
  struct _M0TUdiE* _M0L7_2abindS827;
  double _M0L5_2af1S828;
  int32_t _M0L5_2akiS829;
  double _M0L1fS831;
  double _M0L1kS832;
  double _M0L6_2atmpS2280;
  double _M0L1sS833;
  double _M0L2s2S834;
  double _M0L2s4S835;
  double _M0L6_2atmpS2279;
  double _M0L6_2atmpS2278;
  double _M0L6_2atmpS2277;
  double _M0L6_2atmpS2276;
  double _M0L6_2atmpS2275;
  double _M0L6_2atmpS2274;
  double _M0L2t1S836;
  double _M0L6_2atmpS2273;
  double _M0L6_2atmpS2272;
  double _M0L6_2atmpS2271;
  double _M0L6_2atmpS2270;
  double _M0L2t2S837;
  double _M0L1rS838;
  double _M0L6_2atmpS2269;
  double _M0L4hfsqS839;
  double _M0L6_2atmpS2262;
  double _M0L6_2atmpS2268;
  double _M0L6_2atmpS2266;
  double _M0L6_2atmpS2267;
  double _M0L6_2atmpS2265;
  double _M0L6_2atmpS2264;
  double _M0L6_2atmpS2263;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS826 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS826)
      || _M0MPC16double6Double7is__inf(_M0L1xS826)
    ) {
      return _M0L1xS826;
    } else if (_M0L1xS826 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS827 = _M0FPC14math5frexp(_M0L1xS826);
  _M0L5_2af1S828 = _M0L7_2abindS827->$0;
  _M0L5_2akiS829 = _M0L7_2abindS827->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS827);
  if (_M0L5_2af1S828 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2284 = _M0L5_2af1S828 * 0x1p+1;
    double _M0L6_2atmpS2281 = _M0L6_2atmpS2284 - 0x1p+0;
    int32_t _M0L6_2atmpS2283 = _M0L5_2akiS829 - 1;
    double _M0L6_2atmpS2282 = (double)_M0L6_2atmpS2283;
    _M0L1fS831 = _M0L6_2atmpS2281;
    _M0L1kS832 = _M0L6_2atmpS2282;
    goto join_830;
  } else {
    double _M0L6_2atmpS2285 = _M0L5_2af1S828 - 0x1p+0;
    double _M0L6_2atmpS2286 = (double)_M0L5_2akiS829;
    _M0L1fS831 = _M0L6_2atmpS2285;
    _M0L1kS832 = _M0L6_2atmpS2286;
    goto join_830;
  }
  join_830:;
  _M0L6_2atmpS2280 = 0x1p+1 + _M0L1fS831;
  _M0L1sS833 = _M0L1fS831 / _M0L6_2atmpS2280;
  _M0L2s2S834 = _M0L1sS833 * _M0L1sS833;
  _M0L2s4S835 = _M0L2s2S834 * _M0L2s2S834;
  _M0L6_2atmpS2279 = _M0L2s4S835 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2278 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2279;
  _M0L6_2atmpS2277 = _M0L2s4S835 * _M0L6_2atmpS2278;
  _M0L6_2atmpS2276 = 0x1.2492494229359p-2 + _M0L6_2atmpS2277;
  _M0L6_2atmpS2275 = _M0L2s4S835 * _M0L6_2atmpS2276;
  _M0L6_2atmpS2274 = 0x1.5555555555593p-1 + _M0L6_2atmpS2275;
  _M0L2t1S836 = _M0L2s2S834 * _M0L6_2atmpS2274;
  _M0L6_2atmpS2273 = _M0L2s4S835 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2272 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2273;
  _M0L6_2atmpS2271 = _M0L2s4S835 * _M0L6_2atmpS2272;
  _M0L6_2atmpS2270 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2271;
  _M0L2t2S837 = _M0L2s4S835 * _M0L6_2atmpS2270;
  _M0L1rS838 = _M0L2t1S836 + _M0L2t2S837;
  _M0L6_2atmpS2269 = 0x1p-1 * _M0L1fS831;
  _M0L4hfsqS839 = _M0L6_2atmpS2269 * _M0L1fS831;
  _M0L6_2atmpS2262 = _M0L1kS832 * 0x1.62e42feep-1;
  _M0L6_2atmpS2268 = _M0L4hfsqS839 + _M0L1rS838;
  _M0L6_2atmpS2266 = _M0L1sS833 * _M0L6_2atmpS2268;
  _M0L6_2atmpS2267 = _M0L1kS832 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2265 = _M0L6_2atmpS2266 + _M0L6_2atmpS2267;
  _M0L6_2atmpS2264 = _M0L4hfsqS839 - _M0L6_2atmpS2265;
  _M0L6_2atmpS2263 = _M0L6_2atmpS2264 - _M0L1fS831;
  return _M0L6_2atmpS2262 - _M0L6_2atmpS2263;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS819) {
  struct _M0TUdiE* _M0L7_2abindS820;
  double _M0L10_2anorm__fS821;
  int32_t _M0L6_2aexpS822;
  uint64_t _M0L1uS823;
  uint64_t _M0L6_2atmpS2261;
  uint64_t _M0L6_2atmpS2260;
  int32_t _M0L6_2atmpS2259;
  int32_t _M0L6_2atmpS2258;
  int32_t _M0L3expS824;
  uint64_t _M0L6_2atmpS2257;
  uint64_t _M0L6_2atmpS2256;
  uint64_t _M0L6_2atmpS2255;
  double _M0L4fracS825;
  struct _M0TUdiE* _block_3273;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS819 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS819)
    || _M0MPC16double6Double7is__nan(_M0L1fS819)
  ) {
    struct _M0TUdiE* _block_3272 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3272)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3272->$0 = _M0L1fS819;
    _block_3272->$1 = 0;
    return _block_3272;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS820 = _M0FPC14math9normalize(_M0L1fS819);
  _M0L10_2anorm__fS821 = _M0L7_2abindS820->$0;
  _M0L6_2aexpS822 = _M0L7_2abindS820->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS820);
  _M0L1uS823 = *(int64_t*)&_M0L10_2anorm__fS821;
  _M0L6_2atmpS2261 = _M0L1uS823 >> 52;
  _M0L6_2atmpS2260 = _M0L6_2atmpS2261 & 2047ull;
  _M0L6_2atmpS2259 = (int32_t)_M0L6_2atmpS2260;
  _M0L6_2atmpS2258 = _M0L6_2aexpS822 + _M0L6_2atmpS2259;
  _M0L3expS824 = _M0L6_2atmpS2258 - 1022;
  _M0L6_2atmpS2257 = ~9218868437227405312ull;
  _M0L6_2atmpS2256 = _M0L1uS823 & _M0L6_2atmpS2257;
  _M0L6_2atmpS2255 = _M0L6_2atmpS2256 | 4602678819172646912ull;
  _M0L4fracS825 = *(double*)&_M0L6_2atmpS2255;
  _block_3273 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3273)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3273->$0 = _M0L4fracS825;
  _block_3273->$1 = _M0L3expS824;
  return _block_3273;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS818) {
  double _M0L6_2atmpS2252;
  struct _M0TUdiE* _block_3275;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2252 = fabs(_M0L1fS818);
  if (_M0L6_2atmpS2252 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2254 = (double)4503599627370496ll;
    double _M0L6_2atmpS2253 = _M0L1fS818 * _M0L6_2atmpS2254;
    struct _M0TUdiE* _block_3274 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3274)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3274->$0 = _M0L6_2atmpS2253;
    _block_3274->$1 = -52;
    return _block_3274;
  }
  _block_3275 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3275)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3275->$0 = _M0L1fS818;
  _block_3275->$1 = 0;
  return _block_3275;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS817) {
  double _M0L6_2atmpS2251;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2251 = (double)_M0L4selfS817;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2251);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS816) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS816 != _M0L4selfS816) {
    return 0;
  } else if (_M0L4selfS816 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS816 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS816;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS797,
  float _M0L4elemS799
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS796;
  int32_t _M0L1iS798;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS797);
  _M0L1iS798 = 0;
  while (1) {
    if (_M0L1iS798 < _M0L3lenS797) {
      float* _M0L3bufS2243 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS2244;
      _M0L3bufS2243[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS2244 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS2244;
      continue;
    }
    break;
  }
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS802,
  int32_t _M0L4elemS804
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS801;
  int32_t _M0L1iS803;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS801 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS802);
  _M0L1iS803 = 0;
  while (1) {
    if (_M0L1iS803 < _M0L3lenS802) {
      uint8_t* _M0L3bufS2245 = _M0L3arrS801->$0;
      int32_t _M0L6_2atmpS2246;
      _M0L3bufS2245[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS2246 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS2246;
      continue;
    }
    break;
  }
  return _M0L3arrS801;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS807,
  int32_t _M0L4elemS809
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS806;
  int32_t _M0L1iS808;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS806 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS807);
  _M0L1iS808 = 0;
  while (1) {
    if (_M0L1iS808 < _M0L3lenS807) {
      int32_t* _M0L3bufS2247 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2248;
      _M0L3bufS2247[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2248 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2248;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS812,
  struct _M0TPB5ArrayGfE* _M0L4elemS814
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS811;
  int32_t _M0L1iS813;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS811
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS812);
  _M0L1iS813 = 0;
  while (1) {
    if (_M0L1iS813 < _M0L3lenS812) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2249 = _M0L3arrS811->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS3155 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2249[_M0L1iS813];
      int32_t _M0L6_2atmpS2250;
      moonbit_incref_cycle_free(_M0L4elemS814);
      if (_M0L6_2aoldS3155) {
        moonbit_decref_cycle_free(_M0L6_2aoldS3155);
      }
      _M0L3bufS2249[_M0L1iS813] = _M0L4elemS814;
      _M0L6_2atmpS2250 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS2250;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS814);
    }
    break;
  }
  return _M0L3arrS811;
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
    uint8_t* _M0L6_2atmpS2239;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2239 = _M0MPC15array5Array6bufferGbE(_M0L4selfS781);
    _M0L6_2atmpS2239[_M0L5indexS782] = _M0L5valueS783;
    moonbit_decref_cycle_free(_M0L6_2atmpS2239);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS785,
  int32_t _M0L5indexS786,
  float _M0L5valueS787
) {
  int32_t _M0L3lenS784;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L5indexS786 >= 0 && _M0L5indexS786 < _M0L3lenS784) {
    float* _M0L6_2atmpS2240;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2240 = _M0MPC15array5Array6bufferGfE(_M0L4selfS785);
    _M0L6_2atmpS2240[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2240);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS789,
  int32_t _M0L5indexS790,
  int32_t _M0L5valueS791
) {
  int32_t _M0L3lenS788;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS788 = _M0L4selfS789->$1;
  if (_M0L5indexS790 >= 0 && _M0L5indexS790 < _M0L3lenS788) {
    int32_t* _M0L6_2atmpS2241;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2241 = _M0MPC15array5Array6bufferGiE(_M0L4selfS789);
    _M0L6_2atmpS2241[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2241);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS793,
  int32_t _M0L5indexS794,
  struct _M0TPB5ArrayGfE* _M0L5valueS795
) {
  int32_t _M0L3lenS792;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS792 = _M0L4selfS793->$1;
  if (_M0L5indexS794 >= 0 && _M0L5indexS794 < _M0L3lenS792) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2242;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS3156;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2242
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS793);
    _M0L6_2aoldS3156
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2242[_M0L5indexS794];
    if (_M0L6_2aoldS3156) {
      moonbit_decref_cycle_free(_M0L6_2aoldS3156);
    }
    _M0L6_2atmpS2242[_M0L5indexS794] = _M0L5valueS795;
    moonbit_decref_cycle_free(_M0L6_2atmpS2242);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS795);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    uint8_t* _M0L6_2atmpS2233;
    int32_t _result_3280;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2233 = _M0MPC15array5Array6bufferGbE(_M0L4selfS763);
    _result_3280 = (int32_t)_M0L6_2atmpS2233[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS2233);
    return _result_3280;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    float* _M0L6_2atmpS2234;
    float _result_3281;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2234 = _M0MPC15array5Array6bufferGfE(_M0L4selfS766);
    _result_3281 = (float)_M0L6_2atmpS2234[_M0L5indexS767];
    moonbit_decref_cycle_free(_M0L6_2atmpS2234);
    return _result_3281;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    int32_t* _M0L6_2atmpS2235;
    int32_t _result_3282;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2235 = _M0MPC15array5Array6bufferGiE(_M0L4selfS769);
    _result_3282 = (int32_t)_M0L6_2atmpS2235[_M0L5indexS770];
    moonbit_decref_cycle_free(_M0L6_2atmpS2235);
    return _result_3282;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS2236;
    struct _M0TP26RiantR8snn__mbt8Receptor* _M0L6_2atmpS3157;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2236
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(_M0L4selfS772);
    _M0L6_2atmpS3157
    = (struct _M0TP26RiantR8snn__mbt8Receptor*)_M0L6_2atmpS2236[
        _M0L5indexS773
      ];
    if (_M0L6_2atmpS3157) {
      moonbit_incref_cycle_free(_M0L6_2atmpS3157);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2236);
    return _M0L6_2atmpS3157;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS775,
  int32_t _M0L5indexS776
) {
  int32_t _M0L3lenS774;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    moonbit_string_t* _M0L6_2atmpS2237;
    moonbit_string_t _M0L6_2atmpS3158;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2237 = _M0MPC15array5Array6bufferGsE(_M0L4selfS775);
    _M0L6_2atmpS3158 = (moonbit_string_t)_M0L6_2atmpS2237[_M0L5indexS776];
    moonbit_incref_cycle_free(_M0L6_2atmpS3158);
    moonbit_decref_cycle_free(_M0L6_2atmpS2237);
    return _M0L6_2atmpS3158;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS778,
  int32_t _M0L5indexS779
) {
  int32_t _M0L3lenS777;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2238;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS3159;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2238
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS778);
    _M0L6_2atmpS3159
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2238[_M0L5indexS779];
    if (_M0L6_2atmpS3159) {
      moonbit_incref_cycle_free(_M0L6_2atmpS3159);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2238);
    return _M0L6_2atmpS3159;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS761) {
  moonbit_string_t _M0L6_2atmpS2232;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2232 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS761);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2232);
  moonbit_decref_cycle_free(_M0L6_2atmpS2232);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS760) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS760);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS759) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS759 > _M0FPB18double__max__value
         || _M0L4selfS759 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS758) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS758 != _M0L4selfS758;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS743) {
  uint64_t _M0L4bitsS746;
  uint64_t _M0L6_2atmpS2231;
  uint64_t _M0L6_2atmpS2230;
  int32_t _M0L8ieeeSignS747;
  uint64_t _M0L12ieeeMantissaS748;
  uint64_t _M0L6_2atmpS2229;
  uint64_t _M0L6_2atmpS2228;
  int32_t _M0L12ieeeExponentS749;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS750;
  struct _M0TPB17FloatingDecimal64* _M0L1vS751;
  moonbit_string_t _result_3284;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS743 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L3valS743 >= -0x1p+53 && _M0L3valS743 <= 0x1p+53) {
    if (_M0L3valS743 >= -0x1p+31 && _M0L3valS743 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS744;
      double _M0L6_2atmpS2217;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double7to__int(_M0L3valS743);
      _M0L6_2atmpS2217 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2217 == _M0L3valS743) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS744, 10);
      }
    } else {
      int64_t _M0L1iS745;
      double _M0L6_2atmpS2218;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double9to__int64(_M0L3valS743);
      _M0L6_2atmpS2218 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2218 == _M0L3valS743) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS745, 10);
      }
    }
  }
  _M0L4bitsS746 = *(int64_t*)&_M0L3valS743;
  _M0L6_2atmpS2231 = _M0L4bitsS746 >> 63;
  _M0L6_2atmpS2230 = _M0L6_2atmpS2231 & 1ull;
  _M0L8ieeeSignS747 = _M0L6_2atmpS2230 != 0ull;
  _M0L12ieeeMantissaS748 = _M0L4bitsS746 & 4503599627370495ull;
  _M0L6_2atmpS2229 = _M0L4bitsS746 >> 52;
  _M0L6_2atmpS2228 = _M0L6_2atmpS2229 & 2047ull;
  _M0L12ieeeExponentS749 = (int32_t)_M0L6_2atmpS2228;
  if (
    _M0L12ieeeExponentS749 == 2047
    || _M0L12ieeeExponentS749 == 0 && _M0L12ieeeMantissaS748 == 0ull
  ) {
    int32_t _M0L6_2atmpS2219 = _M0L12ieeeExponentS749 != 0;
    int32_t _M0L6_2atmpS2220 = _M0L12ieeeMantissaS748 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS747, _M0L6_2atmpS2219, _M0L6_2atmpS2220);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS750
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS748, _M0L12ieeeExponentS749);
  if (_M0L7_2abindS750 == 0) {
    uint32_t _M0L6_2atmpS2221;
    if (_M0L7_2abindS750) {
      moonbit_decref_cycle_free(_M0L7_2abindS750);
    }
    _M0L6_2atmpS2221 = *(uint32_t*)&_M0L12ieeeExponentS749;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS751 = _M0FPB3d2d(_M0L12ieeeMantissaS748, _M0L6_2atmpS2221);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS752 = _M0L7_2abindS750;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS753 = _M0L7_2aSomeS752;
    struct _M0TPB17FloatingDecimal64* _M0L1xS754 = _M0L4_2afS753;
    while (1) {
      uint64_t _M0L8mantissaS2227 = _M0L1xS754->$0;
      uint64_t _M0L1qS755 = _M0L8mantissaS2227 / 10ull;
      uint64_t _M0L8mantissaS2225 = _M0L1xS754->$0;
      uint64_t _M0L6_2atmpS2226 = 10ull * _M0L1qS755;
      uint64_t _M0L1rS756 = _M0L8mantissaS2225 - _M0L6_2atmpS2226;
      int32_t _M0L8exponentS2224;
      int32_t _M0L6_2atmpS2223;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2222;
      if (_M0L1rS756 != 0ull) {
        _M0L1vS751 = _M0L1xS754;
        break;
      }
      _M0L8exponentS2224 = _M0L1xS754->$1;
      moonbit_decref_cycle_free(_M0L1xS754);
      _M0L6_2atmpS2223 = _M0L8exponentS2224 + 1;
      _M0L6_2atmpS2222
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2222)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2222->$0 = _M0L1qS755;
      _M0L6_2atmpS2222->$1 = _M0L6_2atmpS2223;
      _M0L1xS754 = _M0L6_2atmpS2222;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3284 = _M0FPB9to__chars(_M0L1vS751, _M0L8ieeeSignS747);
  moonbit_decref_cycle_free(_M0L1vS751);
  return _result_3284;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS738,
  int32_t _M0L12ieeeExponentS740
) {
  uint64_t _M0L2m2S737;
  int32_t _M0L6_2atmpS2216;
  int32_t _M0L2e2S739;
  int32_t _M0L6_2atmpS2215;
  uint64_t _M0L6_2atmpS2214;
  uint64_t _M0L4maskS741;
  uint64_t _M0L8fractionS742;
  int32_t _M0L6_2atmpS2213;
  uint64_t _M0L6_2atmpS2212;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2211;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S737 = 4503599627370496ull | _M0L12ieeeMantissaS738;
  _M0L6_2atmpS2216 = _M0L12ieeeExponentS740 - 1023;
  _M0L2e2S739 = _M0L6_2atmpS2216 - 52;
  if (_M0L2e2S739 > 0) {
    return 0;
  }
  if (_M0L2e2S739 < -52) {
    return 0;
  }
  _M0L6_2atmpS2215 = -_M0L2e2S739;
  _M0L6_2atmpS2214 = 1ull << (_M0L6_2atmpS2215 & 63);
  _M0L4maskS741 = _M0L6_2atmpS2214 - 1ull;
  _M0L8fractionS742 = _M0L2m2S737 & _M0L4maskS741;
  if (_M0L8fractionS742 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2213 = -_M0L2e2S739;
  _M0L6_2atmpS2212 = _M0L2m2S737 >> (_M0L6_2atmpS2213 & 63);
  _M0L6_2atmpS2211
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2211)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2211->$0 = _M0L6_2atmpS2212;
  _M0L6_2atmpS2211->$1 = 0;
  return _M0L6_2atmpS2211;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS705,
  int32_t _M0L4signS703
) {
  moonbit_bytes_t _M0L6resultS701;
  int32_t _M0Lm5indexS702;
  uint64_t _M0L6outputS704;
  int32_t _M0L7olengthS706;
  int32_t _M0L8exponentS2210;
  int32_t _M0L6_2atmpS2209;
  int32_t _M0Lm3expS707;
  int32_t _M0L6_2atmpS2208;
  int32_t _M0L6_2atmpS2206;
  int32_t _M0L18scientificNotationS708;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS701 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS702 = 0;
  if (_M0L4signS703) {
    int32_t _M0L6_2atmpS2080 = _M0Lm5indexS702;
    int32_t _M0L6_2atmpS2081;
    if (
      _M0L6_2atmpS2080 < 0
      || _M0L6_2atmpS2080 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS2080] = 45;
    _M0L6_2atmpS2081 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS2081 + 1;
  }
  _M0L6outputS704 = _M0L1vS705->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS706 = _M0FPB17decimal__length17(_M0L6outputS704);
  _M0L8exponentS2210 = _M0L1vS705->$1;
  _M0L6_2atmpS2209 = _M0L8exponentS2210 + _M0L7olengthS706;
  _M0Lm3expS707 = _M0L6_2atmpS2209 - 1;
  _M0L6_2atmpS2208 = _M0Lm3expS707;
  if (_M0L6_2atmpS2208 >= -6) {
    int32_t _M0L6_2atmpS2207 = _M0Lm3expS707;
    _M0L6_2atmpS2206 = _M0L6_2atmpS2207 < 21;
  } else {
    _M0L6_2atmpS2206 = 0;
  }
  _M0L18scientificNotationS708 = !_M0L6_2atmpS2206;
  if (_M0L18scientificNotationS708) {
    int32_t _M0L7_2abindS709 = _M0L7olengthS706 - 1;
    uint64_t _M0L6outputS710;
    int32_t _M0L1iS711 = 0;
    uint64_t _M0L6outputS712 = _M0L6outputS704;
    int32_t _M0L6_2atmpS2082;
    int32_t _M0L6_2atmpS2086;
    int32_t _M0L6_2atmpS2085;
    int32_t _M0L6_2atmpS2084;
    int32_t _M0L6_2atmpS2083;
    int32_t _M0L6_2atmpS2090;
    int32_t _M0L6_2atmpS2091;
    int32_t _M0L6_2atmpS2092;
    int32_t _M0L6_2atmpS2093;
    int32_t _M0L6_2atmpS2094;
    int32_t _M0L6_2atmpS2100;
    int32_t _M0L6_2atmpS2133;
    moonbit_string_t _result_3286;
    while (1) {
      if (_M0L1iS711 < _M0L7_2abindS709) {
        uint64_t _M0L1cS713 = _M0L6outputS712 % 10ull;
        int32_t _M0L6_2atmpS2139 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2138 = _M0L6_2atmpS2139 + _M0L7olengthS706;
        int32_t _M0L6_2atmpS2134 = _M0L6_2atmpS2138 - _M0L1iS711;
        int32_t _M0L6_2atmpS2137 = (int32_t)_M0L1cS713;
        int32_t _M0L6_2atmpS2136 = 48 + _M0L6_2atmpS2137;
        int32_t _M0L6_2atmpS2135 = _M0L6_2atmpS2136 & 0xff;
        int32_t _M0L6_2atmpS2140;
        uint64_t _M0L6_2atmpS2141;
        if (
          _M0L6_2atmpS2134 < 0
          || _M0L6_2atmpS2134 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2134] = _M0L6_2atmpS2135;
        _M0L6_2atmpS2140 = _M0L1iS711 + 1;
        _M0L6_2atmpS2141 = _M0L6outputS712 / 10ull;
        _M0L1iS711 = _M0L6_2atmpS2140;
        _M0L6outputS712 = _M0L6_2atmpS2141;
        continue;
      } else {
        _M0L6outputS710 = _M0L6outputS712;
      }
      break;
    }
    _M0L6_2atmpS2082 = _M0Lm5indexS702;
    _M0L6_2atmpS2086 = (int32_t)_M0L6outputS710;
    _M0L6_2atmpS2085 = _M0L6_2atmpS2086 % 10;
    _M0L6_2atmpS2084 = 48 + _M0L6_2atmpS2085;
    _M0L6_2atmpS2083 = _M0L6_2atmpS2084 & 0xff;
    if (
      _M0L6_2atmpS2082 < 0
      || _M0L6_2atmpS2082 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS2082] = _M0L6_2atmpS2083;
    if (_M0L7olengthS706 > 1) {
      int32_t _M0L6_2atmpS2088 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2087 = _M0L6_2atmpS2088 + 1;
      if (
        _M0L6_2atmpS2087 < 0
        || _M0L6_2atmpS2087 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2087] = 46;
    } else {
      int32_t _M0L6_2atmpS2089 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2089 - 1;
    }
    _M0L6_2atmpS2090 = _M0Lm5indexS702;
    _M0L6_2atmpS2091 = _M0L7olengthS706 + 1;
    _M0Lm5indexS702 = _M0L6_2atmpS2090 + _M0L6_2atmpS2091;
    _M0L6_2atmpS2092 = _M0Lm5indexS702;
    if (
      _M0L6_2atmpS2092 < 0
      || _M0L6_2atmpS2092 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS2092] = 101;
    _M0L6_2atmpS2093 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS2093 + 1;
    _M0L6_2atmpS2094 = _M0Lm3expS707;
    if (_M0L6_2atmpS2094 < 0) {
      int32_t _M0L6_2atmpS2095 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2096;
      int32_t _M0L6_2atmpS2097;
      if (
        _M0L6_2atmpS2095 < 0
        || _M0L6_2atmpS2095 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2095] = 45;
      _M0L6_2atmpS2096 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2096 + 1;
      _M0L6_2atmpS2097 = _M0Lm3expS707;
      _M0Lm3expS707 = -_M0L6_2atmpS2097;
    } else {
      int32_t _M0L6_2atmpS2098 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2099;
      if (
        _M0L6_2atmpS2098 < 0
        || _M0L6_2atmpS2098 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2098] = 43;
      _M0L6_2atmpS2099 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2099 + 1;
    }
    _M0L6_2atmpS2100 = _M0Lm3expS707;
    if (_M0L6_2atmpS2100 >= 100) {
      int32_t _M0L6_2atmpS2116 = _M0Lm3expS707;
      int32_t _M0L1aS715 = _M0L6_2atmpS2116 / 100;
      int32_t _M0L6_2atmpS2115 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2114 = _M0L6_2atmpS2115 / 10;
      int32_t _M0L1bS716 = _M0L6_2atmpS2114 % 10;
      int32_t _M0L6_2atmpS2113 = _M0Lm3expS707;
      int32_t _M0L1cS717 = _M0L6_2atmpS2113 % 10;
      int32_t _M0L6_2atmpS2101 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2103 = 48 + _M0L1aS715;
      int32_t _M0L6_2atmpS2102 = _M0L6_2atmpS2103 & 0xff;
      int32_t _M0L6_2atmpS2107;
      int32_t _M0L6_2atmpS2104;
      int32_t _M0L6_2atmpS2106;
      int32_t _M0L6_2atmpS2105;
      int32_t _M0L6_2atmpS2111;
      int32_t _M0L6_2atmpS2108;
      int32_t _M0L6_2atmpS2110;
      int32_t _M0L6_2atmpS2109;
      int32_t _M0L6_2atmpS2112;
      if (
        _M0L6_2atmpS2101 < 0
        || _M0L6_2atmpS2101 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2101] = _M0L6_2atmpS2102;
      _M0L6_2atmpS2107 = _M0Lm5indexS702;
      _M0L6_2atmpS2104 = _M0L6_2atmpS2107 + 1;
      _M0L6_2atmpS2106 = 48 + _M0L1bS716;
      _M0L6_2atmpS2105 = _M0L6_2atmpS2106 & 0xff;
      if (
        _M0L6_2atmpS2104 < 0
        || _M0L6_2atmpS2104 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2104] = _M0L6_2atmpS2105;
      _M0L6_2atmpS2111 = _M0Lm5indexS702;
      _M0L6_2atmpS2108 = _M0L6_2atmpS2111 + 2;
      _M0L6_2atmpS2110 = 48 + _M0L1cS717;
      _M0L6_2atmpS2109 = _M0L6_2atmpS2110 & 0xff;
      if (
        _M0L6_2atmpS2108 < 0
        || _M0L6_2atmpS2108 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2108] = _M0L6_2atmpS2109;
      _M0L6_2atmpS2112 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2112 + 3;
    } else {
      int32_t _M0L6_2atmpS2117 = _M0Lm3expS707;
      if (_M0L6_2atmpS2117 >= 10) {
        int32_t _M0L6_2atmpS2127 = _M0Lm3expS707;
        int32_t _M0L1aS718 = _M0L6_2atmpS2127 / 10;
        int32_t _M0L6_2atmpS2126 = _M0Lm3expS707;
        int32_t _M0L1bS719 = _M0L6_2atmpS2126 % 10;
        int32_t _M0L6_2atmpS2118 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2120 = 48 + _M0L1aS718;
        int32_t _M0L6_2atmpS2119 = _M0L6_2atmpS2120 & 0xff;
        int32_t _M0L6_2atmpS2124;
        int32_t _M0L6_2atmpS2121;
        int32_t _M0L6_2atmpS2123;
        int32_t _M0L6_2atmpS2122;
        int32_t _M0L6_2atmpS2125;
        if (
          _M0L6_2atmpS2118 < 0
          || _M0L6_2atmpS2118 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2118] = _M0L6_2atmpS2119;
        _M0L6_2atmpS2124 = _M0Lm5indexS702;
        _M0L6_2atmpS2121 = _M0L6_2atmpS2124 + 1;
        _M0L6_2atmpS2123 = 48 + _M0L1bS719;
        _M0L6_2atmpS2122 = _M0L6_2atmpS2123 & 0xff;
        if (
          _M0L6_2atmpS2121 < 0
          || _M0L6_2atmpS2121 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2121] = _M0L6_2atmpS2122;
        _M0L6_2atmpS2125 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2125 + 2;
      } else {
        int32_t _M0L6_2atmpS2128 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2131 = _M0Lm3expS707;
        int32_t _M0L6_2atmpS2130 = 48 + _M0L6_2atmpS2131;
        int32_t _M0L6_2atmpS2129 = _M0L6_2atmpS2130 & 0xff;
        int32_t _M0L6_2atmpS2132;
        if (
          _M0L6_2atmpS2128 < 0
          || _M0L6_2atmpS2128 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2128] = _M0L6_2atmpS2129;
        _M0L6_2atmpS2132 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2132 + 1;
      }
    }
    _M0L6_2atmpS2133 = _M0Lm5indexS702;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3286
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2133);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_3286;
  } else {
    int32_t _M0L6_2atmpS2142 = _M0Lm3expS707;
    int32_t _M0L6_2atmpS2205;
    moonbit_string_t _result_3292;
    if (_M0L6_2atmpS2142 < 0) {
      int32_t _M0L6_2atmpS2143 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2145;
      int32_t _M0L6_2atmpS2144;
      int32_t _M0L6_2atmpS2146;
      int32_t _M0L1iS720;
      int32_t _M0L6_2atmpS2161;
      int32_t _M0L6_2atmpS2163;
      int32_t _M0L6_2atmpS2162;
      int32_t _M0L7currentS722;
      int32_t _M0L1iS723;
      uint64_t _M0L6outputS724;
      if (
        _M0L6_2atmpS2143 < 0
        || _M0L6_2atmpS2143 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2143] = 48;
      _M0L6_2atmpS2145 = _M0Lm5indexS702;
      _M0L6_2atmpS2144 = _M0L6_2atmpS2145 + 1;
      if (
        _M0L6_2atmpS2144 < 0
        || _M0L6_2atmpS2144 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2144] = 46;
      _M0L6_2atmpS2146 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2146 + 2;
      _M0L1iS720 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2147 = _M0Lm3expS707;
        if (_M0L1iS720 > _M0L6_2atmpS2147) {
          int32_t _M0L6_2atmpS2150 = _M0Lm5indexS702;
          int32_t _M0L6_2atmpS2149 = _M0L6_2atmpS2150 - _M0L1iS720;
          int32_t _M0L6_2atmpS2148 = _M0L6_2atmpS2149 - 1;
          int32_t _M0L6_2atmpS2151;
          if (
            _M0L6_2atmpS2148 < 0
            || _M0L6_2atmpS2148 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2148] = 48;
          _M0L6_2atmpS2151 = _M0L1iS720 - 1;
          _M0L1iS720 = _M0L6_2atmpS2151;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2161 = _M0Lm5indexS702;
      _M0L6_2atmpS2163 = _M0Lm3expS707;
      _M0L6_2atmpS2162 = -1 - _M0L6_2atmpS2163;
      _M0L7currentS722 = _M0L6_2atmpS2161 + _M0L6_2atmpS2162;
      _M0L1iS723 = 0;
      _M0L6outputS724 = _M0L6outputS704;
      while (1) {
        if (_M0L1iS723 < _M0L7olengthS706) {
          int32_t _M0L6_2atmpS2158 = _M0L7currentS722 + _M0L7olengthS706;
          int32_t _M0L6_2atmpS2157 = _M0L6_2atmpS2158 - _M0L1iS723;
          int32_t _M0L6_2atmpS2152 = _M0L6_2atmpS2157 - 1;
          uint64_t _M0L6_2atmpS2156 = _M0L6outputS724 % 10ull;
          int32_t _M0L6_2atmpS2155 = (int32_t)_M0L6_2atmpS2156;
          int32_t _M0L6_2atmpS2154 = 48 + _M0L6_2atmpS2155;
          int32_t _M0L6_2atmpS2153 = _M0L6_2atmpS2154 & 0xff;
          int32_t _M0L6_2atmpS2159;
          uint64_t _M0L6_2atmpS2160;
          if (
            _M0L6_2atmpS2152 < 0
            || _M0L6_2atmpS2152 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2152] = _M0L6_2atmpS2153;
          _M0L6_2atmpS2159 = _M0L1iS723 + 1;
          _M0L6_2atmpS2160 = _M0L6outputS724 / 10ull;
          _M0L1iS723 = _M0L6_2atmpS2159;
          _M0L6outputS724 = _M0L6_2atmpS2160;
          continue;
        }
        break;
      }
      _M0Lm5indexS702 = _M0L7currentS722 + _M0L7olengthS706;
    } else {
      int32_t _M0L6_2atmpS2165 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2164 = _M0L6_2atmpS2165 + 1;
      if (_M0L6_2atmpS2164 >= _M0L7olengthS706) {
        int32_t _M0L1iS726 = 0;
        uint64_t _M0L6outputS727 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2176;
        int32_t _M0L6_2atmpS2181;
        int32_t _M0L7_2abindS729;
        int32_t _M0L1iS730;
        int32_t _M0L6_2atmpS2182;
        int32_t _M0L6_2atmpS2185;
        int32_t _M0L6_2atmpS2184;
        int32_t _M0L6_2atmpS2183;
        while (1) {
          if (_M0L1iS726 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2173 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2172 = _M0L6_2atmpS2173 + _M0L7olengthS706;
            int32_t _M0L6_2atmpS2171 = _M0L6_2atmpS2172 - _M0L1iS726;
            int32_t _M0L6_2atmpS2166 = _M0L6_2atmpS2171 - 1;
            uint64_t _M0L6_2atmpS2170 = _M0L6outputS727 % 10ull;
            int32_t _M0L6_2atmpS2169 = (int32_t)_M0L6_2atmpS2170;
            int32_t _M0L6_2atmpS2168 = 48 + _M0L6_2atmpS2169;
            int32_t _M0L6_2atmpS2167 = _M0L6_2atmpS2168 & 0xff;
            int32_t _M0L6_2atmpS2174;
            uint64_t _M0L6_2atmpS2175;
            if (
              _M0L6_2atmpS2166 < 0
              || _M0L6_2atmpS2166 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2166] = _M0L6_2atmpS2167;
            _M0L6_2atmpS2174 = _M0L1iS726 + 1;
            _M0L6_2atmpS2175 = _M0L6outputS727 / 10ull;
            _M0L1iS726 = _M0L6_2atmpS2174;
            _M0L6outputS727 = _M0L6_2atmpS2175;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2176 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2176 + _M0L7olengthS706;
        _M0L6_2atmpS2181 = _M0Lm3expS707;
        _M0L7_2abindS729 = _M0L6_2atmpS2181 + 1;
        _M0L1iS730 = _M0L7olengthS706;
        while (1) {
          if (_M0L1iS730 < _M0L7_2abindS729) {
            int32_t _M0L6_2atmpS2179 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2178 = _M0L6_2atmpS2179 + _M0L1iS730;
            int32_t _M0L6_2atmpS2177 = _M0L6_2atmpS2178 - _M0L7olengthS706;
            int32_t _M0L6_2atmpS2180;
            if (
              _M0L6_2atmpS2177 < 0
              || _M0L6_2atmpS2177 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2177] = 48;
            _M0L6_2atmpS2180 = _M0L1iS730 + 1;
            _M0L1iS730 = _M0L6_2atmpS2180;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2182 = _M0Lm5indexS702;
        _M0L6_2atmpS2185 = _M0Lm3expS707;
        _M0L6_2atmpS2184 = _M0L6_2atmpS2185 + 1;
        _M0L6_2atmpS2183 = _M0L6_2atmpS2184 - _M0L7olengthS706;
        _M0Lm5indexS702 = _M0L6_2atmpS2182 + _M0L6_2atmpS2183;
      } else {
        int32_t _M0L6_2atmpS2202 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2201 = _M0L6_2atmpS2202 + 1;
        int32_t _M0L1iS732 = 0;
        int32_t _M0L7currentS733 = _M0L6_2atmpS2201;
        uint64_t _M0L6outputS734 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2203;
        int32_t _M0L6_2atmpS2204;
        while (1) {
          if (_M0L1iS732 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2197 = _M0L7olengthS706 - _M0L1iS732;
            int32_t _M0L6_2atmpS2195 = _M0L6_2atmpS2197 - 1;
            int32_t _M0L6_2atmpS2196 = _M0Lm3expS707;
            int32_t _M0L7currentS735;
            int32_t _M0L6_2atmpS2192;
            int32_t _M0L6_2atmpS2191;
            int32_t _M0L6_2atmpS2186;
            uint64_t _M0L6_2atmpS2190;
            int32_t _M0L6_2atmpS2189;
            int32_t _M0L6_2atmpS2188;
            int32_t _M0L6_2atmpS2187;
            int32_t _M0L6_2atmpS2193;
            uint64_t _M0L6_2atmpS2194;
            if (_M0L6_2atmpS2195 == _M0L6_2atmpS2196) {
              int32_t _M0L6_2atmpS2200 = _M0L7currentS733 + _M0L7olengthS706;
              int32_t _M0L6_2atmpS2199 = _M0L6_2atmpS2200 - _M0L1iS732;
              int32_t _M0L6_2atmpS2198 = _M0L6_2atmpS2199 - 1;
              if (
                _M0L6_2atmpS2198 < 0
                || _M0L6_2atmpS2198 >= Moonbit_array_length(_M0L6resultS701)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS701[_M0L6_2atmpS2198] = 46;
              _M0L7currentS735 = _M0L7currentS733 - 1;
            } else {
              _M0L7currentS735 = _M0L7currentS733;
            }
            _M0L6_2atmpS2192 = _M0L7currentS735 + _M0L7olengthS706;
            _M0L6_2atmpS2191 = _M0L6_2atmpS2192 - _M0L1iS732;
            _M0L6_2atmpS2186 = _M0L6_2atmpS2191 - 1;
            _M0L6_2atmpS2190 = _M0L6outputS734 % 10ull;
            _M0L6_2atmpS2189 = (int32_t)_M0L6_2atmpS2190;
            _M0L6_2atmpS2188 = 48 + _M0L6_2atmpS2189;
            _M0L6_2atmpS2187 = _M0L6_2atmpS2188 & 0xff;
            if (
              _M0L6_2atmpS2186 < 0
              || _M0L6_2atmpS2186 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2186] = _M0L6_2atmpS2187;
            _M0L6_2atmpS2193 = _M0L1iS732 + 1;
            _M0L6_2atmpS2194 = _M0L6outputS734 / 10ull;
            _M0L1iS732 = _M0L6_2atmpS2193;
            _M0L7currentS733 = _M0L7currentS735;
            _M0L6outputS734 = _M0L6_2atmpS2194;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2203 = _M0Lm5indexS702;
        _M0L6_2atmpS2204 = _M0L7olengthS706 + 1;
        _M0Lm5indexS702 = _M0L6_2atmpS2203 + _M0L6_2atmpS2204;
      }
    }
    _M0L6_2atmpS2205 = _M0Lm5indexS702;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3292
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2205);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_3292;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS647,
  uint32_t _M0L12ieeeExponentS646
) {
  int32_t _M0Lm2e2S644;
  uint64_t _M0Lm2m2S645;
  uint64_t _M0L6_2atmpS2079;
  uint64_t _M0L6_2atmpS2078;
  int32_t _M0L4evenS648;
  uint64_t _M0L6_2atmpS2077;
  uint64_t _M0L2mvS649;
  int32_t _M0L7mmShiftS650;
  uint64_t _M0Lm2vrS651;
  uint64_t _M0Lm2vpS652;
  uint64_t _M0Lm2vmS653;
  int32_t _M0Lm3e10S654;
  int32_t _M0Lm17vmIsTrailingZerosS655;
  int32_t _M0Lm17vrIsTrailingZerosS656;
  int32_t _M0L6_2atmpS1979;
  int32_t _M0Lm7removedS675;
  int32_t _M0Lm16lastRemovedDigitS676;
  uint64_t _M0Lm6outputS677;
  int32_t _M0L6_2atmpS2075;
  int32_t _M0L6_2atmpS2076;
  int32_t _M0L3expS700;
  uint64_t _M0L6_2atmpS2074;
  struct _M0TPB17FloatingDecimal64* _block_3298;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S644 = 0;
  _M0Lm2m2S645 = 0ull;
  if (_M0L12ieeeExponentS646 == 0u) {
    _M0Lm2e2S644 = -1076;
    _M0Lm2m2S645 = _M0L12ieeeMantissaS647;
  } else {
    int32_t _M0L6_2atmpS1978 = *(int32_t*)&_M0L12ieeeExponentS646;
    int32_t _M0L6_2atmpS1977 = _M0L6_2atmpS1978 - 1023;
    int32_t _M0L6_2atmpS1976 = _M0L6_2atmpS1977 - 52;
    _M0Lm2e2S644 = _M0L6_2atmpS1976 - 2;
    _M0Lm2m2S645 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  }
  _M0L6_2atmpS2079 = _M0Lm2m2S645;
  _M0L6_2atmpS2078 = _M0L6_2atmpS2079 & 1ull;
  _M0L4evenS648 = _M0L6_2atmpS2078 == 0ull;
  _M0L6_2atmpS2077 = _M0Lm2m2S645;
  _M0L2mvS649 = 4ull * _M0L6_2atmpS2077;
  _M0L7mmShiftS650
  = _M0L12ieeeMantissaS647 != 0ull || _M0L12ieeeExponentS646 <= 1u;
  _M0Lm2vrS651 = 0ull;
  _M0Lm2vpS652 = 0ull;
  _M0Lm2vmS653 = 0ull;
  _M0Lm3e10S654 = 0;
  _M0Lm17vmIsTrailingZerosS655 = 0;
  _M0Lm17vrIsTrailingZerosS656 = 0;
  _M0L6_2atmpS1979 = _M0Lm2e2S644;
  if (_M0L6_2atmpS1979 >= 0) {
    int32_t _M0L6_2atmpS2001 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1997;
    int32_t _M0L6_2atmpS2000;
    int32_t _M0L6_2atmpS1999;
    int32_t _M0L6_2atmpS1998;
    int32_t _M0L1qS657;
    int32_t _M0L6_2atmpS1996;
    int32_t _M0L6_2atmpS1995;
    int32_t _M0L1kS658;
    int32_t _M0L6_2atmpS1994;
    int32_t _M0L6_2atmpS1993;
    int32_t _M0L6_2atmpS1992;
    int32_t _M0L1iS659;
    struct _M0TPB8Pow5Pair _M0L4pow5S660;
    uint64_t _M0L6_2atmpS1991;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS661;
    uint64_t _M0L8_2avrOutS662;
    uint64_t _M0L8_2avpOutS663;
    uint64_t _M0L8_2avmOutS664;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1997 = _M0FPB9log10Pow2(_M0L6_2atmpS2001);
    _M0L6_2atmpS2000 = _M0Lm2e2S644;
    _M0L6_2atmpS1999 = _M0L6_2atmpS2000 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1998 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1999);
    _M0L1qS657 = _M0L6_2atmpS1997 - _M0L6_2atmpS1998;
    _M0Lm3e10S654 = _M0L1qS657;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1996 = _M0FPB8pow5bits(_M0L1qS657);
    _M0L6_2atmpS1995 = 125 + _M0L6_2atmpS1996;
    _M0L1kS658 = _M0L6_2atmpS1995 - 1;
    _M0L6_2atmpS1994 = _M0Lm2e2S644;
    _M0L6_2atmpS1993 = -_M0L6_2atmpS1994;
    _M0L6_2atmpS1992 = _M0L6_2atmpS1993 + _M0L1qS657;
    _M0L1iS659 = _M0L6_2atmpS1992 + _M0L1kS658;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S660 = _M0FPB22double__computeInvPow5(_M0L1qS657);
    _M0L6_2atmpS1991 = _M0Lm2m2S645;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS661
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1991, _M0L4pow5S660, _M0L1iS659, _M0L7mmShiftS650);
    _M0L8_2avrOutS662 = _M0L7_2abindS661.$0;
    _M0L8_2avpOutS663 = _M0L7_2abindS661.$1;
    _M0L8_2avmOutS664 = _M0L7_2abindS661.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS662;
    _M0Lm2vpS652 = _M0L8_2avpOutS663;
    _M0Lm2vmS653 = _M0L8_2avmOutS664;
    if (_M0L1qS657 <= 21) {
      int32_t _M0L6_2atmpS1987 = (int32_t)_M0L2mvS649;
      uint64_t _M0L6_2atmpS1990 = _M0L2mvS649 / 5ull;
      int32_t _M0L6_2atmpS1989 = (int32_t)_M0L6_2atmpS1990;
      int32_t _M0L6_2atmpS1988 = 5 * _M0L6_2atmpS1989;
      int32_t _M0L6mvMod5S665 = _M0L6_2atmpS1987 - _M0L6_2atmpS1988;
      if (_M0L6mvMod5S665 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS649, _M0L1qS657);
      } else if (_M0L4evenS648) {
        uint64_t _M0L6_2atmpS1981 = _M0L2mvS649 - 1ull;
        uint64_t _M0L6_2atmpS1982;
        uint64_t _M0L6_2atmpS1980;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1982 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS650);
        _M0L6_2atmpS1980 = _M0L6_2atmpS1981 - _M0L6_2atmpS1982;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1980, _M0L1qS657);
      } else {
        uint64_t _M0L6_2atmpS1983 = _M0Lm2vpS652;
        uint64_t _M0L6_2atmpS1986 = _M0L2mvS649 + 2ull;
        int32_t _M0L6_2atmpS1985;
        uint64_t _M0L6_2atmpS1984;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1985
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1986, _M0L1qS657);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1984 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1985);
        _M0Lm2vpS652 = _M0L6_2atmpS1983 - _M0L6_2atmpS1984;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2015 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS2014 = -_M0L6_2atmpS2015;
    int32_t _M0L6_2atmpS2009;
    int32_t _M0L6_2atmpS2013;
    int32_t _M0L6_2atmpS2012;
    int32_t _M0L6_2atmpS2011;
    int32_t _M0L6_2atmpS2010;
    int32_t _M0L1qS666;
    int32_t _M0L6_2atmpS2002;
    int32_t _M0L6_2atmpS2008;
    int32_t _M0L6_2atmpS2007;
    int32_t _M0L1iS667;
    int32_t _M0L6_2atmpS2006;
    int32_t _M0L1kS668;
    int32_t _M0L1jS669;
    struct _M0TPB8Pow5Pair _M0L4pow5S670;
    uint64_t _M0L6_2atmpS2005;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS671;
    uint64_t _M0L8_2avrOutS672;
    uint64_t _M0L8_2avpOutS673;
    uint64_t _M0L8_2avmOutS674;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2009 = _M0FPB9log10Pow5(_M0L6_2atmpS2014);
    _M0L6_2atmpS2013 = _M0Lm2e2S644;
    _M0L6_2atmpS2012 = -_M0L6_2atmpS2013;
    _M0L6_2atmpS2011 = _M0L6_2atmpS2012 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2010 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2011);
    _M0L1qS666 = _M0L6_2atmpS2009 - _M0L6_2atmpS2010;
    _M0L6_2atmpS2002 = _M0Lm2e2S644;
    _M0Lm3e10S654 = _M0L1qS666 + _M0L6_2atmpS2002;
    _M0L6_2atmpS2008 = _M0Lm2e2S644;
    _M0L6_2atmpS2007 = -_M0L6_2atmpS2008;
    _M0L1iS667 = _M0L6_2atmpS2007 - _M0L1qS666;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2006 = _M0FPB8pow5bits(_M0L1iS667);
    _M0L1kS668 = _M0L6_2atmpS2006 - 125;
    _M0L1jS669 = _M0L1qS666 - _M0L1kS668;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S670 = _M0FPB19double__computePow5(_M0L1iS667);
    _M0L6_2atmpS2005 = _M0Lm2m2S645;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS671
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2005, _M0L4pow5S670, _M0L1jS669, _M0L7mmShiftS650);
    _M0L8_2avrOutS672 = _M0L7_2abindS671.$0;
    _M0L8_2avpOutS673 = _M0L7_2abindS671.$1;
    _M0L8_2avmOutS674 = _M0L7_2abindS671.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS672;
    _M0Lm2vpS652 = _M0L8_2avpOutS673;
    _M0Lm2vmS653 = _M0L8_2avmOutS674;
    if (_M0L1qS666 <= 1) {
      _M0Lm17vrIsTrailingZerosS656 = 1;
      if (_M0L4evenS648) {
        int32_t _M0L6_2atmpS2003;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2003 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS650);
        _M0Lm17vmIsTrailingZerosS655 = _M0L6_2atmpS2003 == 1;
      } else {
        uint64_t _M0L6_2atmpS2004 = _M0Lm2vpS652;
        _M0Lm2vpS652 = _M0L6_2atmpS2004 - 1ull;
      }
    } else if (_M0L1qS666 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS656
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS649, _M0L1qS666);
    }
  }
  _M0Lm7removedS675 = 0;
  _M0Lm16lastRemovedDigitS676 = 0;
  _M0Lm6outputS677 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS655 || _M0Lm17vrIsTrailingZerosS656) {
    int32_t _if__result_3295;
    uint64_t _M0L6_2atmpS2045;
    uint64_t _M0L6_2atmpS2051;
    uint64_t _M0L6_2atmpS2052;
    int32_t _if__result_3296;
    int32_t _M0L6_2atmpS2048;
    int64_t _M0L6_2atmpS2047;
    uint64_t _M0L6_2atmpS2046;
    while (1) {
      uint64_t _M0L6_2atmpS2028 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S678 = _M0L6_2atmpS2028 / 10ull;
      uint64_t _M0L6_2atmpS2027 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS2027 / 10ull;
      uint64_t _M0L6_2atmpS2026;
      int32_t _M0L6_2atmpS2023;
      int32_t _M0L6_2atmpS2025;
      int32_t _M0L6_2atmpS2024;
      int32_t _M0L7vmMod10S681;
      uint64_t _M0L6_2atmpS2022;
      uint64_t _M0L7vrDiv10S682;
      uint64_t _M0L6_2atmpS2021;
      int32_t _M0L6_2atmpS2018;
      int32_t _M0L6_2atmpS2020;
      int32_t _M0L6_2atmpS2019;
      int32_t _M0L7vrMod10S683;
      int32_t _M0L6_2atmpS2017;
      if (_M0L7vpDiv10S678 <= _M0L7vmDiv10S679) {
        break;
      }
      _M0L6_2atmpS2026 = _M0Lm2vmS653;
      _M0L6_2atmpS2023 = (int32_t)_M0L6_2atmpS2026;
      _M0L6_2atmpS2025 = (int32_t)_M0L7vmDiv10S679;
      _M0L6_2atmpS2024 = 10 * _M0L6_2atmpS2025;
      _M0L7vmMod10S681 = _M0L6_2atmpS2023 - _M0L6_2atmpS2024;
      _M0L6_2atmpS2022 = _M0Lm2vrS651;
      _M0L7vrDiv10S682 = _M0L6_2atmpS2022 / 10ull;
      _M0L6_2atmpS2021 = _M0Lm2vrS651;
      _M0L6_2atmpS2018 = (int32_t)_M0L6_2atmpS2021;
      _M0L6_2atmpS2020 = (int32_t)_M0L7vrDiv10S682;
      _M0L6_2atmpS2019 = 10 * _M0L6_2atmpS2020;
      _M0L7vrMod10S683 = _M0L6_2atmpS2018 - _M0L6_2atmpS2019;
      _M0Lm17vmIsTrailingZerosS655
      = _M0Lm17vmIsTrailingZerosS655 && _M0L7vmMod10S681 == 0;
      if (_M0Lm17vrIsTrailingZerosS656) {
        int32_t _M0L6_2atmpS2016 = _M0Lm16lastRemovedDigitS676;
        _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS2016 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS656 = 0;
      }
      _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S683;
      _M0Lm2vrS651 = _M0L7vrDiv10S682;
      _M0Lm2vpS652 = _M0L7vpDiv10S678;
      _M0Lm2vmS653 = _M0L7vmDiv10S679;
      _M0L6_2atmpS2017 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS2017 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS655) {
      while (1) {
        uint64_t _M0L6_2atmpS2041 = _M0Lm2vmS653;
        uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS2041 / 10ull;
        uint64_t _M0L6_2atmpS2040 = _M0Lm2vmS653;
        int32_t _M0L6_2atmpS2037 = (int32_t)_M0L6_2atmpS2040;
        int32_t _M0L6_2atmpS2039 = (int32_t)_M0L7vmDiv10S684;
        int32_t _M0L6_2atmpS2038 = 10 * _M0L6_2atmpS2039;
        int32_t _M0L7vmMod10S685 = _M0L6_2atmpS2037 - _M0L6_2atmpS2038;
        uint64_t _M0L6_2atmpS2036;
        uint64_t _M0L7vpDiv10S687;
        uint64_t _M0L6_2atmpS2035;
        uint64_t _M0L7vrDiv10S688;
        uint64_t _M0L6_2atmpS2034;
        int32_t _M0L6_2atmpS2031;
        int32_t _M0L6_2atmpS2033;
        int32_t _M0L6_2atmpS2032;
        int32_t _M0L7vrMod10S689;
        int32_t _M0L6_2atmpS2030;
        if (_M0L7vmMod10S685 != 0) {
          break;
        }
        _M0L6_2atmpS2036 = _M0Lm2vpS652;
        _M0L7vpDiv10S687 = _M0L6_2atmpS2036 / 10ull;
        _M0L6_2atmpS2035 = _M0Lm2vrS651;
        _M0L7vrDiv10S688 = _M0L6_2atmpS2035 / 10ull;
        _M0L6_2atmpS2034 = _M0Lm2vrS651;
        _M0L6_2atmpS2031 = (int32_t)_M0L6_2atmpS2034;
        _M0L6_2atmpS2033 = (int32_t)_M0L7vrDiv10S688;
        _M0L6_2atmpS2032 = 10 * _M0L6_2atmpS2033;
        _M0L7vrMod10S689 = _M0L6_2atmpS2031 - _M0L6_2atmpS2032;
        if (_M0Lm17vrIsTrailingZerosS656) {
          int32_t _M0L6_2atmpS2029 = _M0Lm16lastRemovedDigitS676;
          _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS2029 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS656 = 0;
        }
        _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S689;
        _M0Lm2vrS651 = _M0L7vrDiv10S688;
        _M0Lm2vpS652 = _M0L7vpDiv10S687;
        _M0Lm2vmS653 = _M0L7vmDiv10S684;
        _M0L6_2atmpS2030 = _M0Lm7removedS675;
        _M0Lm7removedS675 = _M0L6_2atmpS2030 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS656) {
      int32_t _M0L6_2atmpS2044 = _M0Lm16lastRemovedDigitS676;
      if (_M0L6_2atmpS2044 == 5) {
        uint64_t _M0L6_2atmpS2043 = _M0Lm2vrS651;
        uint64_t _M0L6_2atmpS2042 = _M0L6_2atmpS2043 % 2ull;
        _if__result_3295 = _M0L6_2atmpS2042 == 0ull;
      } else {
        _if__result_3295 = 0;
      }
    } else {
      _if__result_3295 = 0;
    }
    if (_if__result_3295) {
      _M0Lm16lastRemovedDigitS676 = 4;
    }
    _M0L6_2atmpS2045 = _M0Lm2vrS651;
    _M0L6_2atmpS2051 = _M0Lm2vrS651;
    _M0L6_2atmpS2052 = _M0Lm2vmS653;
    if (_M0L6_2atmpS2051 == _M0L6_2atmpS2052) {
      if (!_M0L4evenS648) {
        _if__result_3296 = 1;
      } else {
        int32_t _M0L6_2atmpS2050 = _M0Lm17vmIsTrailingZerosS655;
        _if__result_3296 = !_M0L6_2atmpS2050;
      }
    } else {
      _if__result_3296 = 0;
    }
    if (_if__result_3296) {
      _M0L6_2atmpS2048 = 1;
    } else {
      int32_t _M0L6_2atmpS2049 = _M0Lm16lastRemovedDigitS676;
      _M0L6_2atmpS2048 = _M0L6_2atmpS2049 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2047 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2048);
    _M0L6_2atmpS2046 = *(uint64_t*)&_M0L6_2atmpS2047;
    _M0Lm6outputS677 = _M0L6_2atmpS2045 + _M0L6_2atmpS2046;
  } else {
    int32_t _M0Lm7roundUpS690 = 0;
    uint64_t _M0L6_2atmpS2073 = _M0Lm2vpS652;
    uint64_t _M0L8vpDiv100S691 = _M0L6_2atmpS2073 / 100ull;
    uint64_t _M0L6_2atmpS2072 = _M0Lm2vmS653;
    uint64_t _M0L8vmDiv100S692 = _M0L6_2atmpS2072 / 100ull;
    uint64_t _M0L6_2atmpS2067;
    uint64_t _M0L6_2atmpS2070;
    uint64_t _M0L6_2atmpS2071;
    int32_t _M0L6_2atmpS2069;
    uint64_t _M0L6_2atmpS2068;
    if (_M0L8vpDiv100S691 > _M0L8vmDiv100S692) {
      uint64_t _M0L6_2atmpS2058 = _M0Lm2vrS651;
      uint64_t _M0L8vrDiv100S693 = _M0L6_2atmpS2058 / 100ull;
      uint64_t _M0L6_2atmpS2057 = _M0Lm2vrS651;
      int32_t _M0L6_2atmpS2054 = (int32_t)_M0L6_2atmpS2057;
      int32_t _M0L6_2atmpS2056 = (int32_t)_M0L8vrDiv100S693;
      int32_t _M0L6_2atmpS2055 = 100 * _M0L6_2atmpS2056;
      int32_t _M0L8vrMod100S694 = _M0L6_2atmpS2054 - _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2053;
      _M0Lm7roundUpS690 = _M0L8vrMod100S694 >= 50;
      _M0Lm2vrS651 = _M0L8vrDiv100S693;
      _M0Lm2vpS652 = _M0L8vpDiv100S691;
      _M0Lm2vmS653 = _M0L8vmDiv100S692;
      _M0L6_2atmpS2053 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS2053 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2066 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S695 = _M0L6_2atmpS2066 / 10ull;
      uint64_t _M0L6_2atmpS2065 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S696 = _M0L6_2atmpS2065 / 10ull;
      uint64_t _M0L6_2atmpS2064;
      uint64_t _M0L7vrDiv10S698;
      uint64_t _M0L6_2atmpS2063;
      int32_t _M0L6_2atmpS2060;
      int32_t _M0L6_2atmpS2062;
      int32_t _M0L6_2atmpS2061;
      int32_t _M0L7vrMod10S699;
      int32_t _M0L6_2atmpS2059;
      if (_M0L7vpDiv10S695 <= _M0L7vmDiv10S696) {
        break;
      }
      _M0L6_2atmpS2064 = _M0Lm2vrS651;
      _M0L7vrDiv10S698 = _M0L6_2atmpS2064 / 10ull;
      _M0L6_2atmpS2063 = _M0Lm2vrS651;
      _M0L6_2atmpS2060 = (int32_t)_M0L6_2atmpS2063;
      _M0L6_2atmpS2062 = (int32_t)_M0L7vrDiv10S698;
      _M0L6_2atmpS2061 = 10 * _M0L6_2atmpS2062;
      _M0L7vrMod10S699 = _M0L6_2atmpS2060 - _M0L6_2atmpS2061;
      _M0Lm7roundUpS690 = _M0L7vrMod10S699 >= 5;
      _M0Lm2vrS651 = _M0L7vrDiv10S698;
      _M0Lm2vpS652 = _M0L7vpDiv10S695;
      _M0Lm2vmS653 = _M0L7vmDiv10S696;
      _M0L6_2atmpS2059 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS2059 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2067 = _M0Lm2vrS651;
    _M0L6_2atmpS2070 = _M0Lm2vrS651;
    _M0L6_2atmpS2071 = _M0Lm2vmS653;
    _M0L6_2atmpS2069
    = _M0L6_2atmpS2070 == _M0L6_2atmpS2071 || _M0Lm7roundUpS690;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2068 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2069);
    _M0Lm6outputS677 = _M0L6_2atmpS2067 + _M0L6_2atmpS2068;
  }
  _M0L6_2atmpS2075 = _M0Lm3e10S654;
  _M0L6_2atmpS2076 = _M0Lm7removedS675;
  _M0L3expS700 = _M0L6_2atmpS2075 + _M0L6_2atmpS2076;
  _M0L6_2atmpS2074 = _M0Lm6outputS677;
  _block_3298
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_3298)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3298->$0 = _M0L6_2atmpS2074;
  _block_3298->$1 = _M0L3expS700;
  return _block_3298;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS643) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS643) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS642) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS641) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS640) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS640 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS640 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS640 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS640 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS640 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS640 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS640 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS640 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS640 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS640 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS640 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS640 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS640 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS640 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS640 >= 100ull) {
    return 3;
  }
  if (_M0L1vS640 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS623) {
  int32_t _M0L6_2atmpS1975;
  int32_t _M0L6_2atmpS1974;
  int32_t _M0L4baseS622;
  int32_t _M0L5base2S624;
  int32_t _M0L6offsetS625;
  int32_t _M0L6_2atmpS1973;
  uint64_t _M0L4mul0S626;
  int32_t _M0L6_2atmpS1972;
  int32_t _M0L6_2atmpS1971;
  uint64_t _M0L4mul1S627;
  uint64_t _M0L1mS628;
  struct _M0TPB7Umul128 _M0L7_2abindS629;
  uint64_t _M0L7_2alow1S630;
  uint64_t _M0L8_2ahigh1S631;
  struct _M0TPB7Umul128 _M0L7_2abindS632;
  uint64_t _M0L7_2alow0S633;
  uint64_t _M0L8_2ahigh0S634;
  uint64_t _M0L3sumS635;
  uint64_t _M0Lm5high1S636;
  int32_t _M0L6_2atmpS1969;
  int32_t _M0L6_2atmpS1970;
  int32_t _M0L5deltaS637;
  uint64_t _M0L6_2atmpS1968;
  uint64_t _M0L6_2atmpS1960;
  int32_t _M0L6_2atmpS1967;
  uint32_t _M0L6_2atmpS1964;
  int32_t _M0L6_2atmpS1966;
  int32_t _M0L6_2atmpS1965;
  uint32_t _M0L6_2atmpS1963;
  uint32_t _M0L6_2atmpS1962;
  uint64_t _M0L6_2atmpS1961;
  uint64_t _M0L1aS638;
  uint64_t _M0L6_2atmpS1959;
  uint64_t _M0L1bS639;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1975 = _M0L1iS623 + 26;
  _M0L6_2atmpS1974 = _M0L6_2atmpS1975 - 1;
  _M0L4baseS622 = _M0L6_2atmpS1974 / 26;
  _M0L5base2S624 = _M0L4baseS622 * 26;
  _M0L6offsetS625 = _M0L5base2S624 - _M0L1iS623;
  _M0L6_2atmpS1973 = _M0L4baseS622 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1973);
  _M0L6_2atmpS1972 = _M0L4baseS622 * 2;
  _M0L6_2atmpS1971 = _M0L6_2atmpS1972 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1971);
  if (_M0L6offsetS625 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S626, .$1 = _M0L4mul1S627};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS625);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS629 = _M0FPB7umul128(_M0L1mS628, _M0L4mul1S627);
  _M0L7_2alow1S630 = _M0L7_2abindS629.$0;
  _M0L8_2ahigh1S631 = _M0L7_2abindS629.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS632 = _M0FPB7umul128(_M0L1mS628, _M0L4mul0S626);
  _M0L7_2alow0S633 = _M0L7_2abindS632.$0;
  _M0L8_2ahigh0S634 = _M0L7_2abindS632.$1;
  _M0L3sumS635 = _M0L8_2ahigh0S634 + _M0L7_2alow1S630;
  _M0Lm5high1S636 = _M0L8_2ahigh1S631;
  if (_M0L3sumS635 < _M0L8_2ahigh0S634) {
    uint64_t _M0L6_2atmpS1958 = _M0Lm5high1S636;
    _M0Lm5high1S636 = _M0L6_2atmpS1958 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1969 = _M0FPB8pow5bits(_M0L5base2S624);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1970 = _M0FPB8pow5bits(_M0L1iS623);
  _M0L5deltaS637 = _M0L6_2atmpS1969 - _M0L6_2atmpS1970;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1968
  = _M0FPB13shiftright128(_M0L7_2alow0S633, _M0L3sumS635, _M0L5deltaS637);
  _M0L6_2atmpS1960 = _M0L6_2atmpS1968 + 1ull;
  _M0L6_2atmpS1967 = _M0L1iS623 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1964
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1967);
  _M0L6_2atmpS1966 = _M0L1iS623 % 16;
  _M0L6_2atmpS1965 = _M0L6_2atmpS1966 << 1;
  _M0L6_2atmpS1963 = _M0L6_2atmpS1964 >> (_M0L6_2atmpS1965 & 31);
  _M0L6_2atmpS1962 = _M0L6_2atmpS1963 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1961 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1962);
  _M0L1aS638 = _M0L6_2atmpS1960 + _M0L6_2atmpS1961;
  _M0L6_2atmpS1959 = _M0Lm5high1S636;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS639
  = _M0FPB13shiftright128(_M0L3sumS635, _M0L6_2atmpS1959, _M0L5deltaS637);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS638, .$1 = _M0L1bS639};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS605) {
  int32_t _M0L4baseS604;
  int32_t _M0L5base2S606;
  int32_t _M0L6offsetS607;
  int32_t _M0L6_2atmpS1957;
  uint64_t _M0L4mul0S608;
  int32_t _M0L6_2atmpS1956;
  int32_t _M0L6_2atmpS1955;
  uint64_t _M0L4mul1S609;
  uint64_t _M0L1mS610;
  struct _M0TPB7Umul128 _M0L7_2abindS611;
  uint64_t _M0L7_2alow1S612;
  uint64_t _M0L8_2ahigh1S613;
  struct _M0TPB7Umul128 _M0L7_2abindS614;
  uint64_t _M0L7_2alow0S615;
  uint64_t _M0L8_2ahigh0S616;
  uint64_t _M0L3sumS617;
  uint64_t _M0Lm5high1S618;
  int32_t _M0L6_2atmpS1953;
  int32_t _M0L6_2atmpS1954;
  int32_t _M0L5deltaS619;
  uint64_t _M0L6_2atmpS1945;
  int32_t _M0L6_2atmpS1952;
  uint32_t _M0L6_2atmpS1949;
  int32_t _M0L6_2atmpS1951;
  int32_t _M0L6_2atmpS1950;
  uint32_t _M0L6_2atmpS1948;
  uint32_t _M0L6_2atmpS1947;
  uint64_t _M0L6_2atmpS1946;
  uint64_t _M0L1aS620;
  uint64_t _M0L6_2atmpS1944;
  uint64_t _M0L1bS621;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS604 = _M0L1iS605 / 26;
  _M0L5base2S606 = _M0L4baseS604 * 26;
  _M0L6offsetS607 = _M0L1iS605 - _M0L5base2S606;
  _M0L6_2atmpS1957 = _M0L4baseS604 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1957);
  _M0L6_2atmpS1956 = _M0L4baseS604 * 2;
  _M0L6_2atmpS1955 = _M0L6_2atmpS1956 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1955);
  if (_M0L6offsetS607 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S608, .$1 = _M0L4mul1S609};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS607);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS611 = _M0FPB7umul128(_M0L1mS610, _M0L4mul1S609);
  _M0L7_2alow1S612 = _M0L7_2abindS611.$0;
  _M0L8_2ahigh1S613 = _M0L7_2abindS611.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS614 = _M0FPB7umul128(_M0L1mS610, _M0L4mul0S608);
  _M0L7_2alow0S615 = _M0L7_2abindS614.$0;
  _M0L8_2ahigh0S616 = _M0L7_2abindS614.$1;
  _M0L3sumS617 = _M0L8_2ahigh0S616 + _M0L7_2alow1S612;
  _M0Lm5high1S618 = _M0L8_2ahigh1S613;
  if (_M0L3sumS617 < _M0L8_2ahigh0S616) {
    uint64_t _M0L6_2atmpS1943 = _M0Lm5high1S618;
    _M0Lm5high1S618 = _M0L6_2atmpS1943 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1953 = _M0FPB8pow5bits(_M0L1iS605);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1954 = _M0FPB8pow5bits(_M0L5base2S606);
  _M0L5deltaS619 = _M0L6_2atmpS1953 - _M0L6_2atmpS1954;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1945
  = _M0FPB13shiftright128(_M0L7_2alow0S615, _M0L3sumS617, _M0L5deltaS619);
  _M0L6_2atmpS1952 = _M0L1iS605 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1949
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1952);
  _M0L6_2atmpS1951 = _M0L1iS605 % 16;
  _M0L6_2atmpS1950 = _M0L6_2atmpS1951 << 1;
  _M0L6_2atmpS1948 = _M0L6_2atmpS1949 >> (_M0L6_2atmpS1950 & 31);
  _M0L6_2atmpS1947 = _M0L6_2atmpS1948 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1946 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1947);
  _M0L1aS620 = _M0L6_2atmpS1945 + _M0L6_2atmpS1946;
  _M0L6_2atmpS1944 = _M0Lm5high1S618;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS621
  = _M0FPB13shiftright128(_M0L3sumS617, _M0L6_2atmpS1944, _M0L5deltaS619);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS620, .$1 = _M0L1bS621};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS578,
  struct _M0TPB8Pow5Pair _M0L3mulS575,
  int32_t _M0L1jS591,
  int32_t _M0L7mmShiftS593
) {
  uint64_t _M0L7_2amul0S574;
  uint64_t _M0L7_2amul1S576;
  uint64_t _M0L1mS577;
  struct _M0TPB7Umul128 _M0L7_2abindS579;
  uint64_t _M0L5_2aloS580;
  uint64_t _M0L6_2atmpS581;
  struct _M0TPB7Umul128 _M0L7_2abindS582;
  uint64_t _M0L6_2alo2S583;
  uint64_t _M0L6_2ahi2S584;
  uint64_t _M0L3midS585;
  uint64_t _M0L6_2atmpS1942;
  uint64_t _M0L2hiS586;
  uint64_t _M0L3lo2S587;
  uint64_t _M0L6_2atmpS1940;
  uint64_t _M0L6_2atmpS1941;
  uint64_t _M0L4mid2S588;
  uint64_t _M0L6_2atmpS1939;
  uint64_t _M0L3hi2S589;
  int32_t _M0L6_2atmpS1938;
  int32_t _M0L6_2atmpS1937;
  uint64_t _M0L2vpS590;
  uint64_t _M0Lm2vmS592;
  int32_t _M0L6_2atmpS1936;
  int32_t _M0L6_2atmpS1935;
  uint64_t _M0L2vrS603;
  uint64_t _M0L6_2atmpS1934;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S574 = _M0L3mulS575.$0;
  _M0L7_2amul1S576 = _M0L3mulS575.$1;
  _M0L1mS577 = _M0L1mS578 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS579 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul0S574);
  _M0L5_2aloS580 = _M0L7_2abindS579.$0;
  _M0L6_2atmpS581 = _M0L7_2abindS579.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul1S576);
  _M0L6_2alo2S583 = _M0L7_2abindS582.$0;
  _M0L6_2ahi2S584 = _M0L7_2abindS582.$1;
  _M0L3midS585 = _M0L6_2atmpS581 + _M0L6_2alo2S583;
  if (_M0L3midS585 < _M0L6_2atmpS581) {
    _M0L6_2atmpS1942 = 1ull;
  } else {
    _M0L6_2atmpS1942 = 0ull;
  }
  _M0L2hiS586 = _M0L6_2ahi2S584 + _M0L6_2atmpS1942;
  _M0L3lo2S587 = _M0L5_2aloS580 + _M0L7_2amul0S574;
  _M0L6_2atmpS1940 = _M0L3midS585 + _M0L7_2amul1S576;
  if (_M0L3lo2S587 < _M0L5_2aloS580) {
    _M0L6_2atmpS1941 = 1ull;
  } else {
    _M0L6_2atmpS1941 = 0ull;
  }
  _M0L4mid2S588 = _M0L6_2atmpS1940 + _M0L6_2atmpS1941;
  if (_M0L4mid2S588 < _M0L3midS585) {
    _M0L6_2atmpS1939 = 1ull;
  } else {
    _M0L6_2atmpS1939 = 0ull;
  }
  _M0L3hi2S589 = _M0L2hiS586 + _M0L6_2atmpS1939;
  _M0L6_2atmpS1938 = _M0L1jS591 - 64;
  _M0L6_2atmpS1937 = _M0L6_2atmpS1938 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS590
  = _M0FPB13shiftright128(_M0L4mid2S588, _M0L3hi2S589, _M0L6_2atmpS1937);
  _M0Lm2vmS592 = 0ull;
  if (_M0L7mmShiftS593) {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS580 - _M0L7_2amul0S574;
    uint64_t _M0L6_2atmpS1924 = _M0L3midS585 - _M0L7_2amul1S576;
    uint64_t _M0L6_2atmpS1925;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1923;
    uint64_t _M0L3hi3S596;
    int32_t _M0L6_2atmpS1922;
    int32_t _M0L6_2atmpS1921;
    if (_M0L5_2aloS580 < _M0L3lo3S594) {
      _M0L6_2atmpS1925 = 1ull;
    } else {
      _M0L6_2atmpS1925 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1924 - _M0L6_2atmpS1925;
    if (_M0L3midS585 < _M0L4mid3S595) {
      _M0L6_2atmpS1923 = 1ull;
    } else {
      _M0L6_2atmpS1923 = 0ull;
    }
    _M0L3hi3S596 = _M0L2hiS586 - _M0L6_2atmpS1923;
    _M0L6_2atmpS1922 = _M0L1jS591 - 64;
    _M0L6_2atmpS1921 = _M0L6_2atmpS1922 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid3S595, _M0L3hi3S596, _M0L6_2atmpS1921);
  } else {
    uint64_t _M0L3lo3S597 = _M0L5_2aloS580 + _M0L5_2aloS580;
    uint64_t _M0L6_2atmpS1932 = _M0L3midS585 + _M0L3midS585;
    uint64_t _M0L6_2atmpS1933;
    uint64_t _M0L4mid3S598;
    uint64_t _M0L6_2atmpS1930;
    uint64_t _M0L6_2atmpS1931;
    uint64_t _M0L3hi3S599;
    uint64_t _M0L3lo4S600;
    uint64_t _M0L6_2atmpS1928;
    uint64_t _M0L6_2atmpS1929;
    uint64_t _M0L4mid4S601;
    uint64_t _M0L6_2atmpS1927;
    uint64_t _M0L3hi4S602;
    int32_t _M0L6_2atmpS1926;
    if (_M0L3lo3S597 < _M0L5_2aloS580) {
      _M0L6_2atmpS1933 = 1ull;
    } else {
      _M0L6_2atmpS1933 = 0ull;
    }
    _M0L4mid3S598 = _M0L6_2atmpS1932 + _M0L6_2atmpS1933;
    _M0L6_2atmpS1930 = _M0L2hiS586 + _M0L2hiS586;
    if (_M0L4mid3S598 < _M0L3midS585) {
      _M0L6_2atmpS1931 = 1ull;
    } else {
      _M0L6_2atmpS1931 = 0ull;
    }
    _M0L3hi3S599 = _M0L6_2atmpS1930 + _M0L6_2atmpS1931;
    _M0L3lo4S600 = _M0L3lo3S597 - _M0L7_2amul0S574;
    _M0L6_2atmpS1928 = _M0L4mid3S598 - _M0L7_2amul1S576;
    if (_M0L3lo3S597 < _M0L3lo4S600) {
      _M0L6_2atmpS1929 = 1ull;
    } else {
      _M0L6_2atmpS1929 = 0ull;
    }
    _M0L4mid4S601 = _M0L6_2atmpS1928 - _M0L6_2atmpS1929;
    if (_M0L4mid3S598 < _M0L4mid4S601) {
      _M0L6_2atmpS1927 = 1ull;
    } else {
      _M0L6_2atmpS1927 = 0ull;
    }
    _M0L3hi4S602 = _M0L3hi3S599 - _M0L6_2atmpS1927;
    _M0L6_2atmpS1926 = _M0L1jS591 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid4S601, _M0L3hi4S602, _M0L6_2atmpS1926);
  }
  _M0L6_2atmpS1936 = _M0L1jS591 - 64;
  _M0L6_2atmpS1935 = _M0L6_2atmpS1936 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS603
  = _M0FPB13shiftright128(_M0L3midS585, _M0L2hiS586, _M0L6_2atmpS1935);
  _M0L6_2atmpS1934 = _M0Lm2vmS592;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS603,
                                                .$1 = _M0L2vpS590,
                                                .$2 = _M0L6_2atmpS1934};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS572,
  int32_t _M0L1pS573
) {
  uint64_t _M0L6_2atmpS1920;
  uint64_t _M0L6_2atmpS1919;
  uint64_t _M0L6_2atmpS1918;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1920 = 1ull << (_M0L1pS573 & 63);
  _M0L6_2atmpS1919 = _M0L6_2atmpS1920 - 1ull;
  _M0L6_2atmpS1918 = _M0L5valueS572 & _M0L6_2atmpS1919;
  return _M0L6_2atmpS1918 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  int32_t _M0L6_2atmpS1917;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1917 = _M0FPB10pow5Factor(_M0L5valueS570);
  return _M0L6_2atmpS1917 >= _M0L1pS571;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS565) {
  uint64_t _M0L6_2atmpS1908;
  uint64_t _M0L6_2atmpS1909;
  uint64_t _M0L6_2atmpS1910;
  uint64_t _M0L6_2atmpS1911;
  uint64_t _M0L6_2atmpS1916;
  int32_t _M0L5countS566;
  uint64_t _M0L1vS567;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1908 = _M0L5valueS565 % 5ull;
  if (_M0L6_2atmpS1908 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1909 = _M0L5valueS565 % 25ull;
  if (_M0L6_2atmpS1909 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1910 = _M0L5valueS565 % 125ull;
  if (_M0L6_2atmpS1910 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1911 = _M0L5valueS565 % 625ull;
  if (_M0L6_2atmpS1911 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1916 = _M0L5valueS565 / 625ull;
  _M0L5countS566 = 4;
  _M0L1vS567 = _M0L6_2atmpS1916;
  while (1) {
    if (_M0L1vS567 > 0ull) {
      uint64_t _M0L6_2atmpS1912 = _M0L1vS567 % 5ull;
      int32_t _M0L6_2atmpS1913;
      uint64_t _M0L6_2atmpS1914;
      if (_M0L6_2atmpS1912 != 0ull) {
        return _M0L5countS566;
      }
      _M0L6_2atmpS1913 = _M0L5countS566 + 1;
      _M0L6_2atmpS1914 = _M0L1vS567 / 5ull;
      _M0L5countS566 = _M0L6_2atmpS1913;
      _M0L1vS567 = _M0L6_2atmpS1914;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS569;
      moonbit_string_t _M0L6_2atmpS1915;
      int32_t _result_3300;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS569
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS569, (moonbit_string_t)moonbit_string_literal_14.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS569, _M0L5valueS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1915
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS569);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS569);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_3300 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1915);
      moonbit_decref_cycle_free(_M0L6_2atmpS1915);
      return _result_3300;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS564,
  uint64_t _M0L2hiS562,
  int32_t _M0L4distS563
) {
  int32_t _M0L6_2atmpS1907;
  uint64_t _M0L6_2atmpS1905;
  uint64_t _M0L6_2atmpS1906;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1907 = 64 - _M0L4distS563;
  _M0L6_2atmpS1905 = _M0L2hiS562 << (_M0L6_2atmpS1907 & 63);
  _M0L6_2atmpS1906 = _M0L2loS564 >> (_M0L4distS563 & 63);
  return _M0L6_2atmpS1905 | _M0L6_2atmpS1906;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS552,
  uint64_t _M0L1bS555
) {
  uint64_t _M0L3aLoS551;
  uint64_t _M0L3aHiS553;
  uint64_t _M0L3bLoS554;
  uint64_t _M0L3bHiS556;
  uint64_t _M0L1xS557;
  uint64_t _M0L6_2atmpS1903;
  uint64_t _M0L6_2atmpS1904;
  uint64_t _M0L1yS558;
  uint64_t _M0L6_2atmpS1901;
  uint64_t _M0L6_2atmpS1902;
  uint64_t _M0L1zS559;
  uint64_t _M0L6_2atmpS1899;
  uint64_t _M0L6_2atmpS1900;
  uint64_t _M0L6_2atmpS1897;
  uint64_t _M0L6_2atmpS1898;
  uint64_t _M0L1wS560;
  uint64_t _M0L2loS561;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS551 = _M0L1aS552 & 4294967295ull;
  _M0L3aHiS553 = _M0L1aS552 >> 32;
  _M0L3bLoS554 = _M0L1bS555 & 4294967295ull;
  _M0L3bHiS556 = _M0L1bS555 >> 32;
  _M0L1xS557 = _M0L3aLoS551 * _M0L3bLoS554;
  _M0L6_2atmpS1903 = _M0L3aHiS553 * _M0L3bLoS554;
  _M0L6_2atmpS1904 = _M0L1xS557 >> 32;
  _M0L1yS558 = _M0L6_2atmpS1903 + _M0L6_2atmpS1904;
  _M0L6_2atmpS1901 = _M0L3aLoS551 * _M0L3bHiS556;
  _M0L6_2atmpS1902 = _M0L1yS558 & 4294967295ull;
  _M0L1zS559 = _M0L6_2atmpS1901 + _M0L6_2atmpS1902;
  _M0L6_2atmpS1899 = _M0L3aHiS553 * _M0L3bHiS556;
  _M0L6_2atmpS1900 = _M0L1yS558 >> 32;
  _M0L6_2atmpS1897 = _M0L6_2atmpS1899 + _M0L6_2atmpS1900;
  _M0L6_2atmpS1898 = _M0L1zS559 >> 32;
  _M0L1wS560 = _M0L6_2atmpS1897 + _M0L6_2atmpS1898;
  _M0L2loS561 = _M0L1aS552 * _M0L1bS555;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS561, .$1 = _M0L1wS560};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS549,
  int32_t _M0L4fromS546,
  int32_t _M0L2toS545
) {
  int32_t _M0L3lenS544;
  int32_t _M0L6_2atmpS1896;
  uint16_t* _M0L6bufferS547;
  int32_t _M0L1iS548;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS544 = _M0L2toS545 - _M0L4fromS546;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1896 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS547
  = (uint16_t*)moonbit_make_string(_M0L3lenS544, _M0L6_2atmpS1896);
  _M0L1iS548 = 0;
  while (1) {
    if (_M0L1iS548 < _M0L3lenS544) {
      int32_t _M0L6_2atmpS1894 = _M0L4fromS546 + _M0L1iS548;
      int32_t _M0L6_2atmpS1893;
      int32_t _M0L6_2atmpS1892;
      int32_t _M0L6_2atmpS1895;
      if (
        _M0L6_2atmpS1894 < 0
        || _M0L6_2atmpS1894 >= Moonbit_array_length(_M0L5bytesS549)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1893 = (int32_t)_M0L5bytesS549[_M0L6_2atmpS1894];
      _M0L6_2atmpS1892 = (uint16_t)_M0L6_2atmpS1893;
      if (
        _M0L1iS548 < 0 || _M0L1iS548 >= Moonbit_array_length(_M0L6bufferS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS547[_M0L1iS548] = _M0L6_2atmpS1892;
      _M0L6_2atmpS1895 = _M0L1iS548 + 1;
      _M0L1iS548 = _M0L6_2atmpS1895;
      continue;
    }
    break;
  }
  return _M0L6bufferS547;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS1891;
  uint32_t _M0L6_2atmpS1890;
  uint32_t _M0L6_2atmpS1889;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1891 = _M0L1eS543 * 78913;
  _M0L6_2atmpS1890 = *(uint32_t*)&_M0L6_2atmpS1891;
  _M0L6_2atmpS1889 = _M0L6_2atmpS1890 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1889;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1888;
  uint32_t _M0L6_2atmpS1887;
  uint32_t _M0L6_2atmpS1886;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1888 = _M0L1eS542 * 732923;
  _M0L6_2atmpS1887 = *(uint32_t*)&_M0L6_2atmpS1888;
  _M0L6_2atmpS1886 = _M0L6_2atmpS1887 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1886;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS540,
  int32_t _M0L8exponentS541,
  int32_t _M0L8mantissaS538
) {
  moonbit_string_t _M0L1sS539;
  moonbit_string_t _result_3303;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS538) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L4signS540) {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_16.data;
  } else {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS541) {
    moonbit_string_t _result_3302;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3302
    = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_17.data);
    moonbit_decref_cycle_free(_M0L1sS539);
    return _result_3302;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3303
  = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_18.data);
  moonbit_decref_cycle_free(_M0L1sS539);
  return _result_3303;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1885;
  uint32_t _M0L6_2atmpS1884;
  uint32_t _M0L6_2atmpS1883;
  int32_t _M0L6_2atmpS1882;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1885 = _M0L1eS537 * 1217359;
  _M0L6_2atmpS1884 = *(uint32_t*)&_M0L6_2atmpS1885;
  _M0L6_2atmpS1883 = _M0L6_2atmpS1884 >> 19;
  _M0L6_2atmpS1882 = *(int32_t*)&_M0L6_2atmpS1883;
  return _M0L6_2atmpS1882 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS536) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS536 != _M0L4selfS536) {
    return 0;
  } else if (_M0L4selfS536 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS536 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS536;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS535) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS535 != _M0L4selfS535) {
    return 0ll;
  } else if (_M0L4selfS535 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS535 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS535;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS1878;
  struct _M0TPB5ArrayGfE* _block_3304;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1878 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_3304
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_3304)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _block_3304->$0 = _M0L6_2atmpS1878;
  _block_3304->$1 = _M0L3lenS531;
  return _block_3304;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS1879;
  struct _M0TPB5ArrayGbE* _block_3305;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1879 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_3305
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_3305)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 123, 0);
  _block_3305->$0 = _M0L6_2atmpS1879;
  _block_3305->$1 = _M0L3lenS532;
  return _block_3305;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS1880;
  struct _M0TPB5ArrayGiE* _block_3306;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1880 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_3306
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_3306)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 115, 0);
  _block_3306->$0 = _M0L6_2atmpS1880;
  _block_3306->$1 = _M0L3lenS533;
  return _block_3306;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS534
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1881;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_3307;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1881
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS534, 0);
  _block_3307
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_3307)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 126, 0);
  _block_3307->$0 = _M0L6_2atmpS1881;
  _block_3307->$1 = _M0L3lenS534;
  return _block_3307;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS1876;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1876 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1876)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1876[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS1877;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1877 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS1877)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1877[_M0L5indexS530];
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
  int32_t _M0L3lenS1848;
  moonbit_string_t* _M0L6_2atmpS1850;
  int32_t _M0L6_2atmpS1849;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS1853;
  moonbit_string_t _M0L6_2aoldS3160;
  int32_t _M0L6_2atmpS1854;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1848 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1850 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS1849 = Moonbit_array_length(_M0L6_2atmpS1850);
  moonbit_decref_cycle_free(_M0L6_2atmpS1850);
  if (_M0L3lenS1848 == _M0L6_2atmpS1849) {
    int32_t _M0L3lenS1852 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1851 = _M0L3lenS1852 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS1851);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1853 = _M0L4selfS512->$0;
  _M0L6_2aoldS3160 = (moonbit_string_t)_M0L3bufS1853[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS3160);
  _M0L3bufS1853[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1854 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1854;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS1855;
  struct _M0TUsiE** _M0L6_2atmpS1857;
  int32_t _M0L6_2atmpS1856;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS1860;
  struct _M0TUsiE* _M0L6_2aoldS3161;
  int32_t _M0L6_2atmpS1861;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1855 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1857 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS1856 = Moonbit_array_length(_M0L6_2atmpS1857);
  moonbit_decref_cycle_free(_M0L6_2atmpS1857);
  if (_M0L3lenS1855 == _M0L6_2atmpS1856) {
    int32_t _M0L3lenS1859 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1858 = _M0L3lenS1859 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS1858);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1860 = _M0L4selfS515->$0;
  _M0L6_2aoldS3161 = (struct _M0TUsiE*)_M0L3bufS1860[_M0L6lengthS516];
  if (_M0L6_2aoldS3161) {
    moonbit_decref_cycle_free(_M0L6_2aoldS3161);
  }
  _M0L3bufS1860[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1861 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1861;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS518,
  int32_t _M0L5valueS520
) {
  int32_t _M0L3lenS1862;
  int32_t* _M0L6_2atmpS1864;
  int32_t _M0L6_2atmpS1863;
  int32_t _M0L6lengthS519;
  int32_t* _M0L3bufS1867;
  int32_t _M0L6_2atmpS1868;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1862 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1864 = _M0MPC15array5Array6bufferGiE(_M0L4selfS518);
  _M0L6_2atmpS1863 = Moonbit_array_length(_M0L6_2atmpS1864);
  moonbit_decref_cycle_free(_M0L6_2atmpS1864);
  if (_M0L3lenS1862 == _M0L6_2atmpS1863) {
    int32_t _M0L3lenS1866 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1865 = _M0L3lenS1866 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS518, _M0L6_2atmpS1865);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1867 = _M0L4selfS518->$0;
  _M0L3bufS1867[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1868 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1868;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS521,
  float _M0L5valueS523
) {
  int32_t _M0L3lenS1869;
  float* _M0L6_2atmpS1871;
  int32_t _M0L6_2atmpS1870;
  int32_t _M0L6lengthS522;
  float* _M0L3bufS1874;
  int32_t _M0L6_2atmpS1875;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1869 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1871 = _M0MPC15array5Array6bufferGfE(_M0L4selfS521);
  _M0L6_2atmpS1870 = Moonbit_array_length(_M0L6_2atmpS1871);
  moonbit_decref_cycle_free(_M0L6_2atmpS1871);
  if (_M0L3lenS1869 == _M0L6_2atmpS1870) {
    int32_t _M0L3lenS1873 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS1872 = _M0L3lenS1873 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS521, _M0L6_2atmpS1872);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS1874 = _M0L4selfS521->$0;
  _M0L3bufS1874[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS1875 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS1875;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS1844;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS1844 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS1844, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS1845;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS1845 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS1845, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS1846;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGiE(_M0L4selfS505);
  _M0L3lenS1846 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS1846, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS1847;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGfE(_M0L4selfS509);
  _M0L3lenS1847 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS1847, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS509, _M0L8new__capS510);
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
  moonbit_string_t* _M0L6_2aoldS3162;
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
  _M0L6_2aoldS3162 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3162);
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
  struct _M0TUsiE** _M0L6_2aoldS3163;
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
  _M0L6_2aoldS3163 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3163);
  _M0L4selfS479->$0 = _M0L8new__bufS483;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS485,
  int32_t _M0L13new__capacityS488
) {
  int32_t* _M0L8old__bufS484;
  int32_t _M0L3lenS486;
  int32_t _M0L9copy__lenS487;
  int32_t* _M0L8new__bufS489;
  int32_t* _M0L6_2aoldS3164;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS484, _M0L13new__capacityS488, _M0L9copy__lenS487, 0, 0);
  _M0L6_2aoldS3164 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3164);
  _M0L4selfS485->$0 = _M0L8new__bufS489;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS491,
  int32_t _M0L13new__capacityS494
) {
  float* _M0L8old__bufS490;
  int32_t _M0L3lenS492;
  int32_t _M0L9copy__lenS493;
  float* _M0L8new__bufS495;
  float* _M0L6_2aoldS3165;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS490, _M0L13new__capacityS494, _M0L9copy__lenS493, 0, 0);
  _M0L6_2aoldS3165 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3165);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS1840;
  int32_t _result_3308;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1840 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_3308 = Moonbit_array_length(_M0L6_2atmpS1840);
  moonbit_decref_cycle_free(_M0L6_2atmpS1840);
  return _result_3308;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS1841;
  int32_t _result_3309;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1841 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_3309 = Moonbit_array_length(_M0L6_2atmpS1841);
  moonbit_decref_cycle_free(_M0L6_2atmpS1841);
  return _result_3309;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470
) {
  int32_t* _M0L6_2atmpS1842;
  int32_t _result_3310;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1842 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
  _result_3310 = Moonbit_array_length(_M0L6_2atmpS1842);
  moonbit_decref_cycle_free(_M0L6_2atmpS1842);
  return _result_3310;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS471
) {
  float* _M0L6_2atmpS1843;
  int32_t _result_3311;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1843 = _M0MPC15array5Array6bufferGfE(_M0L4selfS471);
  _result_3311 = Moonbit_array_length(_M0L6_2atmpS1843);
  moonbit_decref_cycle_free(_M0L6_2atmpS1843);
  return _result_3311;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS460) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS452) {
  uint8_t* _M0L8_2afieldS3166;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3166 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3166);
  return _M0L8_2afieldS3166;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS453) {
  float* _M0L8_2afieldS3167;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3167 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3167);
  return _M0L8_2afieldS3167;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS3168;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3168 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3168);
  return _M0L8_2afieldS3168;
}

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS455
) {
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L8_2afieldS3169;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3169 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3169);
  return _M0L8_2afieldS3169;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS456
) {
  moonbit_string_t* _M0L8_2afieldS3170;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3170 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3170);
  return _M0L8_2afieldS3170;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS457
) {
  struct _M0TUsiE** _M0L8_2afieldS3171;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3171 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3171);
  return _M0L8_2afieldS3171;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS458
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS3172;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3172 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3172);
  return _M0L8_2afieldS3172;
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
  int32_t _M0L3endS1838;
  int32_t _M0L5startS1839;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1837;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1830;
  int32_t _M0L6_2atmpS1829;
  int32_t _if__result_3313;
  uint16_t* _M0L4dataS1831;
  int32_t _M0L3lenS1832;
  moonbit_string_t _M0L6_2atmpS1833;
  int32_t _M0L6_2atmpS1834;
  int32_t _M0L3lenS1836;
  int32_t _M0L6_2atmpS1835;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1838 = _M0L3strS448.$2;
  _M0L5startS1839 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1838 - _M0L5startS1839;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1837 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1837 + _M0L8str__lenS447;
  _M0L4dataS1830 = _M0L4selfS450->$0;
  _M0L6_2atmpS1829 = Moonbit_array_length(_M0L4dataS1830);
  if (_M0L8requiredS449 > _M0L6_2atmpS1829) {
    _if__result_3313 = 1;
  } else {
    int32_t _M0L3lenS1828 = _M0L4selfS450->$1;
    _if__result_3313 = _M0L8requiredS449 < _M0L3lenS1828;
  }
  if (_if__result_3313) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1831 = _M0L4selfS450->$0;
  _M0L3lenS1832 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1831);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1833 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1834 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1831, _M0L3lenS1832, _M0L6_2atmpS1833, _M0L6_2atmpS1834, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1831);
  moonbit_decref_cycle_free(_M0L6_2atmpS1833);
  _M0L3lenS1836 = _M0L4selfS450->$1;
  _M0L6_2atmpS1835 = _M0L3lenS1836 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1835;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_3314;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1827;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1826;
  moonbit_string_t _result_3315;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1825 = Moonbit_array_length(_M0L3strS444);
    _if__result_3314 = _M0L3endS443 == _M0L6_2atmpS1825;
  } else {
    _if__result_3314 = 0;
  }
  if (_if__result_3314) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1827 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1827, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1826 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_3315
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1826, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1826);
  return _result_3315;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_3316;
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
      int32_t _M0L6_2atmpS1824 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_3316 = _M0L6_2atmpS1824 <= _M0L3lenS436;
    } else {
      _if__result_3316 = 0;
    }
  } else {
    _if__result_3316 = 0;
  }
  if (_if__result_3316) {
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
  int32_t _M0L6_2atmpS1823;
  int32_t _M0L6_2atmpS1822;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1821;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1823 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1822 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1823;
  _M0L2e1S422 = _M0L6_2atmpS1822 - 1;
  _M0L6_2atmpS1821 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1821 - 1;
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
        int32_t _M0L6_2atmpS1818 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1817 = (int32_t)_M0L6_2atmpS1818;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1817;
        uint32_t _M0L6_2atmpS1813 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1812;
        int32_t _M0L6_2atmpS1814;
        uint32_t _M0L6_2atmpS1816;
        int32_t _M0L6_2atmpS1815;
        int32_t _M0L6_2atmpS1819;
        int32_t _M0L6_2atmpS1820;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1812 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1813);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1812;
        _M0L6_2atmpS1814 = _M0L1jS433 + 1;
        _M0L6_2atmpS1816 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1815 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1816);
        if (
          _M0L6_2atmpS1814 < 0
          || _M0L6_2atmpS1814 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1814] = _M0L6_2atmpS1815;
        _M0L6_2atmpS1819 = _M0L1iS432 + 1;
        _M0L6_2atmpS1820 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1819;
        _M0L1jS433 = _M0L6_2atmpS1820;
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
  int32_t _M0L6_2atmpS1811;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1811 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1811 & 0xff;
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
    int64_t _M0L6_2atmpS1810 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1810;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1807;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1807 = 1;
      } else {
        _M0L6_2atmpS1807 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1807;
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
      int32_t _M0L6_2atmpS1808;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1808 = 1;
      } else {
        _M0L6_2atmpS1808 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1808;
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
      int32_t _M0L6_2atmpS1809;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1809 = 1;
      } else {
        _M0L6_2atmpS1809 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1809;
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
  int32_t _M0L6_2atmpS1806;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1806 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1806;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1783 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1783;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1782 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1781 = 48 + _M0L6_2atmpS1782;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1781;
      int32_t _M0L6_2atmpS1780 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1779 = 48 + _M0L6_2atmpS1780;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1779;
      int32_t _M0L6_2atmpS1778 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1777 = 48 + _M0L6_2atmpS1778;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1777;
      int32_t _M0L6_2atmpS1776 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1775 = 48 + _M0L6_2atmpS1776;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1775;
      int32_t _M0L6_2atmpS1767 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1766 = _M0L6_2atmpS1767 - 4;
      int32_t _M0L6_2atmpS1769;
      int32_t _M0L6_2atmpS1768;
      int32_t _M0L6_2atmpS1771;
      int32_t _M0L6_2atmpS1770;
      int32_t _M0L6_2atmpS1773;
      int32_t _M0L6_2atmpS1772;
      int32_t _M0L6_2atmpS1774;
      _M0L6bufferS381[_M0L6_2atmpS1766] = _M0L6d1__hiS377;
      _M0L6_2atmpS1769 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1768 = _M0L6_2atmpS1769 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1768] = _M0L6d1__loS378;
      _M0L6_2atmpS1771 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1770 = _M0L6_2atmpS1771 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1770] = _M0L6d2__hiS379;
      _M0L6_2atmpS1773 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1772 = _M0L6_2atmpS1773 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1772] = _M0L6d2__loS380;
      _M0L6_2atmpS1774 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1774;
      continue;
    } else {
      int32_t _M0L6_2atmpS1805 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1805;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1792 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1791 = 48 + _M0L6_2atmpS1792;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1791;
          int32_t _M0L6_2atmpS1790 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1789 = 48 + _M0L6_2atmpS1790;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1789;
          int32_t _M0L6_2atmpS1785 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1784 = _M0L6_2atmpS1785 - 2;
          int32_t _M0L6_2atmpS1787;
          int32_t _M0L6_2atmpS1786;
          int32_t _M0L6_2atmpS1788;
          _M0L6bufferS381[_M0L6_2atmpS1784] = _M0L5d__hiS388;
          _M0L6_2atmpS1787 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1786 = _M0L6_2atmpS1787 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1786] = _M0L5d__loS389;
          _M0L6_2atmpS1788 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1788;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1800 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1799 = 48 + _M0L6_2atmpS1800;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1799;
          int32_t _M0L6_2atmpS1798 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1797 = 48 + _M0L6_2atmpS1798;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1797;
          int32_t _M0L6_2atmpS1794 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1793 = _M0L6_2atmpS1794 - 2;
          int32_t _M0L6_2atmpS1796;
          int32_t _M0L6_2atmpS1795;
          _M0L6bufferS381[_M0L6_2atmpS1793] = _M0L5d__hiS391;
          _M0L6_2atmpS1796 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1795 = _M0L6_2atmpS1796 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1795] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1804 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1801 = _M0L6_2atmpS1804 - 1;
          int32_t _M0L6_2atmpS1803 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1802 = (uint16_t)_M0L6_2atmpS1803;
          _M0L6bufferS381[_M0L6_2atmpS1801] = _M0L6_2atmpS1802;
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
  int32_t _M0L6_2atmpS1751;
  int32_t _M0L6_2atmpS1750;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1751 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1750 = _M0L5radixS355 & _M0L6_2atmpS1751;
  if (_M0L6_2atmpS1750 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1758;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1758 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1758;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1757 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1757;
        int32_t _M0L6_2atmpS1754 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1752 = _M0L6_2atmpS1754 - 1;
        int32_t _M0L6_2atmpS1753 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1755;
        uint64_t _M0L6_2atmpS1756;
        _M0L6bufferS361[_M0L6_2atmpS1752] = _M0L6_2atmpS1753;
        _M0L6_2atmpS1755 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1756 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1755;
        _M0L1nS359 = _M0L6_2atmpS1756;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1765 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1765;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1764 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1763 = _M0L1nS367 - _M0L6_2atmpS1764;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1763;
        int32_t _M0L6_2atmpS1761 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1759 = _M0L6_2atmpS1761 - 1;
        int32_t _M0L6_2atmpS1760 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1762;
        _M0L6bufferS361[_M0L6_2atmpS1759] = _M0L6_2atmpS1760;
        _M0L6_2atmpS1762 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1762;
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
  int32_t _M0L6_2atmpS1749;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1749 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1749;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1746 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1746;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1740 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1738 = _M0L6_2atmpS1740 - 2;
      int32_t _M0L6_2atmpS1739 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1743;
      int32_t _M0L6_2atmpS1741;
      int32_t _M0L6_2atmpS1742;
      int32_t _M0L6_2atmpS1744;
      uint64_t _M0L6_2atmpS1745;
      _M0L6bufferS348[_M0L6_2atmpS1738] = _M0L6_2atmpS1739;
      _M0L6_2atmpS1743 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1741 = _M0L6_2atmpS1743 - 1;
      _M0L6_2atmpS1742
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1741] = _M0L6_2atmpS1742;
      _M0L6_2atmpS1744 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1745 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1744;
      _M0L1nS344 = _M0L6_2atmpS1745;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1748 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1748;
      int32_t _M0L6_2atmpS1747 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1747;
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
      uint64_t _M0L6_2atmpS1736 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1737 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1736;
      _M0L5countS341 = _M0L6_2atmpS1737;
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
    int32_t _M0L6_2atmpS1735;
    int32_t _M0L6_2atmpS1734;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1735 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1734 = _M0L6_2atmpS1735 / 4;
    return _M0L6_2atmpS1734 + 1;
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
    int32_t _M0L6_2atmpS1733 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1733;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1730;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1730 = 1;
      } else {
        _M0L6_2atmpS1730 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1730;
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
      int32_t _M0L6_2atmpS1731;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1731 = 1;
      } else {
        _M0L6_2atmpS1731 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1731;
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
      int32_t _M0L6_2atmpS1732;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1732 = 1;
      } else {
        _M0L6_2atmpS1732 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1732;
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
      uint32_t _M0L6_2atmpS1728 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1729 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1728;
      _M0L5countS315 = _M0L6_2atmpS1729;
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
    int32_t _M0L6_2atmpS1727;
    int32_t _M0L6_2atmpS1726;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1727 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1726 = _M0L6_2atmpS1727 / 4;
    return _M0L6_2atmpS1726 + 1;
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
  int32_t _M0L6_2atmpS1725;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1725 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1725;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1702 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1702;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1701 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1700 = 48 + _M0L6_2atmpS1701;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1700;
      int32_t _M0L6_2atmpS1699 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1698 = 48 + _M0L6_2atmpS1699;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1698;
      int32_t _M0L6_2atmpS1697 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1696 = 48 + _M0L6_2atmpS1697;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1696;
      int32_t _M0L6_2atmpS1695 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1694 = 48 + _M0L6_2atmpS1695;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1694;
      int32_t _M0L6_2atmpS1686 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1685 = _M0L6_2atmpS1686 - 4;
      int32_t _M0L6_2atmpS1688;
      int32_t _M0L6_2atmpS1687;
      int32_t _M0L6_2atmpS1690;
      int32_t _M0L6_2atmpS1689;
      int32_t _M0L6_2atmpS1692;
      int32_t _M0L6_2atmpS1691;
      int32_t _M0L6_2atmpS1693;
      _M0L6bufferS294[_M0L6_2atmpS1685] = _M0L6d1__hiS290;
      _M0L6_2atmpS1688 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1687 = _M0L6_2atmpS1688 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1687] = _M0L6d1__loS291;
      _M0L6_2atmpS1690 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1689 = _M0L6_2atmpS1690 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1689] = _M0L6d2__hiS292;
      _M0L6_2atmpS1692 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1691 = _M0L6_2atmpS1692 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1691] = _M0L6d2__loS293;
      _M0L6_2atmpS1693 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1693;
      continue;
    } else {
      int32_t _M0L6_2atmpS1724 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1724;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1711 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1710 = 48 + _M0L6_2atmpS1711;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1710;
          int32_t _M0L6_2atmpS1709 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1708 = 48 + _M0L6_2atmpS1709;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1708;
          int32_t _M0L6_2atmpS1704 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1703 = _M0L6_2atmpS1704 - 2;
          int32_t _M0L6_2atmpS1706;
          int32_t _M0L6_2atmpS1705;
          int32_t _M0L6_2atmpS1707;
          _M0L6bufferS294[_M0L6_2atmpS1703] = _M0L5d__hiS301;
          _M0L6_2atmpS1706 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1705 = _M0L6_2atmpS1706 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1705] = _M0L5d__loS302;
          _M0L6_2atmpS1707 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1707;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1719 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1718 = 48 + _M0L6_2atmpS1719;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1718;
          int32_t _M0L6_2atmpS1717 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1716 = 48 + _M0L6_2atmpS1717;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1716;
          int32_t _M0L6_2atmpS1713 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1712 = _M0L6_2atmpS1713 - 2;
          int32_t _M0L6_2atmpS1715;
          int32_t _M0L6_2atmpS1714;
          _M0L6bufferS294[_M0L6_2atmpS1712] = _M0L5d__hiS304;
          _M0L6_2atmpS1715 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1714 = _M0L6_2atmpS1715 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1714] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1723 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1720 = _M0L6_2atmpS1723 - 1;
          int32_t _M0L6_2atmpS1722 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1721 = (uint16_t)_M0L6_2atmpS1722;
          _M0L6bufferS294[_M0L6_2atmpS1720] = _M0L6_2atmpS1721;
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
  int32_t _M0L6_2atmpS1670;
  int32_t _M0L6_2atmpS1669;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1670 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1669 = _M0L5radixS268 & _M0L6_2atmpS1670;
  if (_M0L6_2atmpS1669 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1677;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1677 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1677;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1676 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1676;
        int32_t _M0L6_2atmpS1673 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1671 = _M0L6_2atmpS1673 - 1;
        int32_t _M0L6_2atmpS1672 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1674;
        uint32_t _M0L6_2atmpS1675;
        _M0L6bufferS274[_M0L6_2atmpS1671] = _M0L6_2atmpS1672;
        _M0L6_2atmpS1674 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1675 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1674;
        _M0L1nS272 = _M0L6_2atmpS1675;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1684 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1684;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1683 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1682 = _M0L1nS280 - _M0L6_2atmpS1683;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1682;
        int32_t _M0L6_2atmpS1680 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1678 = _M0L6_2atmpS1680 - 1;
        int32_t _M0L6_2atmpS1679 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1681;
        _M0L6bufferS274[_M0L6_2atmpS1678] = _M0L6_2atmpS1679;
        _M0L6_2atmpS1681 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1681;
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
  int32_t _M0L6_2atmpS1668;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1668 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1668;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1665 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1665;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1659 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1657 = _M0L6_2atmpS1659 - 2;
      int32_t _M0L6_2atmpS1658 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1662;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1661;
      int32_t _M0L6_2atmpS1663;
      uint32_t _M0L6_2atmpS1664;
      _M0L6bufferS261[_M0L6_2atmpS1657] = _M0L6_2atmpS1658;
      _M0L6_2atmpS1662 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1660 = _M0L6_2atmpS1662 - 1;
      _M0L6_2atmpS1661
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1660] = _M0L6_2atmpS1661;
      _M0L6_2atmpS1663 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1664 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1663;
      _M0L1nS257 = _M0L6_2atmpS1664;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1667 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1667;
      int32_t _M0L6_2atmpS1666 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1666;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1656;
  moonbit_string_t _result_3330;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1656
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1656);
  if (_M0L6_2atmpS1656.$1) {
    moonbit_decref(_M0L6_2atmpS1656.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_3330 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_3330;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1653;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1653 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1653);
  moonbit_decref_cycle_free(_M0L6_2atmpS1653);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1654;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1654 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1654);
  moonbit_decref_cycle_free(_M0L6_2atmpS1654);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1655;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1655 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1655);
  moonbit_decref_cycle_free(_M0L6_2atmpS1655);
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
  moonbit_string_t _M0L8_2afieldS3173;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS3173 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3173);
  return _M0L8_2afieldS3173;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1652;
  int64_t _M0L6_2atmpS1651;
  struct _M0TPC16string10StringView _M0L6_2atmpS1650;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1652 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1651 = (int64_t)_M0L6_2atmpS1652;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1650
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1651);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1650);
  moonbit_decref_cycle_free(_M0L6_2atmpS1650.$0);
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
  int32_t _M0L6_2atmpS1634;
  int32_t _if__result_3331;
  int32_t _M0L6_2atmpS1642;
  int32_t _if__result_3332;
  int32_t _M0L6_2atmpS1644;
  int32_t _M0L6_2atmpS1645;
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
  _M0L6_2atmpS1634 = _M0Lm2loS236;
  if (_M0L6_2atmpS1634 > 0) {
    int32_t _M0L6_2atmpS1633 = _M0Lm2loS236;
    if (_M0L6_2atmpS1633 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1632 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1631 = _M0L4selfS235[_M0L6_2atmpS1632];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1631)) {
        int32_t _M0L6_2atmpS1630 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1629 = _M0L6_2atmpS1630 - 1;
        int32_t _M0L6_2atmpS1628 = _M0L4selfS235[_M0L6_2atmpS1629];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3331
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1628);
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
    int32_t _M0L6_2atmpS1635 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1635 + 1;
  }
  _M0L6_2atmpS1642 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1642 > 0) {
    int32_t _M0L6_2atmpS1641 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1641 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1640 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1639 = _M0L4selfS235[_M0L6_2atmpS1640];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1639)) {
        int32_t _M0L6_2atmpS1638 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1637 = _M0L6_2atmpS1638 - 1;
        int32_t _M0L6_2atmpS1636 = _M0L4selfS235[_M0L6_2atmpS1637];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3332
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1636);
      } else {
        _if__result_3332 = 0;
      }
    } else {
      _if__result_3332 = 0;
    }
  } else {
    _if__result_3332 = 0;
  }
  if (_if__result_3332) {
    int32_t _M0L6_2atmpS1643 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1643 - 1;
  }
  _M0L6_2atmpS1644 = _M0Lm2loS236;
  _M0L6_2atmpS1645 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1644 >= _M0L6_2atmpS1645) {
    int32_t _M0L6_2atmpS1646 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1647 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1646,
                                                 .$2 = _M0L6_2atmpS1647};
  } else {
    int32_t _M0L6_2atmpS1648 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1649 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1648,
                                                 .$2 = _M0L6_2atmpS1649};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1627;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1627
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1627);
  if (_M0L6_2atmpS1627.$1) {
    moonbit_decref(_M0L6_2atmpS1627.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1626;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1626
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1626);
  if (_M0L6_2atmpS1626.$1) {
    moonbit_decref(_M0L6_2atmpS1626.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1625;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1625 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1625;
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
  int32_t _M0L6_2atmpS1624;
  struct _M0TPC16string10StringView _M0L6_2atmpS1622;
  struct _M0TPB6Logger _M0L6_2atmpS1623;
  moonbit_string_t _result_3333;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1624 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1622
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1624
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1623
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1622, _M0L6_2atmpS1623, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1622.$0);
  if (_M0L6_2atmpS1623.$1) {
    moonbit_decref(_M0L6_2atmpS1623.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3333 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_3333;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1620;
  int32_t _M0L5startS1621;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1620 = _M0L4selfS218.$2;
  _M0L5startS1621 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1620 - _M0L5startS1621;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 136, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1617;
    int32_t _M0L5startS1619;
    int32_t _M0L6_2atmpS1618;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1601;
    int32_t _M0L6_2atmpS1602;
    int32_t _M0L6_2atmpS1603;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1617 = _M0L4selfS218.$0;
    _M0L5startS1619 = _M0L4selfS218.$1;
    _M0L6_2atmpS1618 = _M0L5startS1619 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1617[_M0L6_2atmpS1618];
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
        int32_t _M0L6_2atmpS1604;
        int32_t _M0L6_2atmpS1605;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1604 = _M0L1iS220 + 1;
        _M0L6_2atmpS1605 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1604;
        _M0L3segS221 = _M0L6_2atmpS1605;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1606;
        int32_t _M0L6_2atmpS1607;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1606 = _M0L1iS220 + 1;
        _M0L6_2atmpS1607 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1606;
        _M0L3segS221 = _M0L6_2atmpS1607;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1608;
        int32_t _M0L6_2atmpS1609;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1608 = _M0L1iS220 + 1;
        _M0L6_2atmpS1609 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1608;
        _M0L3segS221 = _M0L6_2atmpS1609;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1610;
        int32_t _M0L6_2atmpS1611;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1610 = _M0L1iS220 + 1;
        _M0L6_2atmpS1611 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1610;
        _M0L3segS221 = _M0L6_2atmpS1611;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1613;
          moonbit_string_t _M0L6_2atmpS1612;
          int32_t _M0L6_2atmpS1614;
          int32_t _M0L6_2atmpS1615;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
          _M0L6_2atmpS1613 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1612 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1613);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1612);
          moonbit_decref_cycle_free(_M0L6_2atmpS1612);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1614 = _M0L1iS220 + 1;
          _M0L6_2atmpS1615 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1614;
          _M0L3segS221 = _M0L6_2atmpS1615;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1616 = _M0L1iS220 + 1;
          int32_t _tmp_3336 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1616;
          _M0L3segS221 = _tmp_3336;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_3335;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1601 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1601);
    _M0L6_2atmpS1602 = _M0L1iS220 + 1;
    _M0L6_2atmpS1603 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1602;
    _M0L3segS221 = _M0L6_2atmpS1603;
    continue;
    joinlet_3335:;
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
    int64_t _M0L6_2atmpS1600 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1599;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1599
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1600);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1599);
    moonbit_decref_cycle_free(_M0L6_2atmpS1599.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1597;
  int32_t _M0L5startS1598;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1575;
  int32_t _if__result_3337;
  int32_t _M0L6_2atmpS1585;
  int32_t _if__result_3338;
  int32_t _M0L6_2atmpS1587;
  int32_t _M0L6_2atmpS1588;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1597 = _M0L4selfS201.$2;
  _M0L5startS1598 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1597 - _M0L5startS1598;
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
  _M0L6_2atmpS1575 = _M0Lm2loS202;
  if (_M0L6_2atmpS1575 > 0) {
    int32_t _M0L6_2atmpS1574 = _M0Lm2loS202;
    if (_M0L6_2atmpS1574 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1573 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1572 = _M0L4baseS209 + _M0L6_2atmpS1573;
      int32_t _M0L6_2atmpS1571 = _M0L3strS208[_M0L6_2atmpS1572];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1571)) {
        int32_t _M0L6_2atmpS1570 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1569 = _M0L4baseS209 + _M0L6_2atmpS1570;
        int32_t _M0L6_2atmpS1568 = _M0L6_2atmpS1569 - 1;
        int32_t _M0L6_2atmpS1567 = _M0L3strS208[_M0L6_2atmpS1568];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3337
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1567);
      } else {
        _if__result_3337 = 0;
      }
    } else {
      _if__result_3337 = 0;
    }
  } else {
    _if__result_3337 = 0;
  }
  if (_if__result_3337) {
    int32_t _M0L6_2atmpS1576 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1576 + 1;
  }
  _M0L6_2atmpS1585 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1585 > 0) {
    int32_t _M0L6_2atmpS1584 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1584 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1583 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1582 = _M0L4baseS209 + _M0L6_2atmpS1583;
      int32_t _M0L6_2atmpS1581 = _M0L3strS208[_M0L6_2atmpS1582];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1581)) {
        int32_t _M0L6_2atmpS1580 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1579 = _M0L4baseS209 + _M0L6_2atmpS1580;
        int32_t _M0L6_2atmpS1578 = _M0L6_2atmpS1579 - 1;
        int32_t _M0L6_2atmpS1577 = _M0L3strS208[_M0L6_2atmpS1578];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3338
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1577);
      } else {
        _if__result_3338 = 0;
      }
    } else {
      _if__result_3338 = 0;
    }
  } else {
    _if__result_3338 = 0;
  }
  if (_if__result_3338) {
    int32_t _M0L6_2atmpS1586 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1586 - 1;
  }
  _M0L6_2atmpS1587 = _M0Lm2loS202;
  _M0L6_2atmpS1588 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1587 >= _M0L6_2atmpS1588) {
    int32_t _M0L6_2atmpS1592 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1589 = _M0L4baseS209 + _M0L6_2atmpS1592;
    int32_t _M0L6_2atmpS1591 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1590 = _M0L4baseS209 + _M0L6_2atmpS1591;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1589,
                                                 .$2 = _M0L6_2atmpS1590};
  } else {
    int32_t _M0L6_2atmpS1596 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1593 = _M0L4baseS209 + _M0L6_2atmpS1596;
    int32_t _M0L6_2atmpS1595 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1594 = _M0L4baseS209 + _M0L6_2atmpS1595;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1593,
                                                 .$2 = _M0L6_2atmpS1594};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1564;
  int32_t _M0L6_2atmpS1563;
  int32_t _M0L6_2atmpS1566;
  int32_t _M0L6_2atmpS1565;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1562;
  moonbit_string_t _result_3339;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1564 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1563
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1564);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1563);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1566 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1565
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1566);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1565);
  _M0L6_2atmpS1562 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3339 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1562);
  moonbit_decref_cycle_free(_M0L6_2atmpS1562);
  return _result_3339;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1559;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1559 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1559);
  } else {
    int32_t _M0L6_2atmpS1561;
    int32_t _M0L6_2atmpS1560;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1561 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1560 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1561, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1560);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1557;
  int32_t _M0L6_2atmpS1558;
  int32_t _M0L6_2atmpS1556;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1557 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1558 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1556 = _M0L6_2atmpS1557 - _M0L6_2atmpS1558;
  return _M0L6_2atmpS1556 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1554;
  int32_t _M0L6_2atmpS1555;
  int32_t _M0L6_2atmpS1553;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1554 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1555 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1553 = _M0L6_2atmpS1554 % _M0L6_2atmpS1555;
  return _M0L6_2atmpS1553 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1551;
  int32_t _M0L6_2atmpS1552;
  int32_t _M0L6_2atmpS1550;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1551 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1552 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1550 = _M0L6_2atmpS1551 / _M0L6_2atmpS1552;
  return _M0L6_2atmpS1550 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1548;
  int32_t _M0L6_2atmpS1549;
  int32_t _M0L6_2atmpS1547;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1548 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1549 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1547 = _M0L6_2atmpS1548 + _M0L6_2atmpS1549;
  return _M0L6_2atmpS1547 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1546;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1546 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1546;
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
  int32_t _M0L3lenS1545;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1540;
  int32_t _M0L6_2atmpS1539;
  int32_t _if__result_3340;
  uint16_t* _M0L4dataS1541;
  int32_t _M0L3lenS1542;
  int32_t _M0L3lenS1544;
  int32_t _M0L6_2atmpS1543;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1545 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1545 + _M0L8str__lenS182;
  _M0L4dataS1540 = _M0L4selfS185->$0;
  _M0L6_2atmpS1539 = Moonbit_array_length(_M0L4dataS1540);
  if (_M0L8requiredS184 > _M0L6_2atmpS1539) {
    _if__result_3340 = 1;
  } else {
    int32_t _M0L3lenS1538 = _M0L4selfS185->$1;
    _if__result_3340 = _M0L8requiredS184 < _M0L3lenS1538;
  }
  if (_if__result_3340) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1541 = _M0L4selfS185->$0;
  _M0L3lenS1542 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1541);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1541, _M0L3lenS1542, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1541);
  _M0L3lenS1544 = _M0L4selfS185->$1;
  _M0L6_2atmpS1543 = _M0L3lenS1544 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1543;
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
      int32_t _M0L6_2atmpS1535 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1536;
      int32_t _M0L6_2atmpS1537;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1535;
      _M0L6_2atmpS1536 = _M0L1iS176 + 1;
      _M0L6_2atmpS1537 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1536;
      _M0L1jS177 = _M0L6_2atmpS1537;
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
    int32_t _M0L3lenS1506 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1508 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1507 = Moonbit_array_length(_M0L4dataS1508);
    uint16_t* _M0L4dataS1511;
    int32_t _M0L3lenS1512;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L3lenS1515;
    int32_t _M0L6_2atmpS1514;
    if (_M0L3lenS1506 >= _M0L6_2atmpS1507) {
      int32_t _M0L3lenS1510 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1509 = _M0L3lenS1510 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1509);
    }
    _M0L4dataS1511 = _M0L4selfS171->$0;
    _M0L3lenS1512 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1511);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1513 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1512 < 0
      || _M0L3lenS1512 >= Moonbit_array_length(_M0L4dataS1511)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1511[_M0L3lenS1512] = _M0L6_2atmpS1513;
    moonbit_decref_cycle_free(_M0L4dataS1511);
    _M0L3lenS1515 = _M0L4selfS171->$1;
    _M0L6_2atmpS1514 = _M0L3lenS1515 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1514;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1519 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1517 = Moonbit_array_length(_M0L4dataS1519);
    int32_t _M0L3lenS1518 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1516 = _M0L6_2atmpS1517 - _M0L3lenS1518;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1522;
    int32_t _M0L3lenS1523;
    uint32_t _M0L6_2atmpS1526;
    uint32_t _M0L6_2atmpS1525;
    int32_t _M0L6_2atmpS1524;
    uint16_t* _M0L4dataS1527;
    int32_t _M0L3lenS1532;
    int32_t _M0L6_2atmpS1528;
    uint32_t _M0L6_2atmpS1531;
    uint32_t _M0L6_2atmpS1530;
    int32_t _M0L6_2atmpS1529;
    int32_t _M0L3lenS1534;
    int32_t _M0L6_2atmpS1533;
    if (_M0L6_2atmpS1516 < 2) {
      int32_t _M0L3lenS1521 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1520 = _M0L3lenS1521 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1520);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1522 = _M0L4selfS171->$0;
    _M0L3lenS1523 = _M0L4selfS171->$1;
    _M0L6_2atmpS1526 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1525 = 55296u + _M0L6_2atmpS1526;
    moonbit_incref_cycle_free(_M0L4dataS1522);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1524 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1525);
    if (
      _M0L3lenS1523 < 0
      || _M0L3lenS1523 >= Moonbit_array_length(_M0L4dataS1522)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1522[_M0L3lenS1523] = _M0L6_2atmpS1524;
    moonbit_decref_cycle_free(_M0L4dataS1522);
    _M0L4dataS1527 = _M0L4selfS171->$0;
    _M0L3lenS1532 = _M0L4selfS171->$1;
    _M0L6_2atmpS1528 = _M0L3lenS1532 + 1;
    _M0L6_2atmpS1531 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1530 = 56320u + _M0L6_2atmpS1531;
    moonbit_incref_cycle_free(_M0L4dataS1527);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1529 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1530);
    if (
      _M0L6_2atmpS1528 < 0
      || _M0L6_2atmpS1528 >= Moonbit_array_length(_M0L4dataS1527)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1527[_M0L6_2atmpS1528] = _M0L6_2atmpS1529;
    moonbit_decref_cycle_free(_M0L4dataS1527);
    _M0L3lenS1534 = _M0L4selfS171->$1;
    _M0L6_2atmpS1533 = _M0L3lenS1534 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1533;
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
  uint16_t* _M0L4dataS1505;
  int32_t _M0L6_2atmpS1503;
  int32_t _M0L3lenS1504;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1500;
  int32_t _M0L6_2atmpS1501;
  int32_t _M0L3lenS1502;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS3174;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1505 = _M0L4selfS166->$0;
  _M0L6_2atmpS1503 = Moonbit_array_length(_M0L4dataS1505);
  _M0L3lenS1504 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1503, _M0L3lenS1504, _M0L8requiredS167);
  _M0L4dataS1500 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1500);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1501 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1502 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1500, _M0L13new__capacityS165, _M0L6_2atmpS1501, _M0L3lenS1502, 0, 0);
  _M0L6_2aoldS3174 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3174);
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
  int32_t _M0L6_2atmpS1499;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1499 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1499;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1498;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1498 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1498;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1489;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1489 = _M0L4selfS155->$1;
  if (_M0L3lenS1489 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1490 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1492 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1491 = Moonbit_array_length(_M0L4dataS1492);
    if (_M0L3lenS1490 == _M0L6_2atmpS1491) {
      uint16_t* _M0L4dataS1493 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1493);
      return _M0L4dataS1493;
    } else {
      uint16_t* _M0L4dataS1494 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1495 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1496;
      int32_t _M0L3lenS1497;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1494);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1496 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1497 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1494, _M0L3lenS1495, _M0L6_2atmpS1496, _M0L3lenS1497, 0, 0);
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
  int32_t _if__result_3343;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1485 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1486 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1485 <= _M0L6_2atmpS1486) {
            int32_t _M0L6_2atmpS1484 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_3343 = _M0L6_2atmpS1484 <= _M0L13allocate__lenS148;
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
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1488;
    moonbit_string_t _M0L6_2atmpS1487;
    uint16_t* _result_3344;
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
    _M0L6_2atmpS1488 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1488);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1487
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_3344 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1487);
    moonbit_decref_cycle_free(_M0L6_2atmpS1487);
    return _result_3344;
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
  struct _M0TPB13StringBuilder* _block_3345;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1483 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1483 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_3345
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_3345)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 141, 0);
  _block_3345->$0 = _M0L4dataS140;
  _block_3345->$1 = 0;
  return _block_3345;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1482;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1482 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1482;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_3346;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1463 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1464;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1464
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1463 <= _M0L6_2atmpS1464) {
            int32_t _M0L6_2atmpS1462 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_3346 = _M0L6_2atmpS1462 <= _M0L13allocate__lenS113;
          } else {
            _if__result_3346 = 0;
          }
        } else {
          _if__result_3346 = 0;
        }
      } else {
        _if__result_3346 = 0;
      }
    } else {
      _if__result_3346 = 0;
    }
  } else {
    _if__result_3346 = 0;
  }
  if (_if__result_3346) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1466;
    moonbit_string_t _M0L6_2atmpS1465;
    moonbit_string_t* _result_3347;
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
    _M0L6_2atmpS1466 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1466);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1465
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3347
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1465);
    moonbit_decref_cycle_free(_M0L6_2atmpS1465);
    return _result_3347;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_3348;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1468 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1469;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1469
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1468 <= _M0L6_2atmpS1469) {
            int32_t _M0L6_2atmpS1467 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_3348 = _M0L6_2atmpS1467 <= _M0L13allocate__lenS119;
          } else {
            _if__result_3348 = 0;
          }
        } else {
          _if__result_3348 = 0;
        }
      } else {
        _if__result_3348 = 0;
      }
    } else {
      _if__result_3348 = 0;
    }
  } else {
    _if__result_3348 = 0;
  }
  if (_if__result_3348) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1471;
    moonbit_string_t _M0L6_2atmpS1470;
    struct _M0TUsiE** _result_3349;
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
    _M0L6_2atmpS1471 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1471);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1470
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3349
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1470);
    moonbit_decref_cycle_free(_M0L6_2atmpS1470);
    return _result_3349;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_3350;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1473 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1474;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1474
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1473 <= _M0L6_2atmpS1474) {
            int32_t _M0L6_2atmpS1472 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_3350 = _M0L6_2atmpS1472 <= _M0L13allocate__lenS125;
          } else {
            _if__result_3350 = 0;
          }
        } else {
          _if__result_3350 = 0;
        }
      } else {
        _if__result_3350 = 0;
      }
    } else {
      _if__result_3350 = 0;
    }
  } else {
    _if__result_3350 = 0;
  }
  if (_if__result_3350) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1476;
    moonbit_string_t _M0L6_2atmpS1475;
    int32_t* _result_3351;
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
    _M0L6_2atmpS1476 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1476);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1475
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3351
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1475);
    moonbit_decref_cycle_free(_M0L6_2atmpS1475);
    return _result_3351;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_3352;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1478 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1479;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1479
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1478 <= _M0L6_2atmpS1479) {
            int32_t _M0L6_2atmpS1477 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_3352 = _M0L6_2atmpS1477 <= _M0L13allocate__lenS131;
          } else {
            _if__result_3352 = 0;
          }
        } else {
          _if__result_3352 = 0;
        }
      } else {
        _if__result_3352 = 0;
      }
    } else {
      _if__result_3352 = 0;
    }
  } else {
    _if__result_3352 = 0;
  }
  if (_if__result_3352) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1481;
    moonbit_string_t _M0L6_2atmpS1480;
    float* _result_3353;
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
    _M0L6_2atmpS1481 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1481);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1480
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3353
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1480);
    moonbit_decref_cycle_free(_M0L6_2atmpS1480);
    return _result_3353;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1459;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1459
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1459);
  if (_M0L6_2atmpS1459.$1) {
    moonbit_decref(_M0L6_2atmpS1459.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1460;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1460
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1460);
  if (_M0L6_2atmpS1460.$1) {
    moonbit_decref(_M0L6_2atmpS1460.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1461;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1461
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1461);
  if (_M0L6_2atmpS1461.$1) {
    moonbit_decref(_M0L6_2atmpS1461.$1);
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  int32_t* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS96);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  float* _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS102);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  int32_t* _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  float* _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82, sizeof(float));
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
        int32_t _M0L6_2atmpS1414 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1416 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1415;
        int32_t _M0L6_2atmpS1417;
        if (
          _M0L6_2atmpS1416 < 0
          || _M0L6_2atmpS1416 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1415 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1416];
        if (
          _M0L6_2atmpS1414 < 0
          || _M0L6_2atmpS1414 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1414] = _M0L6_2atmpS1415;
        _M0L6_2atmpS1417 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1417;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1422 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1422;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1418 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1420 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1419;
        int32_t _M0L6_2atmpS1421;
        if (
          _M0L6_2atmpS1420 < 0
          || _M0L6_2atmpS1420 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1419 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1420];
        if (
          _M0L6_2atmpS1418 < 0
          || _M0L6_2atmpS1418 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1418] = _M0L6_2atmpS1419;
        _M0L6_2atmpS1421 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1421;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  moonbit_string_t* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS1423 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1425 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1424;
        moonbit_string_t _M0L6_2aoldS3175;
        int32_t _M0L6_2atmpS1426;
        if (
          _M0L6_2atmpS1425 < 0
          || _M0L6_2atmpS1425 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1424 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1425];
        if (
          _M0L6_2atmpS1423 < 0
          || _M0L6_2atmpS1423 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3175 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1423];
        moonbit_incref_cycle_free(_M0L6_2atmpS1424);
        moonbit_decref_cycle_free(_M0L6_2aoldS3175);
        _M0L3dstS27[_M0L6_2atmpS1423] = _M0L6_2atmpS1424;
        _M0L6_2atmpS1426 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1426;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1431 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1431;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1427 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1429 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1428;
        moonbit_string_t _M0L6_2aoldS3176;
        int32_t _M0L6_2atmpS1430;
        if (
          _M0L6_2atmpS1429 < 0
          || _M0L6_2atmpS1429 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1428 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1429];
        if (
          _M0L6_2atmpS1427 < 0
          || _M0L6_2atmpS1427 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3176 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1427];
        moonbit_incref_cycle_free(_M0L6_2atmpS1428);
        moonbit_decref_cycle_free(_M0L6_2aoldS3176);
        _M0L3dstS27[_M0L6_2atmpS1427] = _M0L6_2atmpS1428;
        _M0L6_2atmpS1430 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1430;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  struct _M0TUsiE** _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1432 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1434 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1433;
        struct _M0TUsiE* _M0L6_2aoldS3177;
        int32_t _M0L6_2atmpS1435;
        if (
          _M0L6_2atmpS1434 < 0
          || _M0L6_2atmpS1434 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1433 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1434];
        if (
          _M0L6_2atmpS1432 < 0
          || _M0L6_2atmpS1432 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3177 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1432];
        if (_M0L6_2atmpS1433) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1433);
        }
        if (_M0L6_2aoldS3177) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3177);
        }
        _M0L3dstS36[_M0L6_2atmpS1432] = _M0L6_2atmpS1433;
        _M0L6_2atmpS1435 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1435;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1440 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1440;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1436 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1438 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1437;
        struct _M0TUsiE* _M0L6_2aoldS3178;
        int32_t _M0L6_2atmpS1439;
        if (
          _M0L6_2atmpS1438 < 0
          || _M0L6_2atmpS1438 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1437 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1438];
        if (
          _M0L6_2atmpS1436 < 0
          || _M0L6_2atmpS1436 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3178 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1436];
        if (_M0L6_2atmpS1437) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1437);
        }
        if (_M0L6_2aoldS3178) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3178);
        }
        _M0L3dstS36[_M0L6_2atmpS1436] = _M0L6_2atmpS1437;
        _M0L6_2atmpS1439 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1439;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  int32_t* _M0L3srcS46,
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
        int32_t _M0L6_2atmpS1441 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1443 = _M0L11src__offsetS48 + _M0L1iS49;
        int32_t _M0L6_2atmpS1442;
        int32_t _M0L6_2atmpS1444;
        if (
          _M0L6_2atmpS1443 < 0
          || _M0L6_2atmpS1443 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1442 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1443];
        if (
          _M0L6_2atmpS1441 < 0
          || _M0L6_2atmpS1441 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1441] = _M0L6_2atmpS1442;
        _M0L6_2atmpS1444 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1444;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1449 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1449;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1445 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1447 = _M0L11src__offsetS48 + _M0L1iS52;
        int32_t _M0L6_2atmpS1446;
        int32_t _M0L6_2atmpS1448;
        if (
          _M0L6_2atmpS1447 < 0
          || _M0L6_2atmpS1447 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1446 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1447];
        if (
          _M0L6_2atmpS1445 < 0
          || _M0L6_2atmpS1445 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1445] = _M0L6_2atmpS1446;
        _M0L6_2atmpS1448 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1448;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  float* _M0L3srcS55,
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
        int32_t _M0L6_2atmpS1450 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1452 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS1451;
        int32_t _M0L6_2atmpS1453;
        if (
          _M0L6_2atmpS1452 < 0
          || _M0L6_2atmpS1452 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1451 = (float)_M0L3srcS55[_M0L6_2atmpS1452];
        if (
          _M0L6_2atmpS1450 < 0
          || _M0L6_2atmpS1450 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1450] = _M0L6_2atmpS1451;
        _M0L6_2atmpS1453 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1453;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1458 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1458;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1454 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1456 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS1455;
        int32_t _M0L6_2atmpS1457;
        if (
          _M0L6_2atmpS1456 < 0
          || _M0L6_2atmpS1456 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1455 = (float)_M0L3srcS55[_M0L6_2atmpS1456];
        if (
          _M0L6_2atmpS1454 < 0
          || _M0L6_2atmpS1454 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1454] = _M0L6_2atmpS1455;
        _M0L6_2atmpS1457 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1457;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS17) {
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1382) {
  switch (Moonbit_object_tag(_M0L4_2aeS1382)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1382);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1409,
  struct _M0TPB4Show _M0L8_2aparamS1408
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1407 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1409;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1407, _M0L8_2aparamS1408);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1406,
  struct _M0TPB4Show _M0L8_2aparamS1405
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1404 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1406;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1404, _M0L8_2aparamS1405);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1403,
  int32_t _M0L8_2aparamS1402
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1401 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1403;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1401, _M0L8_2aparamS1402);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1400,
  struct _M0TPC16string10StringView _M0L8_2aparamS1399
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1398 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1400;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1398, _M0L8_2aparamS1399);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1397,
  moonbit_string_t _M0L8_2aparamS1394,
  int32_t _M0L8_2aparamS1395,
  int32_t _M0L8_2aparamS1396
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1393 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1397;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1393, _M0L8_2aparamS1394, _M0L8_2aparamS1395, _M0L8_2aparamS1396);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1392,
  moonbit_string_t _M0L8_2aparamS1391
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1390 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1392;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1390, _M0L8_2aparamS1391);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_3364 = 9218868437227405311ll;
  int64_t _tmp_3365;
  int64_t _tmp_3366;
  int64_t _tmp_3367;
  int64_t _tmp_3368;
  _M0FPB18double__max__value = *(double*)&_tmp_3364;
  _tmp_3365 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_3365;
  _tmp_3366 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_3366;
  _tmp_3367 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_3367;
  _tmp_3368 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_3368;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1413;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1375;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1376;
  int32_t _M0L7_2abindS1377;
  struct _M0TUsiE** _M0L7_2abindS1378;
  int32_t _M0L6_2acntS3185;
  int32_t _M0L2__S1379;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1413
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1375
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1375)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 144, 0);
  _M0L12async__testsS1375->$0 = _M0L6_2atmpS1413;
  _M0L12async__testsS1375->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1376
  = _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1377 = _M0L7_2abindS1376->$1;
  _M0L7_2abindS1378 = _M0L7_2abindS1376->$0;
  _M0L6_2acntS3185
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1376));
  if (_M0L6_2acntS3185 > 1) {
    int32_t _M0L11_2anew__cntS3186 = _M0L6_2acntS3185 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1376), _M0L11_2anew__cntS3186);
    moonbit_incref_cycle_free(_M0L7_2abindS1378);
  } else if (_M0L6_2acntS3185 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1376);
  }
  _M0L2__S1379 = 0;
  while (1) {
    if (_M0L2__S1379 < _M0L7_2abindS1377) {
      struct _M0TUsiE* _M0L3argS1380 =
        (struct _M0TUsiE*)_M0L7_2abindS1378[_M0L2__S1379];
      moonbit_string_t _M0L6_2atmpS1410 = _M0L3argS1380->$0;
      int32_t _M0L6_2atmpS1411 = _M0L3argS1380->$1;
      int32_t _M0L6_2atmpS1412;
      moonbit_incref_cycle_free(_M0L6_2atmpS1410);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1375, _M0L6_2atmpS1410, _M0L6_2atmpS1411);
      moonbit_decref_cycle_free(_M0L6_2atmpS1410);
      _M0L6_2atmpS1412 = _M0L2__S1379 + 1;
      _M0L2__S1379 = _M0L6_2atmpS1412;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1378);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_receptor\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples32tripod__receptor__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1375);
  moonbit_decref_cycle_free(_M0L12async__testsS1375);
  moonbit_flush_cycles();
  return 0;
}