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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx;

struct _M0TUddE;

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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
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

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray {
  float $0;
  struct _M0TPB5ArrayGbE* $1;
  struct _M0TPB5ArrayGfE* $2;
  int32_t $3;
  float $4;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $5;
  
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

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
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

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
};

struct _M0TPB5ArrayGiE {
  int32_t* $0;
  int32_t $1;
  
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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx {
  struct _M0TP26RiantR8snn__mbt4AdEx* $0;
  struct _M0TP26RiantR8snn__mbt4AdEx* $1;
  moonbit_string_t $2;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $3;
  
};

struct _M0TUddE {
  double $0;
  double $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
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

int32_t _M0FP26RiantR8snn__mbt22forward__adex__synapse(
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*
);

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0MP26RiantR8snn__mbt18SpikingSynapseAdEx6random(
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray*
);

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0MP26RiantR8snn__mbt20CurrentStimulusArray11new_2einner(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
);

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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

int32_t _M0MPC16double6Double7is__inf(double);

int32_t _M0MPC16double6Double7is__nan(double);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

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

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_20 =
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
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_12 =
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
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 104, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_26 =
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
} const moonbit_string_literal_23 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[112]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 111, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 115, 111, 100, 121, 107, 115, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_11 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 115, 111, 100, 121, 107, 115, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 
    112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_21 =
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
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916
  };

uint32_t const moonbit_layout_table_data[101] =
  {
    sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904)
    / 4, 1,
    offsetof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904, $1)
    / 4
    * 2,
    sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909)
    / 4, 1,
    offsetof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4AdEx) / 4, 17,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $16) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $17) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $3) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray) / 4, 
    3,
    offsetof(struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray, $5) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

double _M0FPB18double__max__value;

double _M0FPB18double__min__value;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1916
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS937,
  moonbit_string_t _M0L8filenameS906,
  int32_t _M0L5indexS908
) {
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904* _closure_1947;
  struct _M0TWEu* _M0L13handle__startS904;
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909* _closure_1948;
  struct _M0TWssbEu* _M0L14handle__resultS909;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS916;
  void* _M0L11_2atry__errS931;
  struct moonbit_result_0 _tmp_1950;
  int32_t _handle__error__result_1951;
  int32_t _M0L6_2atmpS1904;
  void* _M0L3errS932;
  moonbit_string_t _M0L4nameS934;
  struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS935;
  moonbit_string_t _M0L7_2anameS936;
  int32_t _M0L6_2acntS1941;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS906);
  _closure_1947
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904*)moonbit_malloc(sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904));
  Moonbit_object_header(_closure_1947)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1947->code
  = &_M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904;
  _closure_1947->$0 = _M0L5indexS908;
  _closure_1947->$1 = _M0L8filenameS906;
  _M0L13handle__startS904 = (struct _M0TWEu*)_closure_1947;
  moonbit_incref_cycle_free(_M0L8filenameS906);
  _closure_1948
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909*)moonbit_malloc(sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909));
  Moonbit_object_header(_closure_1948)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1948->code
  = &_M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909;
  _closure_1948->$0 = _M0L5indexS908;
  _closure_1948->$1 = _M0L8filenameS906;
  _M0L14handle__resultS909 = (struct _M0TWssbEu*)_closure_1948;
  _M0L17error__to__stringS916
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1950
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
  if (_tmp_1950.tag) {
    int32_t const _M0L5_2aokS1913 = _tmp_1950.data.ok;
    _handle__error__result_1951 = _M0L5_2aokS1913;
  } else {
    void* const _M0L6_2aerrS1914 = _tmp_1950.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS916);
    moonbit_decref_cycle_free(_M0L13handle__startS904);
    _M0L11_2atry__errS931 = _M0L6_2aerrS1914;
    goto join_930;
  }
  if (_handle__error__result_1951) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS916);
    moonbit_decref_cycle_free(_M0L13handle__startS904);
    _M0L6_2atmpS1904 = 1;
  } else {
    struct moonbit_result_0 _tmp_1952;
    int32_t _handle__error__result_1953;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1952
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
    if (_tmp_1952.tag) {
      int32_t const _M0L5_2aokS1911 = _tmp_1952.data.ok;
      _handle__error__result_1953 = _M0L5_2aokS1911;
    } else {
      void* const _M0L6_2aerrS1912 = _tmp_1952.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS916);
      moonbit_decref_cycle_free(_M0L13handle__startS904);
      _M0L11_2atry__errS931 = _M0L6_2aerrS1912;
      goto join_930;
    }
    if (_handle__error__result_1953) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS916);
      moonbit_decref_cycle_free(_M0L13handle__startS904);
      _M0L6_2atmpS1904 = 1;
    } else {
      struct moonbit_result_0 _tmp_1954;
      int32_t _handle__error__result_1955;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1954
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
      if (_tmp_1954.tag) {
        int32_t const _M0L5_2aokS1909 = _tmp_1954.data.ok;
        _handle__error__result_1955 = _M0L5_2aokS1909;
      } else {
        void* const _M0L6_2aerrS1910 = _tmp_1954.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS916);
        moonbit_decref_cycle_free(_M0L13handle__startS904);
        _M0L11_2atry__errS931 = _M0L6_2aerrS1910;
        goto join_930;
      }
      if (_handle__error__result_1955) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS916);
        moonbit_decref_cycle_free(_M0L13handle__startS904);
        _M0L6_2atmpS1904 = 1;
      } else {
        struct moonbit_result_0 _tmp_1956;
        int32_t _handle__error__result_1957;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1956
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
        if (_tmp_1956.tag) {
          int32_t const _M0L5_2aokS1907 = _tmp_1956.data.ok;
          _handle__error__result_1957 = _M0L5_2aokS1907;
        } else {
          void* const _M0L6_2aerrS1908 = _tmp_1956.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          _M0L11_2atry__errS931 = _M0L6_2aerrS1908;
          goto join_930;
        }
        if (_handle__error__result_1957) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          _M0L6_2atmpS1904 = 1;
        } else {
          struct moonbit_result_0 _tmp_1958;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1958
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          if (_tmp_1958.tag) {
            int32_t const _M0L5_2aokS1905 = _tmp_1958.data.ok;
            _M0L6_2atmpS1904 = _M0L5_2aokS1905;
          } else {
            void* const _M0L6_2aerrS1906 = _tmp_1958.data.err;
            _M0L11_2atry__errS931 = _M0L6_2aerrS1906;
            goto join_930;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1904) {
    void* _M0L126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1915 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1915)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1915)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS931
    = _M0L126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1915;
    goto join_930;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS909);
  }
  goto joinlet_1949;
  join_930:;
  _M0L3errS932 = _M0L11_2atry__errS931;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS935
  = (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS932;
  _M0L7_2anameS936 = _M0L36_2aMoonBitTestDriverInternalSkipTestS935->$0;
  _M0L6_2acntS1941
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS935));
  if (_M0L6_2acntS1941 > 1) {
    int32_t _M0L11_2anew__cntS1942 = _M0L6_2acntS1941 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS935), _M0L11_2anew__cntS1942);
    moonbit_incref_cycle_free(_M0L7_2anameS936);
  } else if (_M0L6_2acntS1941 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS935);
  }
  _M0L4nameS934 = _M0L7_2anameS936;
  goto join_933;
  goto joinlet_1959;
  join_933:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(_M0L14handle__resultS909, _M0L4nameS934, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS909);
  moonbit_decref_cycle_free(_M0L4nameS934);
  joinlet_1959:;
  joinlet_1949:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1903,
  void* _M0L3errS917
) {
  void* _M0L1eS919;
  moonbit_string_t _M0L1eS921;
  moonbit_string_t _result_1962;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS917)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS922 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS923 = _M0L10_2aFailureS922->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS923);
      _M0L1eS921 = _M0L4_2aeS923;
      goto join_920;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS924 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS925 = _M0L15_2aInspectErrorS924->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS925);
      _M0L1eS921 = _M0L4_2aeS925;
      goto join_920;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS926 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS927 = _M0L16_2aSnapshotErrorS926->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS927);
      _M0L1eS921 = _M0L4_2aeS927;
      goto join_920;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS928 =
        (struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS929 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS928->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS929);
      _M0L1eS921 = _M0L4_2aeS929;
      goto join_920;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS917);
      _M0L1eS919 = _M0L3errS917;
      goto join_918;
      break;
    }
  }
  join_920:;
  return _M0L1eS921;
  join_918:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1962 = _M0FP15Error10to__string(_M0L1eS919);
  moonbit_decref_cycle_free(_M0L1eS919);
  return _result_1962;
}

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(
  struct _M0TWssbEu* _M0L6_2aenvS1900,
  moonbit_string_t _M0L10__testnameS910,
  moonbit_string_t _M0L7messageS911,
  int32_t _M0L7skippedS912
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909* _M0L14_2acasted__envS1901;
  moonbit_string_t _M0L8filenameS906;
  int32_t _M0L5indexS908;
  moonbit_string_t _M0L10file__nameS913;
  moonbit_string_t _M0L7messageS914;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS915;
  moonbit_string_t _M0L6_2atmpS1902;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1901
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909*)_M0L6_2aenvS1900;
  _M0L8filenameS906 = _M0L14_2acasted__envS1901->$1;
  _M0L5indexS908 = _M0L14_2acasted__envS1901->$0;
  if (!_M0L7skippedS912 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS913
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS906, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS914
  = _M0MPC16string6String14escape_2einner(_M0L7messageS911, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS915
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS915, _M0L10file__nameS913);
  moonbit_decref_cycle_free(_M0L10file__nameS913);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS915, _M0L5indexS908);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS915, _M0L7messageS914);
  moonbit_decref_cycle_free(_M0L7messageS914);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1902
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS915);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS915);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1902);
  moonbit_decref_cycle_free(_M0L6_2atmpS1902);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904(
  struct _M0TWEu* _M0L6_2aenvS1897
) {
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904* _M0L14_2acasted__envS1898;
  moonbit_string_t _M0L8filenameS906;
  int32_t _M0L5indexS908;
  moonbit_string_t _M0L10file__nameS905;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS907;
  moonbit_string_t _M0L6_2atmpS1899;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1898
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2ftsodyks__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904*)_M0L6_2aenvS1897;
  _M0L8filenameS906 = _M0L14_2acasted__envS1898->$1;
  _M0L5indexS908 = _M0L14_2acasted__envS1898->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS905
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS906, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS907
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS907, _M0L10file__nameS905);
  moonbit_decref_cycle_free(_M0L10file__nameS905);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS907, _M0L5indexS908);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1899
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS907);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS907);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1899);
  moonbit_decref_cycle_free(_M0L6_2atmpS1899);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S874;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS881;
  struct _M0TUsiE** _M0L6_2atmpS1896;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS888;
  moonbit_string_t* _M0L9cli__argsS889;
  moonbit_string_t _M0L6_2atmpS1895;
  moonbit_string_t _M0L6_2atmpS1894;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS890;
  int32_t _M0L7_2abindS891;
  moonbit_string_t* _M0L7_2abindS892;
  int32_t _M0L6_2acntS1943;
  int32_t _M0L2__S893;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S874 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS881 = 0;
  _M0L6_2atmpS1896 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS888
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS888->$0 = _M0L6_2atmpS1896;
  _M0L16file__and__indexS888->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS889
  = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS889)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1895 = (moonbit_string_t)_M0L9cli__argsS889[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1895);
  moonbit_decref_cycle_free(_M0L9cli__argsS889);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1894
  = _M0MP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1895);
  moonbit_decref_cycle_free(_M0L6_2atmpS1895);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS890
  = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L6_2atmpS1894, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1894);
  _M0L7_2abindS891 = _M0L10test__argsS890->$1;
  _M0L7_2abindS892 = _M0L10test__argsS890->$0;
  _M0L6_2acntS1943
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS890));
  if (_M0L6_2acntS1943 > 1) {
    int32_t _M0L11_2anew__cntS1944 = _M0L6_2acntS1943 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS890), _M0L11_2anew__cntS1944);
    moonbit_incref_cycle_free(_M0L7_2abindS892);
  } else if (_M0L6_2acntS1943 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS890);
  }
  _M0L2__S893 = 0;
  while (1) {
    if (_M0L2__S893 < _M0L7_2abindS891) {
      moonbit_string_t _M0L3argS894 =
        (moonbit_string_t)_M0L7_2abindS892[_M0L2__S893];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS895;
      moonbit_string_t _M0L4fileS896;
      moonbit_string_t _M0L5rangeS897;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS898;
      moonbit_string_t _M0L6_2atmpS1892;
      int32_t _M0L5startS899;
      moonbit_string_t _M0L6_2atmpS1891;
      int32_t _M0L3endS900;
      int32_t _M0L1iS901;
      int32_t _M0L6_2atmpS1893;
      moonbit_incref_cycle_free(_M0L3argS894);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS895
      = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L3argS894, 58);
      moonbit_decref_cycle_free(_M0L3argS894);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS896
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS895, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS897
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS895, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS895);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS898
      = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L5rangeS897, 45);
      moonbit_decref_cycle_free(_M0L5rangeS897);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1892
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS898, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS899
      = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(_M0L45moonbit__test__driver__internal__parse__int__S874, _M0L6_2atmpS1892);
      moonbit_decref_cycle_free(_M0L6_2atmpS1892);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1891
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS898, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS898);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS900
      = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(_M0L45moonbit__test__driver__internal__parse__int__S874, _M0L6_2atmpS1891);
      moonbit_decref_cycle_free(_M0L6_2atmpS1891);
      _M0L1iS901 = _M0L5startS899;
      while (1) {
        if (_M0L1iS901 < _M0L3endS900) {
          struct _M0TUsiE* _M0L8_2atupleS1889;
          int32_t _M0L6_2atmpS1890;
          moonbit_incref_cycle_free(_M0L4fileS896);
          _M0L8_2atupleS1889
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1889)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1889->$0 = _M0L4fileS896;
          _M0L8_2atupleS1889->$1 = _M0L1iS901;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS888, _M0L8_2atupleS1889);
          _M0L6_2atmpS1890 = _M0L1iS901 + 1;
          _M0L1iS901 = _M0L6_2atmpS1890;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS896);
        }
        break;
      }
      _M0L6_2atmpS1893 = _M0L2__S893 + 1;
      _M0L2__S893 = _M0L6_2atmpS1893;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS892);
    }
    break;
  }
  return _M0L16file__and__indexS888;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(
  int32_t _M0L6_2aenvS1870,
  moonbit_string_t _M0L1sS882,
  int32_t _M0L3sepS883
) {
  moonbit_string_t* _M0L6_2atmpS1888;
  struct _M0TPB5ArrayGsE* _M0L3resS884;
  struct _M0TPB8MutLocalGiE* _M0L1iS885;
  struct _M0TPB8MutLocalGiE* _M0L5startS886;
  int32_t _M0L3valS1883;
  int32_t _M0L6_2atmpS1884;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1888 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS884
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS884->$0 = _M0L6_2atmpS1888;
  _M0L3resS884->$1 = 0;
  _M0L1iS885
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS885->$0 = 0;
  _M0L5startS886
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS886)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS886->$0 = 0;
  while (1) {
    int32_t _M0L3valS1871 = _M0L1iS885->$0;
    int32_t _M0L6_2atmpS1872 = Moonbit_array_length(_M0L1sS882);
    if (_M0L3valS1871 < _M0L6_2atmpS1872) {
      int32_t _M0L3valS1875 = _M0L1iS885->$0;
      int32_t _M0L6_2atmpS1874;
      int32_t _M0L6_2atmpS1873;
      int32_t _M0L3valS1882;
      int32_t _M0L6_2atmpS1881;
      if (
        _M0L3valS1875 < 0
        || _M0L3valS1875 >= Moonbit_array_length(_M0L1sS882)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1874 = _M0L1sS882[_M0L3valS1875];
      _M0L6_2atmpS1873 = _M0L6_2atmpS1874;
      if (_M0L6_2atmpS1873 == _M0L3sepS883) {
        int32_t _M0L3valS1877 = _M0L5startS886->$0;
        int32_t _M0L3valS1878 = _M0L1iS885->$0;
        moonbit_string_t _M0L6_2atmpS1876;
        int32_t _M0L3valS1880;
        int32_t _M0L6_2atmpS1879;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1876
        = _M0MPC16string6String17unsafe__substring(_M0L1sS882, _M0L3valS1877, _M0L3valS1878);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS884, _M0L6_2atmpS1876);
        _M0L3valS1880 = _M0L1iS885->$0;
        _M0L6_2atmpS1879 = _M0L3valS1880 + 1;
        _M0L5startS886->$0 = _M0L6_2atmpS1879;
      }
      _M0L3valS1882 = _M0L1iS885->$0;
      _M0L6_2atmpS1881 = _M0L3valS1882 + 1;
      _M0L1iS885->$0 = _M0L6_2atmpS1881;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS885);
    }
    break;
  }
  _M0L3valS1883 = _M0L5startS886->$0;
  _M0L6_2atmpS1884 = Moonbit_array_length(_M0L1sS882);
  if (_M0L3valS1883 < _M0L6_2atmpS1884) {
    int32_t _M0L3valS1886 = _M0L5startS886->$0;
    int32_t _M0L6_2atmpS1887;
    moonbit_string_t _M0L6_2atmpS1885;
    moonbit_decref_cycle_free(_M0L5startS886);
    _M0L6_2atmpS1887 = Moonbit_array_length(_M0L1sS882);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1885
    = _M0MPC16string6String17unsafe__substring(_M0L1sS882, _M0L3valS1886, _M0L6_2atmpS1887);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS884, _M0L6_2atmpS1885);
  } else {
    moonbit_decref_cycle_free(_M0L5startS886);
  }
  return _M0L3resS884;
}

int32_t _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(
  int32_t _M0L6_2aenvS1863,
  moonbit_string_t _M0L1sS875
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS876;
  int32_t _M0L3lenS877;
  int32_t _M0L7_2abindS878;
  int32_t _M0L1iS879;
  int32_t _result_1967;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS876
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS876->$0 = 0;
  _M0L3lenS877 = Moonbit_array_length(_M0L1sS875);
  _M0L7_2abindS878 = 0;
  _M0L1iS879 = _M0L7_2abindS878;
  while (1) {
    if (_M0L1iS879 < _M0L3lenS877) {
      int32_t _M0L3valS1868 = _M0L3resS876->$0;
      int32_t _M0L6_2atmpS1865 = _M0L3valS1868 * 10;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1866;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1869;
      if (_M0L1iS879 < 0 || _M0L1iS879 >= Moonbit_array_length(_M0L1sS875)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1867 = _M0L1sS875[_M0L1iS879];
      _M0L6_2atmpS1866 = _M0L6_2atmpS1867 - 48;
      _M0L6_2atmpS1864 = _M0L6_2atmpS1865 + _M0L6_2atmpS1866;
      _M0L3resS876->$0 = _M0L6_2atmpS1864;
      _M0L6_2atmpS1869 = _M0L1iS879 + 1;
      _M0L1iS879 = _M0L6_2atmpS1869;
      continue;
    }
    break;
  }
  _result_1967 = _M0L3resS876->$0;
  moonbit_decref_cycle_free(_M0L3resS876);
  return _result_1967;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23tsodyks__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS873
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS873);
  return _M0L4selfS873;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S843,
  moonbit_string_t _M0L12_2adiscard__S844,
  int32_t _M0L12_2adiscard__S845,
  struct _M0TWEu* _M0L12_2adiscard__S846,
  struct _M0TWssbEu* _M0L12_2adiscard__S847,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S848
) {
  struct moonbit_result_0 _result_1968;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1968.tag = 1;
  _result_1968.data.ok = 0;
  return _result_1968;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S849,
  moonbit_string_t _M0L12_2adiscard__S850,
  int32_t _M0L12_2adiscard__S851,
  struct _M0TWEu* _M0L12_2adiscard__S852,
  struct _M0TWssbEu* _M0L12_2adiscard__S853,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S854
) {
  struct moonbit_result_0 _result_1969;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1969.tag = 1;
  _result_1969.data.ok = 0;
  return _result_1969;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S855,
  moonbit_string_t _M0L12_2adiscard__S856,
  int32_t _M0L12_2adiscard__S857,
  struct _M0TWEu* _M0L12_2adiscard__S858,
  struct _M0TWssbEu* _M0L12_2adiscard__S859,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S860
) {
  struct moonbit_result_0 _result_1970;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1970.tag = 1;
  _result_1970.data.ok = 0;
  return _result_1970;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S861,
  moonbit_string_t _M0L12_2adiscard__S862,
  int32_t _M0L12_2adiscard__S863,
  struct _M0TWEu* _M0L12_2adiscard__S864,
  struct _M0TWssbEu* _M0L12_2adiscard__S865,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S866
) {
  struct moonbit_result_0 _result_1971;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1971.tag = 1;
  _result_1971.data.ok = 0;
  return _result_1971;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S867,
  moonbit_string_t _M0L12_2adiscard__S868,
  int32_t _M0L12_2adiscard__S869,
  struct _M0TWEu* _M0L12_2adiscard__S870,
  struct _M0TWssbEu* _M0L12_2adiscard__S871,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S872
) {
  struct moonbit_result_0 _result_1972;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _result_1972.tag = 1;
  _result_1972.data.ok = 0;
  return _result_1972;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S842
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx3new(
  int32_t _M0L1nS805,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS806,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS807
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS1862;
  struct _M0TP26RiantR8snn__mbt4AdEx* _result_1973;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6_2atmpS1862 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _result_1973
  = _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(_M0L1nS805, _M0L5paramS806, _M0L6_2atmpS1862, _M0L3rngS807);
  moonbit_decref_cycle_free(_M0L6_2atmpS1862);
  return _result_1973;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(
  int32_t _M0L1nS783,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS785,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS804,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS788
) {
  struct _M0TPB5ArrayGfE* _M0L1vS782;
  float _M0L2vtS1860;
  float _M0L2vrS1861;
  float _M0L6spreadS784;
  int32_t _M0L7_2abindS786;
  int32_t _M0L1kS787;
  struct _M0TPB5ArrayGfE* _M0L1wS790;
  struct _M0TPB5ArrayGbE* _M0L4fireS791;
  float _M0L2vtS1859;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS792;
  struct _M0TPB5ArrayGiE* _M0L4tabsS793;
  struct _M0TPB5ArrayGfE* _M0L1iS794;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS795;
  struct _M0TPB5ArrayGfE* _M0L2geS796;
  struct _M0TPB5ArrayGfE* _M0L2giS797;
  struct _M0TPB5ArrayGfE* _M0L2heS798;
  struct _M0TPB5ArrayGfE* _M0L2hiS799;
  struct _M0TPB5ArrayGfE* _M0L3gluS800;
  struct _M0TPB5ArrayGfE* _M0L4gabaS801;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS802;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS803;
  struct _M0TP26RiantR8snn__mbt4AdEx* _block_1975;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1vS782 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  _M0L2vtS1860 = _M0L5paramS785->$2;
  _M0L2vrS1861 = _M0L5paramS785->$3;
  _M0L6spreadS784 = _M0L2vtS1860 - _M0L2vrS1861;
  _M0L7_2abindS786 = 0;
  _M0L1kS787 = _M0L7_2abindS786;
  while (1) {
    if (_M0L1kS787 < _M0L1nS783) {
      float _M0L2vrS1855 = _M0L5paramS785->$3;
      float _M0L6_2atmpS1857;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1854;
      int32_t _M0L6_2atmpS1858;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1857 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS788);
      _M0L6_2atmpS1856 = _M0L6_2atmpS1857 * _M0L6spreadS784;
      _M0L6_2atmpS1854 = _M0L2vrS1855 + _M0L6_2atmpS1856;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS782, _M0L1kS787, _M0L6_2atmpS1854);
      _M0L6_2atmpS1858 = _M0L1kS787 + 1;
      _M0L1kS787 = _M0L6_2atmpS1858;
      continue;
    }
    break;
  }
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1wS790 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4fireS791 = _M0MPC15array5Array4makeGbE(_M0L1nS783, 0);
  _M0L2vtS1859 = _M0L5paramS785->$2;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9thresholdS792 = _M0MPC15array5Array4makeGfE(_M0L1nS783, _M0L2vtS1859);
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4tabsS793 = _M0MPC15array5Array4makeGiE(_M0L1nS783, 1);
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1iS794 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9syn__currS795 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2geS796 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2giS797 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2heS798 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2hiS799 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L3gluS800 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4gabaS801 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
  #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__eS802 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x1p+0f);
  #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__iS803 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x1p+0f);
  moonbit_incref_cycle_free(_M0L5paramS785);
  moonbit_incref_cycle_free(_M0L5spikeS804);
  _block_1975
  = (struct _M0TP26RiantR8snn__mbt4AdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4AdEx));
  Moonbit_object_header(_block_1975)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1975->$0 = _M0L5paramS785;
  _block_1975->$1 = _M0L5spikeS804;
  _block_1975->$2 = _M0L1nS783;
  _block_1975->$3 = _M0L1vS782;
  _block_1975->$4 = _M0L1wS790;
  _block_1975->$5 = _M0L4fireS791;
  _block_1975->$6 = _M0L9thresholdS792;
  _block_1975->$7 = _M0L4tabsS793;
  _block_1975->$8 = _M0L1iS794;
  _block_1975->$9 = _M0L9syn__currS795;
  _block_1975->$10 = _M0L2geS796;
  _block_1975->$11 = _M0L2giS797;
  _block_1975->$12 = _M0L2heS798;
  _block_1975->$13 = _M0L2hiS799;
  _block_1975->$14 = _M0L3gluS800;
  _block_1975->$15 = _M0L4gabaS801;
  _block_1975->$16 = _M0L7gsyn__eS802;
  _block_1975->$17 = _M0L7gsyn__iS803;
  _block_1975->$18 = 0x0p+0f;
  _block_1975->$19 = -0x1.2cp+6f;
  _block_1975->$20 = 0x1p+0f;
  _block_1975->$21 = 0x1.8p+2f;
  _block_1975->$22 = 0x1p-1f;
  _block_1975->$23 = 0x1p+1f;
  return _block_1975;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS780;
  float _M0L2glS781;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_1976;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS780 = -0x1p+0f;
  _M0L2glS781 = -0x1p+0f;
  _block_1976
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_1976)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1976->$0 = _M0L1cS780;
  _block_1976->$1 = _M0L2glS781;
  _block_1976->$2 = 0x1.ep+3f;
  _block_1976->$3 = -0x1.9p+5f;
  _block_1976->$4 = -0x1.ep+5f;
  _block_1976->$5 = -0x1.18p+6f;
  _block_1976->$6 = 0x1.eb851eb851eb8p-5f;
  _block_1976->$7 = 0x1p+1f;
  _block_1976->$8 = 0x0p+0f;
  _block_1976->$9 = 0x0p+0f;
  _block_1976->$10 = 0x0p+0f;
  return _block_1976;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS754,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS756,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS759
) {
  struct _M0TPB5ArrayGfE* _M0L1vS753;
  float _M0L2vtS1852;
  float _M0L2vrS1853;
  float _M0L6spreadS755;
  int32_t _M0L7_2abindS757;
  int32_t _M0L1kS758;
  struct _M0TPB5ArrayGfE* _M0L1wS761;
  struct _M0TPB5ArrayGbE* _M0L4fireS762;
  struct _M0TPB5ArrayGiE* _M0L4tabsS763;
  struct _M0TPB5ArrayGfE* _M0L1iS764;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS765;
  struct _M0TPB5ArrayGfE* _M0L2geS766;
  struct _M0TPB5ArrayGfE* _M0L2giS767;
  struct _M0TPB5ArrayGfE* _M0L2heS768;
  struct _M0TPB5ArrayGfE* _M0L2hiS769;
  struct _M0TPB5ArrayGfE* _M0L3gluS770;
  struct _M0TPB5ArrayGfE* _M0L4gabaS771;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS772;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS773;
  float _M0L4e__eS774;
  float _M0L4e__iS775;
  float _M0L3treS776;
  float _M0L3tdeS777;
  float _M0L3triS778;
  float _M0L3tdiS779;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS1851;
  struct _M0TP26RiantR8snn__mbt2IF* _block_1978;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS753 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  _M0L2vtS1852 = _M0L5paramS756->$3;
  _M0L2vrS1853 = _M0L5paramS756->$4;
  _M0L6spreadS755 = _M0L2vtS1852 - _M0L2vrS1853;
  _M0L7_2abindS757 = 0;
  _M0L1kS758 = _M0L7_2abindS757;
  while (1) {
    if (_M0L1kS758 < _M0L1nS754) {
      float _M0L2vrS1847 = _M0L5paramS756->$4;
      float _M0L6_2atmpS1849;
      float _M0L6_2atmpS1848;
      float _M0L6_2atmpS1846;
      int32_t _M0L6_2atmpS1850;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1849 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS759);
      _M0L6_2atmpS1848 = _M0L6_2atmpS1849 * _M0L6spreadS755;
      _M0L6_2atmpS1846 = _M0L2vrS1847 + _M0L6_2atmpS1848;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS753, _M0L1kS758, _M0L6_2atmpS1846);
      _M0L6_2atmpS1850 = _M0L1kS758 + 1;
      _M0L1kS758 = _M0L6_2atmpS1850;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS761 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS762 = _M0MPC15array5Array4makeGbE(_M0L1nS754, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS763 = _M0MPC15array5Array4makeGiE(_M0L1nS754, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS764 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS765 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS766 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS767 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS768 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS769 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS770 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS771 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS772 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS773 = _M0MPC15array5Array4makeGfE(_M0L1nS754, 0x1p+0f);
  _M0L4e__eS774 = 0x0p+0f;
  _M0L4e__iS775 = -0x1.2cp+6f;
  _M0L3treS776 = 0x1p+0f;
  _M0L3tdeS777 = 0x1.8p+2f;
  _M0L3triS778 = 0x1p-1f;
  _M0L3tdiS779 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS1851 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS756);
  _block_1978
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_1978)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _block_1978->$0 = _M0L5paramS756;
  _block_1978->$1 = _M0L6_2atmpS1851;
  _block_1978->$2 = _M0L1nS754;
  _block_1978->$3 = _M0L1vS753;
  _block_1978->$4 = _M0L1wS761;
  _block_1978->$5 = _M0L4fireS762;
  _block_1978->$6 = _M0L4tabsS763;
  _block_1978->$7 = _M0L1iS764;
  _block_1978->$8 = _M0L9syn__currS765;
  _block_1978->$9 = _M0L2geS766;
  _block_1978->$10 = _M0L2giS767;
  _block_1978->$11 = _M0L2heS768;
  _block_1978->$12 = _M0L2hiS769;
  _block_1978->$13 = _M0L3gluS770;
  _block_1978->$14 = _M0L4gabaS771;
  _block_1978->$15 = _M0L7gsyn__eS772;
  _block_1978->$16 = _M0L7gsyn__iS773;
  _block_1978->$17 = _M0L4e__eS774;
  _block_1978->$18 = _M0L4e__iS775;
  _block_1978->$19 = _M0L3treS776;
  _block_1978->$20 = _M0L3tdeS777;
  _block_1978->$21 = _M0L3triS778;
  _block_1978->$22 = _M0L3tdiS779;
  return _block_1978;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_1979;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_1979
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_1979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1979->$0 = 0x1p+1f;
  return _block_1979;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS749;
  float _M0L2glS750;
  float _M0L2tmS751;
  float _M0L1rS752;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_1980;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS749 = 0x1.19p+8f;
  _M0L2glS750 = 0x1.4p+5f;
  _M0L2tmS751 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS752 = 0x1p+0f / 0x1.4p+5f;
  _block_1980
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_1980)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1980->$0 = _M0L1cS749;
  _block_1980->$1 = _M0L2glS750;
  _block_1980->$2 = -0x1.9p+5f;
  _block_1980->$3 = -0x1.1a66666666666p+6f;
  _block_1980->$4 = -0x1.1a66666666666p+6f;
  _block_1980->$5 = _M0L2tmS751;
  _block_1980->$6 = _M0L1rS752;
  _block_1980->$7 = 0x1p+1f;
  _block_1980->$8 = 0x1.2p+7f;
  _block_1980->$9 = 0x1p+2f;
  _block_1980->$10 = 0x1.42p+6f;
  return _block_1980;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_1981;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_1981
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_1981)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1981->$0 = 0x0p+0f;
  _block_1981->$1 = 0x1.4p+3f;
  _block_1981->$2 = 0x1.4p+3f;
  _block_1981->$3 = 0x1p+0f;
  _block_1981->$4 = 0x1p+0f;
  return _block_1981;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt10step__adex(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS728,
  float _M0L2dtS743
) {
  int32_t _M0L1nS727;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S729;
  float _M0L2tmS730;
  float _M0L2vtS731;
  float _M0L2vrS732;
  float _M0L2elS733;
  float _M0L1rS734;
  float _M0L9dt__slopeS735;
  float _M0L2twS736;
  float _M0L1aS737;
  float _M0L1bS738;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS1845;
  float _M0L2atS739;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS1844;
  float _M0L6tau__aS740;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS1843;
  float _M0L11tabs__constS741;
  float _M0L6_2atmpS1842;
  int32_t _M0L11tabs__stepsS742;
  int32_t _M0L7_2abindS744;
  int32_t _M0L1iS745;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS727 = _M0L1pS728->$2;
  _M0L3p__S729 = _M0L1pS728->$0;
  _M0L2tmS730 = _M0L3p__S729->$5;
  _M0L2vtS731 = _M0L3p__S729->$2;
  _M0L2vrS732 = _M0L3p__S729->$3;
  _M0L2elS733 = _M0L3p__S729->$4;
  _M0L1rS734 = _M0L3p__S729->$6;
  _M0L9dt__slopeS735 = _M0L3p__S729->$7;
  _M0L2twS736 = _M0L3p__S729->$8;
  _M0L1aS737 = _M0L3p__S729->$9;
  _M0L1bS738 = _M0L3p__S729->$10;
  _M0L5spikeS1845 = _M0L1pS728->$1;
  _M0L2atS739 = _M0L5spikeS1845->$0;
  _M0L5spikeS1844 = _M0L1pS728->$1;
  _M0L6tau__aS740 = _M0L5spikeS1844->$1;
  _M0L5spikeS1843 = _M0L1pS728->$1;
  _M0L11tabs__constS741 = _M0L5spikeS1843->$3;
  _M0L6_2atmpS1842 = _M0L11tabs__constS741 / _M0L2dtS743;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS742 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1842);
  _M0L7_2abindS744 = 0;
  _M0L1iS745 = _M0L7_2abindS744;
  while (1) {
    if (_M0L1iS745 < _M0L1nS727) {
      struct _M0TPB5ArrayGfE* _M0L1vS1755 = _M0L1pS728->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS1757 = _M0L1pS728->$5;
      float _M0L6_2atmpS1756;
      struct _M0TPB5ArrayGbE* _M0L4fireS1759;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1760;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1763;
      int32_t _M0L6_2atmpS1762;
      int32_t _M0L6_2atmpS1761;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1765;
      int32_t _M0L6_2atmpS1764;
      struct _M0TPB5ArrayGfE* _M0L1wS1766;
      struct _M0TPB5ArrayGfE* _M0L1wS1778;
      float _M0L6_2atmpS1768;
      struct _M0TPB5ArrayGfE* _M0L1vS1777;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1775;
      float _M0L6_2atmpS1772;
      struct _M0TPB5ArrayGfE* _M0L1wS1774;
      float _M0L6_2atmpS1773;
      float _M0L6_2atmpS1771;
      float _M0L6_2atmpS1770;
      float _M0L6_2atmpS1769;
      float _M0L6_2atmpS1767;
      float _M0L9exp__termS748;
      struct _M0TPB5ArrayGfE* _M0L1vS1779;
      struct _M0TPB5ArrayGfE* _M0L1vS1801;
      float _M0L6_2atmpS1781;
      struct _M0TPB5ArrayGfE* _M0L1vS1800;
      float _M0L6_2atmpS1799;
      float _M0L6_2atmpS1798;
      float _M0L6_2atmpS1797;
      float _M0L6_2atmpS1793;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1796;
      float _M0L6_2atmpS1795;
      float _M0L6_2atmpS1794;
      float _M0L6_2atmpS1789;
      struct _M0TPB5ArrayGfE* _M0L1wS1792;
      float _M0L6_2atmpS1791;
      float _M0L6_2atmpS1790;
      float _M0L6_2atmpS1785;
      struct _M0TPB5ArrayGfE* _M0L1iS1788;
      float _M0L6_2atmpS1787;
      float _M0L6_2atmpS1786;
      float _M0L6_2atmpS1784;
      float _M0L6_2atmpS1783;
      float _M0L6_2atmpS1782;
      float _M0L6_2atmpS1780;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1802;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1810;
      float _M0L6_2atmpS1804;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1809;
      float _M0L6_2atmpS1808;
      float _M0L6_2atmpS1807;
      float _M0L6_2atmpS1806;
      float _M0L6_2atmpS1805;
      float _M0L6_2atmpS1803;
      struct _M0TPB5ArrayGbE* _M0L4fireS1811;
      struct _M0TPB5ArrayGfE* _M0L1vS1814;
      float _M0L6_2atmpS1813;
      int32_t _M0L6_2atmpS1812;
      struct _M0TPB5ArrayGfE* _M0L1vS1815;
      struct _M0TPB5ArrayGbE* _M0L4fireS1817;
      float _M0L6_2atmpS1816;
      struct _M0TPB5ArrayGfE* _M0L1wS1819;
      struct _M0TPB5ArrayGbE* _M0L4fireS1821;
      float _M0L6_2atmpS1820;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1825;
      struct _M0TPB5ArrayGbE* _M0L4fireS1827;
      float _M0L6_2atmpS1826;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1831;
      struct _M0TPB5ArrayGbE* _M0L4fireS1833;
      int32_t _M0L6_2atmpS1832;
      int32_t _M0L6_2atmpS1754;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1757, _M0L1iS745)) {
        _M0L6_2atmpS1756 = _M0L2vrS732;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1758 = _M0L1pS728->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1756 = _M0MPC15array5Array2atGfE(_M0L1vS1758, _M0L1iS745);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1755, _M0L1iS745, _M0L6_2atmpS1756);
      _M0L4fireS1759 = _M0L1pS728->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1759, _M0L1iS745, 0);
      _M0L4tabsS1760 = _M0L1pS728->$7;
      _M0L4tabsS1763 = _M0L1pS728->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1762
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1763, _M0L1iS745);
      _M0L6_2atmpS1761 = _M0L6_2atmpS1762 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1760, _M0L1iS745, _M0L6_2atmpS1761);
      _M0L4tabsS1765 = _M0L1pS728->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1764
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1765, _M0L1iS745);
      if (_M0L6_2atmpS1764 > 0) {
        goto join_746;
      }
      _M0L1wS1766 = _M0L1pS728->$4;
      _M0L1wS1778 = _M0L1pS728->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1768 = _M0MPC15array5Array2atGfE(_M0L1wS1778, _M0L1iS745);
      _M0L1vS1777 = _M0L1pS728->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1776 = _M0MPC15array5Array2atGfE(_M0L1vS1777, _M0L1iS745);
      _M0L6_2atmpS1775 = _M0L6_2atmpS1776 - _M0L2elS733;
      _M0L6_2atmpS1772 = _M0L1aS737 * _M0L6_2atmpS1775;
      _M0L1wS1774 = _M0L1pS728->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1773 = _M0MPC15array5Array2atGfE(_M0L1wS1774, _M0L1iS745);
      _M0L6_2atmpS1771 = _M0L6_2atmpS1772 - _M0L6_2atmpS1773;
      _M0L6_2atmpS1770 = _M0L2dtS743 * _M0L6_2atmpS1771;
      _M0L6_2atmpS1769 = _M0L6_2atmpS1770 / _M0L2twS736;
      _M0L6_2atmpS1767 = _M0L6_2atmpS1768 + _M0L6_2atmpS1769;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS1766, _M0L1iS745, _M0L6_2atmpS1767);
      if (_M0L9dt__slopeS735 < 0x0p+0f) {
        _M0L9exp__termS748 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1841 = _M0L1pS728->$3;
        float _M0L6_2atmpS1838;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS1840;
        float _M0L6_2atmpS1839;
        float _M0L6_2atmpS1837;
        float _M0L6_2atmpS1836;
        float _M0L6_2atmpS1835;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1838 = _M0MPC15array5Array2atGfE(_M0L1vS1841, _M0L1iS745);
        _M0L9thresholdS1840 = _M0L1pS728->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1839
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS1840, _M0L1iS745);
        _M0L6_2atmpS1837 = _M0L6_2atmpS1838 - _M0L6_2atmpS1839;
        _M0L6_2atmpS1836 = _M0L6_2atmpS1837 / _M0L9dt__slopeS735;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1835 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1836);
        _M0L9exp__termS748 = _M0L9dt__slopeS735 * _M0L6_2atmpS1835;
      }
      _M0L1vS1779 = _M0L1pS728->$3;
      _M0L1vS1801 = _M0L1pS728->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1781 = _M0MPC15array5Array2atGfE(_M0L1vS1801, _M0L1iS745);
      _M0L1vS1800 = _M0L1pS728->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1799 = _M0MPC15array5Array2atGfE(_M0L1vS1800, _M0L1iS745);
      _M0L6_2atmpS1798 = _M0L6_2atmpS1799 - _M0L2elS733;
      _M0L6_2atmpS1797 = -_M0L6_2atmpS1798;
      _M0L6_2atmpS1793 = _M0L6_2atmpS1797 + _M0L9exp__termS748;
      _M0L9syn__currS1796 = _M0L1pS728->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1795
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1796, _M0L1iS745);
      _M0L6_2atmpS1794 = _M0L1rS734 * _M0L6_2atmpS1795;
      _M0L6_2atmpS1789 = _M0L6_2atmpS1793 - _M0L6_2atmpS1794;
      _M0L1wS1792 = _M0L1pS728->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1791 = _M0MPC15array5Array2atGfE(_M0L1wS1792, _M0L1iS745);
      _M0L6_2atmpS1790 = _M0L1rS734 * _M0L6_2atmpS1791;
      _M0L6_2atmpS1785 = _M0L6_2atmpS1789 - _M0L6_2atmpS1790;
      _M0L1iS1788 = _M0L1pS728->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1787 = _M0MPC15array5Array2atGfE(_M0L1iS1788, _M0L1iS745);
      _M0L6_2atmpS1786 = _M0L1rS734 * _M0L6_2atmpS1787;
      _M0L6_2atmpS1784 = _M0L6_2atmpS1785 + _M0L6_2atmpS1786;
      _M0L6_2atmpS1783 = _M0L2dtS743 * _M0L6_2atmpS1784;
      _M0L6_2atmpS1782 = _M0L6_2atmpS1783 / _M0L2tmS730;
      _M0L6_2atmpS1780 = _M0L6_2atmpS1781 + _M0L6_2atmpS1782;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1779, _M0L1iS745, _M0L6_2atmpS1780);
      _M0L9thresholdS1802 = _M0L1pS728->$6;
      _M0L9thresholdS1810 = _M0L1pS728->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1804
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS1810, _M0L1iS745);
      _M0L9thresholdS1809 = _M0L1pS728->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1808
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS1809, _M0L1iS745);
      _M0L6_2atmpS1807 = _M0L2vtS731 - _M0L6_2atmpS1808;
      _M0L6_2atmpS1806 = _M0L2dtS743 * _M0L6_2atmpS1807;
      _M0L6_2atmpS1805 = _M0L6_2atmpS1806 / _M0L6tau__aS740;
      _M0L6_2atmpS1803 = _M0L6_2atmpS1804 + _M0L6_2atmpS1805;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS1802, _M0L1iS745, _M0L6_2atmpS1803);
      _M0L4fireS1811 = _M0L1pS728->$5;
      _M0L1vS1814 = _M0L1pS728->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1813 = _M0MPC15array5Array2atGfE(_M0L1vS1814, _M0L1iS745);
      _M0L6_2atmpS1812 = _M0L6_2atmpS1813 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1811, _M0L1iS745, _M0L6_2atmpS1812);
      _M0L1vS1815 = _M0L1pS728->$3;
      _M0L4fireS1817 = _M0L1pS728->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1817, _M0L1iS745)) {
        _M0L6_2atmpS1816 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1818 = _M0L1pS728->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1816 = _M0MPC15array5Array2atGfE(_M0L1vS1818, _M0L1iS745);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1815, _M0L1iS745, _M0L6_2atmpS1816);
      _M0L1wS1819 = _M0L1pS728->$4;
      _M0L4fireS1821 = _M0L1pS728->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1821, _M0L1iS745)) {
        struct _M0TPB5ArrayGfE* _M0L1wS1823 = _M0L1pS728->$4;
        float _M0L6_2atmpS1822;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1822 = _M0MPC15array5Array2atGfE(_M0L1wS1823, _M0L1iS745);
        _M0L6_2atmpS1820 = _M0L6_2atmpS1822 + _M0L1bS738;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS1824 = _M0L1pS728->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1820 = _M0MPC15array5Array2atGfE(_M0L1wS1824, _M0L1iS745);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS1819, _M0L1iS745, _M0L6_2atmpS1820);
      _M0L9thresholdS1825 = _M0L1pS728->$6;
      _M0L4fireS1827 = _M0L1pS728->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1827, _M0L1iS745)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS1829 = _M0L1pS728->$6;
        float _M0L6_2atmpS1828;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1828
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS1829, _M0L1iS745);
        _M0L6_2atmpS1826 = _M0L6_2atmpS1828 + _M0L2atS739;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS1830 = _M0L1pS728->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1826
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS1830, _M0L1iS745);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS1825, _M0L1iS745, _M0L6_2atmpS1826);
      _M0L4tabsS1831 = _M0L1pS728->$7;
      _M0L4fireS1833 = _M0L1pS728->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1833, _M0L1iS745)) {
        _M0L6_2atmpS1832 = _M0L11tabs__stepsS742;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1834 = _M0L1pS728->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS1832
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1834, _M0L1iS745);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1831, _M0L1iS745, _M0L6_2atmpS1832);
      goto join_746;
      goto joinlet_1983;
      join_746:;
      _M0L6_2atmpS1754 = _M0L1iS745 + 1;
      _M0L1iS745 = _M0L6_2atmpS1754;
      continue;
      joinlet_1983:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS723
) {
  int32_t _M0L1nS722;
  int32_t _M0L7_2abindS724;
  int32_t _M0L1iS725;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS722 = _M0L1pS723->$2;
  _M0L7_2abindS724 = 0;
  _M0L1iS725 = _M0L7_2abindS724;
  while (1) {
    if (_M0L1iS725 < _M0L1nS722) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1731 = _M0L1pS723->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS1752 = _M0L1pS723->$10;
      float _M0L6_2atmpS1747;
      struct _M0TPB5ArrayGfE* _M0L1vS1751;
      float _M0L6_2atmpS1749;
      float _M0L4e__eS1750;
      float _M0L6_2atmpS1748;
      float _M0L6_2atmpS1744;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1746;
      float _M0L6_2atmpS1745;
      float _M0L6_2atmpS1733;
      struct _M0TPB5ArrayGfE* _M0L2giS1743;
      float _M0L6_2atmpS1738;
      struct _M0TPB5ArrayGfE* _M0L1vS1742;
      float _M0L6_2atmpS1740;
      float _M0L4e__iS1741;
      float _M0L6_2atmpS1739;
      float _M0L6_2atmpS1735;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1737;
      float _M0L6_2atmpS1736;
      float _M0L6_2atmpS1734;
      float _M0L6_2atmpS1732;
      int32_t _M0L6_2atmpS1753;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1747 = _M0MPC15array5Array2atGfE(_M0L2geS1752, _M0L1iS725);
      _M0L1vS1751 = _M0L1pS723->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1749 = _M0MPC15array5Array2atGfE(_M0L1vS1751, _M0L1iS725);
      _M0L4e__eS1750 = _M0L1pS723->$18;
      _M0L6_2atmpS1748 = _M0L6_2atmpS1749 - _M0L4e__eS1750;
      _M0L6_2atmpS1744 = _M0L6_2atmpS1747 * _M0L6_2atmpS1748;
      _M0L7gsyn__eS1746 = _M0L1pS723->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1745
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1746, _M0L1iS725);
      _M0L6_2atmpS1733 = _M0L6_2atmpS1744 * _M0L6_2atmpS1745;
      _M0L2giS1743 = _M0L1pS723->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1738 = _M0MPC15array5Array2atGfE(_M0L2giS1743, _M0L1iS725);
      _M0L1vS1742 = _M0L1pS723->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1740 = _M0MPC15array5Array2atGfE(_M0L1vS1742, _M0L1iS725);
      _M0L4e__iS1741 = _M0L1pS723->$19;
      _M0L6_2atmpS1739 = _M0L6_2atmpS1740 - _M0L4e__iS1741;
      _M0L6_2atmpS1735 = _M0L6_2atmpS1738 * _M0L6_2atmpS1739;
      _M0L7gsyn__iS1737 = _M0L1pS723->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1736
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1737, _M0L1iS725);
      _M0L6_2atmpS1734 = _M0L6_2atmpS1735 * _M0L6_2atmpS1736;
      _M0L6_2atmpS1732 = _M0L6_2atmpS1733 + _M0L6_2atmpS1734;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1731, _M0L1iS725, _M0L6_2atmpS1732);
      _M0L6_2atmpS1753 = _M0L1iS725 + 1;
      _M0L1iS725 = _M0L6_2atmpS1753;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS714,
  float _M0L2dtS717
) {
  int32_t _M0L1nS713;
  int32_t _M0L7_2abindS715;
  int32_t _M0L1iS716;
  int32_t _M0L7_2abindS719;
  int32_t _M0L1iS720;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS713 = _M0L1pS714->$2;
  _M0L7_2abindS715 = 0;
  _M0L1iS716 = _M0L7_2abindS715;
  while (1) {
    if (_M0L1iS716 < _M0L1nS713) {
      struct _M0TPB5ArrayGfE* _M0L2heS1669 = _M0L1pS714->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS1674 = _M0L1pS714->$12;
      float _M0L6_2atmpS1671;
      struct _M0TPB5ArrayGfE* _M0L3gluS1673;
      float _M0L6_2atmpS1672;
      float _M0L6_2atmpS1670;
      struct _M0TPB5ArrayGfE* _M0L2hiS1675;
      struct _M0TPB5ArrayGfE* _M0L2hiS1680;
      float _M0L6_2atmpS1677;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1679;
      float _M0L6_2atmpS1678;
      float _M0L6_2atmpS1676;
      struct _M0TPB5ArrayGfE* _M0L2geS1681;
      struct _M0TPB5ArrayGfE* _M0L2geS1693;
      float _M0L6_2atmpS1683;
      struct _M0TPB5ArrayGfE* _M0L2geS1692;
      float _M0L6_2atmpS1691;
      float _M0L6_2atmpS1689;
      float _M0L3tdeS1690;
      float _M0L6_2atmpS1686;
      struct _M0TPB5ArrayGfE* _M0L2heS1688;
      float _M0L6_2atmpS1687;
      float _M0L6_2atmpS1685;
      float _M0L6_2atmpS1684;
      float _M0L6_2atmpS1682;
      struct _M0TPB5ArrayGfE* _M0L2heS1694;
      struct _M0TPB5ArrayGfE* _M0L2heS1703;
      float _M0L6_2atmpS1696;
      struct _M0TPB5ArrayGfE* _M0L2heS1702;
      float _M0L6_2atmpS1701;
      float _M0L6_2atmpS1699;
      float _M0L3treS1700;
      float _M0L6_2atmpS1698;
      float _M0L6_2atmpS1697;
      float _M0L6_2atmpS1695;
      struct _M0TPB5ArrayGfE* _M0L2giS1704;
      struct _M0TPB5ArrayGfE* _M0L2giS1716;
      float _M0L6_2atmpS1706;
      struct _M0TPB5ArrayGfE* _M0L2giS1715;
      float _M0L6_2atmpS1714;
      float _M0L6_2atmpS1712;
      float _M0L3tdiS1713;
      float _M0L6_2atmpS1709;
      struct _M0TPB5ArrayGfE* _M0L2hiS1711;
      float _M0L6_2atmpS1710;
      float _M0L6_2atmpS1708;
      float _M0L6_2atmpS1707;
      float _M0L6_2atmpS1705;
      struct _M0TPB5ArrayGfE* _M0L2hiS1717;
      struct _M0TPB5ArrayGfE* _M0L2hiS1726;
      float _M0L6_2atmpS1719;
      struct _M0TPB5ArrayGfE* _M0L2hiS1725;
      float _M0L6_2atmpS1724;
      float _M0L6_2atmpS1722;
      float _M0L3triS1723;
      float _M0L6_2atmpS1721;
      float _M0L6_2atmpS1720;
      float _M0L6_2atmpS1718;
      int32_t _M0L6_2atmpS1727;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1671 = _M0MPC15array5Array2atGfE(_M0L2heS1674, _M0L1iS716);
      _M0L3gluS1673 = _M0L1pS714->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1672 = _M0MPC15array5Array2atGfE(_M0L3gluS1673, _M0L1iS716);
      _M0L6_2atmpS1670 = _M0L6_2atmpS1671 + _M0L6_2atmpS1672;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1669, _M0L1iS716, _M0L6_2atmpS1670);
      _M0L2hiS1675 = _M0L1pS714->$13;
      _M0L2hiS1680 = _M0L1pS714->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1677 = _M0MPC15array5Array2atGfE(_M0L2hiS1680, _M0L1iS716);
      _M0L4gabaS1679 = _M0L1pS714->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1678
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1679, _M0L1iS716);
      _M0L6_2atmpS1676 = _M0L6_2atmpS1677 + _M0L6_2atmpS1678;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1675, _M0L1iS716, _M0L6_2atmpS1676);
      _M0L2geS1681 = _M0L1pS714->$10;
      _M0L2geS1693 = _M0L1pS714->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1683 = _M0MPC15array5Array2atGfE(_M0L2geS1693, _M0L1iS716);
      _M0L2geS1692 = _M0L1pS714->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1691 = _M0MPC15array5Array2atGfE(_M0L2geS1692, _M0L1iS716);
      _M0L6_2atmpS1689 = -_M0L6_2atmpS1691;
      _M0L3tdeS1690 = _M0L1pS714->$21;
      _M0L6_2atmpS1686 = _M0L6_2atmpS1689 / _M0L3tdeS1690;
      _M0L2heS1688 = _M0L1pS714->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1687 = _M0MPC15array5Array2atGfE(_M0L2heS1688, _M0L1iS716);
      _M0L6_2atmpS1685 = _M0L6_2atmpS1686 + _M0L6_2atmpS1687;
      _M0L6_2atmpS1684 = _M0L2dtS717 * _M0L6_2atmpS1685;
      _M0L6_2atmpS1682 = _M0L6_2atmpS1683 + _M0L6_2atmpS1684;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1681, _M0L1iS716, _M0L6_2atmpS1682);
      _M0L2heS1694 = _M0L1pS714->$12;
      _M0L2heS1703 = _M0L1pS714->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1696 = _M0MPC15array5Array2atGfE(_M0L2heS1703, _M0L1iS716);
      _M0L2heS1702 = _M0L1pS714->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1701 = _M0MPC15array5Array2atGfE(_M0L2heS1702, _M0L1iS716);
      _M0L6_2atmpS1699 = -_M0L6_2atmpS1701;
      _M0L3treS1700 = _M0L1pS714->$20;
      _M0L6_2atmpS1698 = _M0L6_2atmpS1699 / _M0L3treS1700;
      _M0L6_2atmpS1697 = _M0L2dtS717 * _M0L6_2atmpS1698;
      _M0L6_2atmpS1695 = _M0L6_2atmpS1696 + _M0L6_2atmpS1697;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1694, _M0L1iS716, _M0L6_2atmpS1695);
      _M0L2giS1704 = _M0L1pS714->$11;
      _M0L2giS1716 = _M0L1pS714->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1706 = _M0MPC15array5Array2atGfE(_M0L2giS1716, _M0L1iS716);
      _M0L2giS1715 = _M0L1pS714->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1714 = _M0MPC15array5Array2atGfE(_M0L2giS1715, _M0L1iS716);
      _M0L6_2atmpS1712 = -_M0L6_2atmpS1714;
      _M0L3tdiS1713 = _M0L1pS714->$23;
      _M0L6_2atmpS1709 = _M0L6_2atmpS1712 / _M0L3tdiS1713;
      _M0L2hiS1711 = _M0L1pS714->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1710 = _M0MPC15array5Array2atGfE(_M0L2hiS1711, _M0L1iS716);
      _M0L6_2atmpS1708 = _M0L6_2atmpS1709 + _M0L6_2atmpS1710;
      _M0L6_2atmpS1707 = _M0L2dtS717 * _M0L6_2atmpS1708;
      _M0L6_2atmpS1705 = _M0L6_2atmpS1706 + _M0L6_2atmpS1707;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1704, _M0L1iS716, _M0L6_2atmpS1705);
      _M0L2hiS1717 = _M0L1pS714->$13;
      _M0L2hiS1726 = _M0L1pS714->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1719 = _M0MPC15array5Array2atGfE(_M0L2hiS1726, _M0L1iS716);
      _M0L2hiS1725 = _M0L1pS714->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1724 = _M0MPC15array5Array2atGfE(_M0L2hiS1725, _M0L1iS716);
      _M0L6_2atmpS1722 = -_M0L6_2atmpS1724;
      _M0L3triS1723 = _M0L1pS714->$22;
      _M0L6_2atmpS1721 = _M0L6_2atmpS1722 / _M0L3triS1723;
      _M0L6_2atmpS1720 = _M0L2dtS717 * _M0L6_2atmpS1721;
      _M0L6_2atmpS1718 = _M0L6_2atmpS1719 + _M0L6_2atmpS1720;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1717, _M0L1iS716, _M0L6_2atmpS1718);
      _M0L6_2atmpS1727 = _M0L1iS716 + 1;
      _M0L1iS716 = _M0L6_2atmpS1727;
      continue;
    }
    break;
  }
  _M0L7_2abindS719 = 0;
  _M0L1iS720 = _M0L7_2abindS719;
  while (1) {
    if (_M0L1iS720 < _M0L1nS713) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1728 = _M0L1pS714->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1729;
      int32_t _M0L6_2atmpS1730;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1728, _M0L1iS720, 0x0p+0f);
      _M0L4gabaS1729 = _M0L1pS714->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1729, _M0L1iS720, 0x0p+0f);
      _M0L6_2atmpS1730 = _M0L1iS720 + 1;
      _M0L1iS720 = _M0L6_2atmpS1730;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22forward__adex__synapse(
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0L1cS712
) {
  moonbit_string_t _M0L3symS1666;
  int32_t _if__result_1987;
  struct _M0TPB5ArrayGfE* _M0L6targetS711;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1662;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3preS1664;
  struct _M0TPB5ArrayGbE* _M0L4fireS1663;
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L3symS1666 = _M0L1cS712->$2;
  #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1666 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS1666)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS1666, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1666) * 2)
  ) {
    _if__result_1987 = 1;
  } else {
    moonbit_string_t _M0L3symS1665 = _M0L1cS712->$2;
    #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _if__result_1987
    = _M0L3symS1665 == (moonbit_string_t)moonbit_string_literal_10.data
      || Moonbit_array_length(_M0L3symS1665)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
         && 0
            == memcmp(_M0L3symS1665, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS1665) * 2);
  }
  if (_if__result_1987) {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS1667 = _M0L1cS712->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1917 = _M0L4postS1667->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS1917);
    _M0L6targetS711 = _M0L8_2afieldS1917;
  } else {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS1668 = _M0L1cS712->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1918 = _M0L4postS1668->$15;
    moonbit_incref_cycle_free(_M0L8_2afieldS1918);
    _M0L6targetS711 = _M0L8_2afieldS1918;
  }
  _M0L6matrixS1662 = _M0L1cS712->$3;
  _M0L3preS1664 = _M0L1cS712->$0;
  _M0L4fireS1663 = _M0L3preS1664->$5;
  #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1662, _M0L4fireS1663, _M0L6targetS711);
  moonbit_decref_cycle_free(_M0L6targetS711);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0MP26RiantR8snn__mbt18SpikingSynapseAdEx6random(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3preS704,
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS705,
  moonbit_string_t _M0L3symS710,
  float _M0L2muS706,
  float _M0L5sigmaS707,
  float _M0L1pS708,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS709
) {
  int32_t _M0L1nS1660;
  int32_t _M0L1nS1661;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS703;
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _block_1988;
  #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L1nS1660 = _M0L3preS704->$2;
  _M0L1nS1661 = _M0L4postS705->$2;
  #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6matrixS703
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS1660, _M0L1nS1661, _M0L2muS706, _M0L5sigmaS707, _M0L1pS708, _M0L3rngS709);
  moonbit_incref_cycle_free(_M0L3preS704);
  moonbit_incref_cycle_free(_M0L4postS705);
  moonbit_incref_cycle_free(_M0L3symS710);
  _block_1988
  = (struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx));
  Moonbit_object_header(_block_1988)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 55, 0);
  _block_1988->$0 = _M0L3preS704;
  _block_1988->$1 = _M0L4postS705;
  _block_1988->$2 = _M0L3symS710;
  _block_1988->$3 = _M0L6matrixS703;
  return _block_1988;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS699
) {
  int32_t _M0L1nS698;
  int32_t _M0L7_2abindS700;
  int32_t _M0L1iS701;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS698 = _M0L1pS699->$2;
  _M0L7_2abindS700 = 0;
  _M0L1iS701 = _M0L7_2abindS700;
  while (1) {
    if (_M0L1iS701 < _M0L1nS698) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1637 = _M0L1pS699->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1658 = _M0L1pS699->$9;
      float _M0L6_2atmpS1653;
      struct _M0TPB5ArrayGfE* _M0L1vS1657;
      float _M0L6_2atmpS1655;
      float _M0L4e__eS1656;
      float _M0L6_2atmpS1654;
      float _M0L6_2atmpS1650;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1652;
      float _M0L6_2atmpS1651;
      float _M0L6_2atmpS1639;
      struct _M0TPB5ArrayGfE* _M0L2giS1649;
      float _M0L6_2atmpS1644;
      struct _M0TPB5ArrayGfE* _M0L1vS1648;
      float _M0L6_2atmpS1646;
      float _M0L4e__iS1647;
      float _M0L6_2atmpS1645;
      float _M0L6_2atmpS1641;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1643;
      float _M0L6_2atmpS1642;
      float _M0L6_2atmpS1640;
      float _M0L6_2atmpS1638;
      int32_t _M0L6_2atmpS1659;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1653 = _M0MPC15array5Array2atGfE(_M0L2geS1658, _M0L1iS701);
      _M0L1vS1657 = _M0L1pS699->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1655 = _M0MPC15array5Array2atGfE(_M0L1vS1657, _M0L1iS701);
      _M0L4e__eS1656 = _M0L1pS699->$17;
      _M0L6_2atmpS1654 = _M0L6_2atmpS1655 - _M0L4e__eS1656;
      _M0L6_2atmpS1650 = _M0L6_2atmpS1653 * _M0L6_2atmpS1654;
      _M0L7gsyn__eS1652 = _M0L1pS699->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1651
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1652, _M0L1iS701);
      _M0L6_2atmpS1639 = _M0L6_2atmpS1650 * _M0L6_2atmpS1651;
      _M0L2giS1649 = _M0L1pS699->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1644 = _M0MPC15array5Array2atGfE(_M0L2giS1649, _M0L1iS701);
      _M0L1vS1648 = _M0L1pS699->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1646 = _M0MPC15array5Array2atGfE(_M0L1vS1648, _M0L1iS701);
      _M0L4e__iS1647 = _M0L1pS699->$18;
      _M0L6_2atmpS1645 = _M0L6_2atmpS1646 - _M0L4e__iS1647;
      _M0L6_2atmpS1641 = _M0L6_2atmpS1644 * _M0L6_2atmpS1645;
      _M0L7gsyn__iS1643 = _M0L1pS699->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1642
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1643, _M0L1iS701);
      _M0L6_2atmpS1640 = _M0L6_2atmpS1641 * _M0L6_2atmpS1642;
      _M0L6_2atmpS1638 = _M0L6_2atmpS1639 + _M0L6_2atmpS1640;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1637, _M0L1iS701, _M0L6_2atmpS1638);
      _M0L6_2atmpS1659 = _M0L1iS701 + 1;
      _M0L1iS701 = _M0L6_2atmpS1659;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS690,
  float _M0L2dtS693
) {
  int32_t _M0L1nS689;
  int32_t _M0L7_2abindS691;
  int32_t _M0L1iS692;
  int32_t _M0L7_2abindS695;
  int32_t _M0L1iS696;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS689 = _M0L1pS690->$2;
  _M0L7_2abindS691 = 0;
  _M0L1iS692 = _M0L7_2abindS691;
  while (1) {
    if (_M0L1iS692 < _M0L1nS689) {
      struct _M0TPB5ArrayGfE* _M0L2heS1575 = _M0L1pS690->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1580 = _M0L1pS690->$11;
      float _M0L6_2atmpS1577;
      struct _M0TPB5ArrayGfE* _M0L3gluS1579;
      float _M0L6_2atmpS1578;
      float _M0L6_2atmpS1576;
      struct _M0TPB5ArrayGfE* _M0L2hiS1581;
      struct _M0TPB5ArrayGfE* _M0L2hiS1586;
      float _M0L6_2atmpS1583;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1585;
      float _M0L6_2atmpS1584;
      float _M0L6_2atmpS1582;
      struct _M0TPB5ArrayGfE* _M0L2geS1587;
      struct _M0TPB5ArrayGfE* _M0L2geS1599;
      float _M0L6_2atmpS1589;
      struct _M0TPB5ArrayGfE* _M0L2geS1598;
      float _M0L6_2atmpS1597;
      float _M0L6_2atmpS1595;
      float _M0L3tdeS1596;
      float _M0L6_2atmpS1592;
      struct _M0TPB5ArrayGfE* _M0L2heS1594;
      float _M0L6_2atmpS1593;
      float _M0L6_2atmpS1591;
      float _M0L6_2atmpS1590;
      float _M0L6_2atmpS1588;
      struct _M0TPB5ArrayGfE* _M0L2heS1600;
      struct _M0TPB5ArrayGfE* _M0L2heS1609;
      float _M0L6_2atmpS1602;
      struct _M0TPB5ArrayGfE* _M0L2heS1608;
      float _M0L6_2atmpS1607;
      float _M0L6_2atmpS1605;
      float _M0L3treS1606;
      float _M0L6_2atmpS1604;
      float _M0L6_2atmpS1603;
      float _M0L6_2atmpS1601;
      struct _M0TPB5ArrayGfE* _M0L2giS1610;
      struct _M0TPB5ArrayGfE* _M0L2giS1622;
      float _M0L6_2atmpS1612;
      struct _M0TPB5ArrayGfE* _M0L2giS1621;
      float _M0L6_2atmpS1620;
      float _M0L6_2atmpS1618;
      float _M0L3tdiS1619;
      float _M0L6_2atmpS1615;
      struct _M0TPB5ArrayGfE* _M0L2hiS1617;
      float _M0L6_2atmpS1616;
      float _M0L6_2atmpS1614;
      float _M0L6_2atmpS1613;
      float _M0L6_2atmpS1611;
      struct _M0TPB5ArrayGfE* _M0L2hiS1623;
      struct _M0TPB5ArrayGfE* _M0L2hiS1632;
      float _M0L6_2atmpS1625;
      struct _M0TPB5ArrayGfE* _M0L2hiS1631;
      float _M0L6_2atmpS1630;
      float _M0L6_2atmpS1628;
      float _M0L3triS1629;
      float _M0L6_2atmpS1627;
      float _M0L6_2atmpS1626;
      float _M0L6_2atmpS1624;
      int32_t _M0L6_2atmpS1633;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1577 = _M0MPC15array5Array2atGfE(_M0L2heS1580, _M0L1iS692);
      _M0L3gluS1579 = _M0L1pS690->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1578 = _M0MPC15array5Array2atGfE(_M0L3gluS1579, _M0L1iS692);
      _M0L6_2atmpS1576 = _M0L6_2atmpS1577 + _M0L6_2atmpS1578;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1575, _M0L1iS692, _M0L6_2atmpS1576);
      _M0L2hiS1581 = _M0L1pS690->$12;
      _M0L2hiS1586 = _M0L1pS690->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1583 = _M0MPC15array5Array2atGfE(_M0L2hiS1586, _M0L1iS692);
      _M0L4gabaS1585 = _M0L1pS690->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1584
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1585, _M0L1iS692);
      _M0L6_2atmpS1582 = _M0L6_2atmpS1583 + _M0L6_2atmpS1584;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1581, _M0L1iS692, _M0L6_2atmpS1582);
      _M0L2geS1587 = _M0L1pS690->$9;
      _M0L2geS1599 = _M0L1pS690->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1589 = _M0MPC15array5Array2atGfE(_M0L2geS1599, _M0L1iS692);
      _M0L2geS1598 = _M0L1pS690->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1597 = _M0MPC15array5Array2atGfE(_M0L2geS1598, _M0L1iS692);
      _M0L6_2atmpS1595 = -_M0L6_2atmpS1597;
      _M0L3tdeS1596 = _M0L1pS690->$20;
      _M0L6_2atmpS1592 = _M0L6_2atmpS1595 / _M0L3tdeS1596;
      _M0L2heS1594 = _M0L1pS690->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1593 = _M0MPC15array5Array2atGfE(_M0L2heS1594, _M0L1iS692);
      _M0L6_2atmpS1591 = _M0L6_2atmpS1592 + _M0L6_2atmpS1593;
      _M0L6_2atmpS1590 = _M0L2dtS693 * _M0L6_2atmpS1591;
      _M0L6_2atmpS1588 = _M0L6_2atmpS1589 + _M0L6_2atmpS1590;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1587, _M0L1iS692, _M0L6_2atmpS1588);
      _M0L2heS1600 = _M0L1pS690->$11;
      _M0L2heS1609 = _M0L1pS690->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1602 = _M0MPC15array5Array2atGfE(_M0L2heS1609, _M0L1iS692);
      _M0L2heS1608 = _M0L1pS690->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1607 = _M0MPC15array5Array2atGfE(_M0L2heS1608, _M0L1iS692);
      _M0L6_2atmpS1605 = -_M0L6_2atmpS1607;
      _M0L3treS1606 = _M0L1pS690->$19;
      _M0L6_2atmpS1604 = _M0L6_2atmpS1605 / _M0L3treS1606;
      _M0L6_2atmpS1603 = _M0L2dtS693 * _M0L6_2atmpS1604;
      _M0L6_2atmpS1601 = _M0L6_2atmpS1602 + _M0L6_2atmpS1603;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1600, _M0L1iS692, _M0L6_2atmpS1601);
      _M0L2giS1610 = _M0L1pS690->$10;
      _M0L2giS1622 = _M0L1pS690->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1612 = _M0MPC15array5Array2atGfE(_M0L2giS1622, _M0L1iS692);
      _M0L2giS1621 = _M0L1pS690->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1620 = _M0MPC15array5Array2atGfE(_M0L2giS1621, _M0L1iS692);
      _M0L6_2atmpS1618 = -_M0L6_2atmpS1620;
      _M0L3tdiS1619 = _M0L1pS690->$22;
      _M0L6_2atmpS1615 = _M0L6_2atmpS1618 / _M0L3tdiS1619;
      _M0L2hiS1617 = _M0L1pS690->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1616 = _M0MPC15array5Array2atGfE(_M0L2hiS1617, _M0L1iS692);
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 + _M0L6_2atmpS1616;
      _M0L6_2atmpS1613 = _M0L2dtS693 * _M0L6_2atmpS1614;
      _M0L6_2atmpS1611 = _M0L6_2atmpS1612 + _M0L6_2atmpS1613;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1610, _M0L1iS692, _M0L6_2atmpS1611);
      _M0L2hiS1623 = _M0L1pS690->$12;
      _M0L2hiS1632 = _M0L1pS690->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1625 = _M0MPC15array5Array2atGfE(_M0L2hiS1632, _M0L1iS692);
      _M0L2hiS1631 = _M0L1pS690->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1630 = _M0MPC15array5Array2atGfE(_M0L2hiS1631, _M0L1iS692);
      _M0L6_2atmpS1628 = -_M0L6_2atmpS1630;
      _M0L3triS1629 = _M0L1pS690->$21;
      _M0L6_2atmpS1627 = _M0L6_2atmpS1628 / _M0L3triS1629;
      _M0L6_2atmpS1626 = _M0L2dtS693 * _M0L6_2atmpS1627;
      _M0L6_2atmpS1624 = _M0L6_2atmpS1625 + _M0L6_2atmpS1626;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1623, _M0L1iS692, _M0L6_2atmpS1624);
      _M0L6_2atmpS1633 = _M0L1iS692 + 1;
      _M0L1iS692 = _M0L6_2atmpS1633;
      continue;
    }
    break;
  }
  _M0L7_2abindS695 = 0;
  _M0L1iS696 = _M0L7_2abindS695;
  while (1) {
    if (_M0L1iS696 < _M0L1nS689) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1634 = _M0L1pS690->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1635;
      int32_t _M0L6_2atmpS1636;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1634, _M0L1iS696, 0x0p+0f);
      _M0L4gabaS1635 = _M0L1pS690->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1635, _M0L1iS696, 0x0p+0f);
      _M0L6_2atmpS1636 = _M0L1iS696 + 1;
      _M0L1iS696 = _M0L6_2atmpS1636;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS675,
  float _M0L2dtS684
) {
  int32_t _M0L1nS674;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S676;
  float _M0L2tmS677;
  float _M0L2elS678;
  float _M0L1rS679;
  float _M0L2vtS680;
  float _M0L2vrS681;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1574;
  float _M0L11tabs__constS682;
  float _M0L6_2atmpS1573;
  int32_t _M0L11tabs__stepsS683;
  int32_t _M0L7_2abindS685;
  int32_t _M0L1iS686;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS674 = _M0L1pS675->$2;
  _M0L3p__S676 = _M0L1pS675->$0;
  _M0L2tmS677 = _M0L3p__S676->$2;
  _M0L2elS678 = _M0L3p__S676->$5;
  _M0L1rS679 = _M0L3p__S676->$6;
  _M0L2vtS680 = _M0L3p__S676->$3;
  _M0L2vrS681 = _M0L3p__S676->$4;
  _M0L5spikeS1574 = _M0L1pS675->$1;
  _M0L11tabs__constS682 = _M0L5spikeS1574->$0;
  _M0L6_2atmpS1573 = _M0L11tabs__constS682 / _M0L2dtS684;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS683 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1573);
  _M0L7_2abindS685 = 0;
  _M0L1iS686 = _M0L7_2abindS685;
  while (1) {
    if (_M0L1iS686 < _M0L1nS674) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1533 = _M0L1pS675->$6;
      int32_t _M0L6_2atmpS1532;
      struct _M0TPB5ArrayGfE* _M0L1vS1539;
      struct _M0TPB5ArrayGfE* _M0L1vS1560;
      float _M0L6_2atmpS1541;
      float _M0L6_2atmpS1543;
      struct _M0TPB5ArrayGfE* _M0L1vS1559;
      float _M0L6_2atmpS1558;
      float _M0L6_2atmpS1557;
      float _M0L6_2atmpS1549;
      struct _M0TPB5ArrayGfE* _M0L1wS1556;
      float _M0L6_2atmpS1555;
      float _M0L6_2atmpS1552;
      struct _M0TPB5ArrayGfE* _M0L1iS1554;
      float _M0L6_2atmpS1553;
      float _M0L6_2atmpS1551;
      float _M0L6_2atmpS1550;
      float _M0L6_2atmpS1545;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1548;
      float _M0L6_2atmpS1547;
      float _M0L6_2atmpS1546;
      float _M0L6_2atmpS1544;
      float _M0L6_2atmpS1542;
      float _M0L6_2atmpS1540;
      struct _M0TPB5ArrayGbE* _M0L4fireS1561;
      struct _M0TPB5ArrayGfE* _M0L1vS1564;
      float _M0L6_2atmpS1563;
      int32_t _M0L6_2atmpS1562;
      struct _M0TPB5ArrayGfE* _M0L1vS1565;
      struct _M0TPB5ArrayGbE* _M0L4fireS1567;
      float _M0L6_2atmpS1566;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1569;
      struct _M0TPB5ArrayGbE* _M0L4fireS1571;
      int32_t _M0L6_2atmpS1570;
      int32_t _M0L6_2atmpS1531;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1532
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1533, _M0L1iS686);
      if (_M0L6_2atmpS1532 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1534 = _M0L1pS675->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1535;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1538;
        int32_t _M0L6_2atmpS1537;
        int32_t _M0L6_2atmpS1536;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1534, _M0L1iS686, 0);
        _M0L4tabsS1535 = _M0L1pS675->$6;
        _M0L4tabsS1538 = _M0L1pS675->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1537
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1538, _M0L1iS686);
        _M0L6_2atmpS1536 = _M0L6_2atmpS1537 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1535, _M0L1iS686, _M0L6_2atmpS1536);
        goto join_687;
      }
      _M0L1vS1539 = _M0L1pS675->$3;
      _M0L1vS1560 = _M0L1pS675->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1541 = _M0MPC15array5Array2atGfE(_M0L1vS1560, _M0L1iS686);
      _M0L6_2atmpS1543 = _M0L2dtS684 / _M0L2tmS677;
      _M0L1vS1559 = _M0L1pS675->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1558 = _M0MPC15array5Array2atGfE(_M0L1vS1559, _M0L1iS686);
      _M0L6_2atmpS1557 = _M0L6_2atmpS1558 - _M0L2elS678;
      _M0L6_2atmpS1549 = -_M0L6_2atmpS1557;
      _M0L1wS1556 = _M0L1pS675->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1555 = _M0MPC15array5Array2atGfE(_M0L1wS1556, _M0L1iS686);
      _M0L6_2atmpS1552 = -_M0L6_2atmpS1555;
      _M0L1iS1554 = _M0L1pS675->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1553 = _M0MPC15array5Array2atGfE(_M0L1iS1554, _M0L1iS686);
      _M0L6_2atmpS1551 = _M0L6_2atmpS1552 + _M0L6_2atmpS1553;
      _M0L6_2atmpS1550 = _M0L1rS679 * _M0L6_2atmpS1551;
      _M0L6_2atmpS1545 = _M0L6_2atmpS1549 + _M0L6_2atmpS1550;
      _M0L9syn__currS1548 = _M0L1pS675->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1547
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1548, _M0L1iS686);
      _M0L6_2atmpS1546 = _M0L1rS679 * _M0L6_2atmpS1547;
      _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - _M0L6_2atmpS1546;
      _M0L6_2atmpS1542 = _M0L6_2atmpS1543 * _M0L6_2atmpS1544;
      _M0L6_2atmpS1540 = _M0L6_2atmpS1541 + _M0L6_2atmpS1542;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1539, _M0L1iS686, _M0L6_2atmpS1540);
      _M0L4fireS1561 = _M0L1pS675->$5;
      _M0L1vS1564 = _M0L1pS675->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1563 = _M0MPC15array5Array2atGfE(_M0L1vS1564, _M0L1iS686);
      _M0L6_2atmpS1562 = _M0L6_2atmpS1563 > _M0L2vtS680;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1561, _M0L1iS686, _M0L6_2atmpS1562);
      _M0L1vS1565 = _M0L1pS675->$3;
      _M0L4fireS1567 = _M0L1pS675->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1567, _M0L1iS686)) {
        _M0L6_2atmpS1566 = _M0L2vrS681;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1568 = _M0L1pS675->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1566 = _M0MPC15array5Array2atGfE(_M0L1vS1568, _M0L1iS686);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1565, _M0L1iS686, _M0L6_2atmpS1566);
      _M0L4tabsS1569 = _M0L1pS675->$6;
      _M0L4fireS1571 = _M0L1pS675->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1571, _M0L1iS686)) {
        _M0L6_2atmpS1570 = _M0L11tabs__stepsS683;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1572 = _M0L1pS675->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1570
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1572, _M0L1iS686);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1569, _M0L1iS686, _M0L6_2atmpS1570);
      goto join_687;
      goto joinlet_1993;
      join_687:;
      _M0L6_2atmpS1531 = _M0L1iS686 + 1;
      _M0L1iS686 = _M0L6_2atmpS1531;
      continue;
      joinlet_1993:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS662,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS665,
  struct _M0TPB5ArrayGfE* _M0L7post__gS671
) {
  int32_t _M0L4rowsS661;
  int32_t _M0L7_2abindS663;
  int32_t _M0L1iS664;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS661 = _M0L1mS662->$0;
  _M0L7_2abindS663 = 0;
  _M0L1iS664 = _M0L7_2abindS663;
  while (1) {
    if (_M0L1iS664 < _M0L4rowsS661) {
      int32_t _M0L6_2atmpS1530;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS665, _M0L1iS664)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1529 = _M0L1mS662->$2;
        int32_t _M0L5startS666;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1527;
        int32_t _M0L6_2atmpS1528;
        int32_t _M0L3endS667;
        int32_t _M0L1kS668;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS666
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1529, _M0L1iS664);
        _M0L6rowptrS1527 = _M0L1mS662->$2;
        _M0L6_2atmpS1528 = _M0L1iS664 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS667
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1527, _M0L6_2atmpS1528);
        _M0L1kS668 = _M0L5startS666;
        while (1) {
          if (_M0L1kS668 < _M0L3endS667) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1525 = _M0L1mS662->$3;
            int32_t _M0L9post__idxS669;
            struct _M0TPB5ArrayGfE* _M0L4valsS1524;
            float _M0L1wS670;
            float _M0L6_2atmpS1523;
            float _M0L6_2atmpS1522;
            int32_t _M0L6_2atmpS1526;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS669
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1525, _M0L1kS668);
            _M0L4valsS1524 = _M0L1mS662->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS670
            = _M0MPC15array5Array2atGfE(_M0L4valsS1524, _M0L1kS668);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1523
            = _M0MPC15array5Array2atGfE(_M0L7post__gS671, _M0L9post__idxS669);
            _M0L6_2atmpS1522 = _M0L6_2atmpS1523 + _M0L1wS670;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS671, _M0L9post__idxS669, _M0L6_2atmpS1522);
            _M0L6_2atmpS1526 = _M0L1kS668 + 1;
            _M0L1kS668 = _M0L6_2atmpS1526;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1530 = _M0L1iS664 + 1;
      _M0L1iS664 = _M0L6_2atmpS1530;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS655,
  int32_t _M0L4colsS656,
  float _M0L2muS657,
  float _M0L5sigmaS658,
  float _M0L1pS659,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS660
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS655, _M0L4colsS656, _M0L2muS657, _M0L5sigmaS658, _M0L1pS659, 0, _M0L3rngS660);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS569,
  int32_t _M0L4colsS573,
  float _M0L2muS579,
  float _M0L5sigmaS580,
  float _M0L1pS592,
  int32_t _M0L4ruleS586,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS582
) {
  float* _M0L6_2atmpS1521;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1520;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS568;
  int32_t _M0L7_2abindS570;
  int32_t _M0L1iS571;
  int32_t _M0L6_2atmpS1519;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS645;
  int32_t* _M0L6_2atmpS1518;
  struct _M0TPB5ArrayGiE* _M0L6colptrS646;
  float* _M0L6_2atmpS1517;
  struct _M0TPB5ArrayGfE* _M0L4valsS647;
  int32_t _M0L7_2abindS648;
  int32_t _M0L1iS649;
  int32_t _M0L6_2atmpS1516;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2015;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1521 = moonbit_empty_float_array;
  _M0L6_2atmpS1520
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1520)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 61, 0);
  _M0L6_2atmpS1520->$0 = _M0L6_2atmpS1521;
  _M0L6_2atmpS1520->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS568
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS569, _M0L6_2atmpS1520);
  _M0L7_2abindS570 = 0;
  _M0L1iS571 = _M0L7_2abindS570;
  while (1) {
    if (_M0L1iS571 < _M0L4rowsS569) {
      struct _M0TPB5ArrayGfE* _M0L3rowS572;
      int32_t _M0L7_2abindS574;
      int32_t _M0L1jS575;
      int32_t _M0L6_2atmpS1472;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS572 = _M0MPC15array5Array4makeGfE(_M0L4colsS573, 0x0p+0f);
      _M0L7_2abindS574 = 0;
      _M0L1jS575 = _M0L7_2abindS574;
      while (1) {
        if (_M0L1jS575 < _M0L4colsS573) {
          double _M0L2z1S577;
          struct _M0TUddE* _M0L7_2abindS581;
          double _M0L5_2az1S583;
          float _M0L6_2atmpS1470;
          float _M0L6_2atmpS1469;
          float _M0L1wS578;
          int32_t _M0L6_2atmpS1471;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS581
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS582);
          _M0L5_2az1S583 = _M0L7_2abindS581->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS581);
          _M0L2z1S577 = _M0L5_2az1S583;
          goto join_576;
          goto joinlet_1998;
          join_576:;
          _M0L6_2atmpS1470 = (float)_M0L2z1S577;
          _M0L6_2atmpS1469 = _M0L5sigmaS580 * _M0L6_2atmpS1470;
          _M0L1wS578 = _M0L2muS579 + _M0L6_2atmpS1469;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS572, _M0L1jS575, _M0L1wS578);
          joinlet_1998:;
          _M0L6_2atmpS1471 = _M0L1jS575 + 1;
          _M0L1jS575 = _M0L6_2atmpS1471;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS571, _M0L3rowS572);
      _M0L6_2atmpS1472 = _M0L1iS571 + 1;
      _M0L1iS571 = _M0L6_2atmpS1472;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS586) {
    case 0: {
      int32_t _M0L7_2abindS587 = 0;
      int32_t _M0L1iS588 = _M0L7_2abindS587;
      while (1) {
        if (_M0L1iS588 < _M0L4rowsS569) {
          int32_t _M0L7_2abindS589 = 0;
          int32_t _M0L1jS590 = _M0L7_2abindS589;
          int32_t _M0L6_2atmpS1475;
          while (1) {
            if (_M0L1jS590 < _M0L4colsS573) {
              float _M0L1uS591;
              int32_t _M0L6_2atmpS1474;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS591 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS582);
              if (_M0L1uS591 >= _M0L1pS592) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1473;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1473
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS588);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1473, _M0L1jS590, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1473);
              }
              _M0L6_2atmpS1474 = _M0L1jS590 + 1;
              _M0L1jS590 = _M0L6_2atmpS1474;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1475 = _M0L1iS588 + 1;
          _M0L1iS588 = _M0L6_2atmpS1475;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1493 = (float)_M0L4rowsS569;
      float _M0L6_2atmpS1492 = _M0L6_2atmpS1493 * _M0L1pS592;
      int32_t _M0L7n__keepS595;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS595 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1492);
      if (_M0L7n__keepS595 > 0 && _M0L7n__keepS595 <= _M0L4rowsS569) {
        int32_t _M0L7_2abindS596 = 0;
        int32_t _M0L1jS597 = _M0L7_2abindS596;
        while (1) {
          if (_M0L1jS597 < _M0L4colsS573) {
            int32_t* _M0L6_2atmpS1487 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS598 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS599;
            int32_t _M0L1kS600;
            int32_t _M0L7n__dropS602;
            int32_t _M0L7_2abindS603;
            int32_t _M0L1kS604;
            int32_t _M0L7_2abindS610;
            int32_t _M0L1kS611;
            int32_t _M0L6_2atmpS1488;
            Moonbit_object_header(_M0L8pre__idxS598)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
            _M0L8pre__idxS598->$0 = _M0L6_2atmpS1487;
            _M0L8pre__idxS598->$1 = 0;
            _M0L7_2abindS599 = 0;
            _M0L1kS600 = _M0L7_2abindS599;
            while (1) {
              if (_M0L1kS600 < _M0L4rowsS569) {
                int32_t _M0L6_2atmpS1476;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS598, _M0L1kS600);
                _M0L6_2atmpS1476 = _M0L1kS600 + 1;
                _M0L1kS600 = _M0L6_2atmpS1476;
                continue;
              }
              break;
            }
            _M0L7n__dropS602 = _M0L4rowsS569 - _M0L7n__keepS595;
            _M0L7_2abindS603 = 0;
            _M0L1kS604 = _M0L7_2abindS603;
            while (1) {
              if (_M0L1kS604 < _M0L7n__dropS602) {
                float _M0L1uS605;
                float _M0L6_2atmpS1480;
                float _M0L6_2atmpS1482;
                float _M0L6_2atmpS1481;
                float _M0L6_2atmpS1479;
                int32_t _M0L6_2atmpS1478;
                int32_t _M0L6r__idxS606;
                int32_t _M0L10r__clampedS607;
                int32_t _M0L3tmpS608;
                int32_t _M0L6_2atmpS1477;
                int32_t _M0L6_2atmpS1483;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS605 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS582);
                _M0L6_2atmpS1480 = (float)_M0L4rowsS569;
                _M0L6_2atmpS1482 = (float)_M0L1kS604;
                _M0L6_2atmpS1481 = _M0L6_2atmpS1482 * _M0L1uS605;
                _M0L6_2atmpS1479 = _M0L6_2atmpS1480 - _M0L6_2atmpS1481;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1478
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1479);
                _M0L6r__idxS606 = _M0L1kS604 + _M0L6_2atmpS1478;
                if (_M0L6r__idxS606 >= _M0L4rowsS569) {
                  _M0L10r__clampedS607 = _M0L4rowsS569 - 1;
                } else {
                  _M0L10r__clampedS607 = _M0L6r__idxS606;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS608
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS598, _M0L1kS604);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1477
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS598, _M0L10r__clampedS607);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS598, _M0L1kS604, _M0L6_2atmpS1477);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS598, _M0L10r__clampedS607, _M0L3tmpS608);
                _M0L6_2atmpS1483 = _M0L1kS604 + 1;
                _M0L1kS604 = _M0L6_2atmpS1483;
                continue;
              }
              break;
            }
            _M0L7_2abindS610 = 0;
            _M0L1kS611 = _M0L7_2abindS610;
            while (1) {
              if (_M0L1kS611 < _M0L7n__dropS602) {
                int32_t _M0L6_2atmpS1485;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1484;
                int32_t _M0L6_2atmpS1486;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1485
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS598, _M0L1kS611);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1484
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L6_2atmpS1485);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1484, _M0L1jS597, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1484);
                _M0L6_2atmpS1486 = _M0L1kS611 + 1;
                _M0L1kS611 = _M0L6_2atmpS1486;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS598);
              }
              break;
            }
            _M0L6_2atmpS1488 = _M0L1jS597 + 1;
            _M0L1jS597 = _M0L6_2atmpS1488;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS595 == 0) {
        int32_t _M0L7_2abindS614 = 0;
        int32_t _M0L1iS615 = _M0L7_2abindS614;
        while (1) {
          if (_M0L1iS615 < _M0L4rowsS569) {
            int32_t _M0L7_2abindS616 = 0;
            int32_t _M0L1jS617 = _M0L7_2abindS616;
            int32_t _M0L6_2atmpS1491;
            while (1) {
              if (_M0L1jS617 < _M0L4colsS573) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1489;
                int32_t _M0L6_2atmpS1490;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1489
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS615);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1489, _M0L1jS617, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1489);
                _M0L6_2atmpS1490 = _M0L1jS617 + 1;
                _M0L1jS617 = _M0L6_2atmpS1490;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1491 = _M0L1iS615 + 1;
            _M0L1iS615 = _M0L6_2atmpS1491;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1511 = (float)_M0L4colsS573;
      float _M0L6_2atmpS1510 = _M0L6_2atmpS1511 * _M0L1pS592;
      int32_t _M0L7n__keepS620;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS620 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1510);
      if (_M0L7n__keepS620 > 0 && _M0L7n__keepS620 <= _M0L4colsS573) {
        int32_t _M0L7_2abindS621 = 0;
        int32_t _M0L1iS622 = _M0L7_2abindS621;
        while (1) {
          if (_M0L1iS622 < _M0L4rowsS569) {
            int32_t* _M0L6_2atmpS1505 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS623 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS624;
            int32_t _M0L1kS625;
            int32_t _M0L7n__dropS627;
            int32_t _M0L7_2abindS628;
            int32_t _M0L1kS629;
            int32_t _M0L7_2abindS635;
            int32_t _M0L1kS636;
            int32_t _M0L6_2atmpS1506;
            Moonbit_object_header(_M0L9post__idxS623)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
            _M0L9post__idxS623->$0 = _M0L6_2atmpS1505;
            _M0L9post__idxS623->$1 = 0;
            _M0L7_2abindS624 = 0;
            _M0L1kS625 = _M0L7_2abindS624;
            while (1) {
              if (_M0L1kS625 < _M0L4colsS573) {
                int32_t _M0L6_2atmpS1494;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS623, _M0L1kS625);
                _M0L6_2atmpS1494 = _M0L1kS625 + 1;
                _M0L1kS625 = _M0L6_2atmpS1494;
                continue;
              }
              break;
            }
            _M0L7n__dropS627 = _M0L4colsS573 - _M0L7n__keepS620;
            _M0L7_2abindS628 = 0;
            _M0L1kS629 = _M0L7_2abindS628;
            while (1) {
              if (_M0L1kS629 < _M0L7n__dropS627) {
                float _M0L1uS630;
                float _M0L6_2atmpS1498;
                float _M0L6_2atmpS1500;
                float _M0L6_2atmpS1499;
                float _M0L6_2atmpS1497;
                int32_t _M0L6_2atmpS1496;
                int32_t _M0L6r__idxS631;
                int32_t _M0L10r__clampedS632;
                int32_t _M0L3tmpS633;
                int32_t _M0L6_2atmpS1495;
                int32_t _M0L6_2atmpS1501;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS630 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS582);
                _M0L6_2atmpS1498 = (float)_M0L4colsS573;
                _M0L6_2atmpS1500 = (float)_M0L1kS629;
                _M0L6_2atmpS1499 = _M0L6_2atmpS1500 * _M0L1uS630;
                _M0L6_2atmpS1497 = _M0L6_2atmpS1498 - _M0L6_2atmpS1499;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1496
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1497);
                _M0L6r__idxS631 = _M0L1kS629 + _M0L6_2atmpS1496;
                if (_M0L6r__idxS631 >= _M0L4colsS573) {
                  _M0L10r__clampedS632 = _M0L4colsS573 - 1;
                } else {
                  _M0L10r__clampedS632 = _M0L6r__idxS631;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS633
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS623, _M0L1kS629);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1495
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS623, _M0L10r__clampedS632);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS623, _M0L1kS629, _M0L6_2atmpS1495);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS623, _M0L10r__clampedS632, _M0L3tmpS633);
                _M0L6_2atmpS1501 = _M0L1kS629 + 1;
                _M0L1kS629 = _M0L6_2atmpS1501;
                continue;
              }
              break;
            }
            _M0L7_2abindS635 = 0;
            _M0L1kS636 = _M0L7_2abindS635;
            while (1) {
              if (_M0L1kS636 < _M0L7n__dropS627) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1502;
                int32_t _M0L6_2atmpS1503;
                int32_t _M0L6_2atmpS1504;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1502
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS622);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1503
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS623, _M0L1kS636);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1502, _M0L6_2atmpS1503, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1502);
                _M0L6_2atmpS1504 = _M0L1kS636 + 1;
                _M0L1kS636 = _M0L6_2atmpS1504;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS623);
              }
              break;
            }
            _M0L6_2atmpS1506 = _M0L1iS622 + 1;
            _M0L1iS622 = _M0L6_2atmpS1506;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS620 == 0) {
        int32_t _M0L7_2abindS639 = 0;
        int32_t _M0L1iS640 = _M0L7_2abindS639;
        while (1) {
          if (_M0L1iS640 < _M0L4rowsS569) {
            int32_t _M0L7_2abindS641 = 0;
            int32_t _M0L1jS642 = _M0L7_2abindS641;
            int32_t _M0L6_2atmpS1509;
            while (1) {
              if (_M0L1jS642 < _M0L4colsS573) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1507;
                int32_t _M0L6_2atmpS1508;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1507
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS640);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1507, _M0L1jS642, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1507);
                _M0L6_2atmpS1508 = _M0L1jS642 + 1;
                _M0L1jS642 = _M0L6_2atmpS1508;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1509 = _M0L1iS640 + 1;
            _M0L1iS640 = _M0L6_2atmpS1509;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1519 = _M0L4rowsS569 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS645 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1519, 0);
  _M0L6_2atmpS1518 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS646
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS646)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
  _M0L6colptrS646->$0 = _M0L6_2atmpS1518;
  _M0L6colptrS646->$1 = 0;
  _M0L6_2atmpS1517 = moonbit_empty_float_array;
  _M0L4valsS647
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS647)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 61, 0);
  _M0L4valsS647->$0 = _M0L6_2atmpS1517;
  _M0L4valsS647->$1 = 0;
  _M0L7_2abindS648 = 0;
  _M0L1iS649 = _M0L7_2abindS648;
  while (1) {
    if (_M0L1iS649 < _M0L4rowsS569) {
      int32_t _M0L6_2atmpS1512;
      int32_t _M0L7_2abindS650;
      int32_t _M0L1jS651;
      int32_t _M0L6_2atmpS1515;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1512 = _M0MPC15array5Array6lengthGfE(_M0L4valsS647);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS645, _M0L1iS649, _M0L6_2atmpS1512);
      _M0L7_2abindS650 = 0;
      _M0L1jS651 = _M0L7_2abindS650;
      while (1) {
        if (_M0L1jS651 < _M0L4colsS573) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1513;
          float _M0L1vS652;
          int32_t _M0L6_2atmpS1514;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1513
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS568, _M0L1iS649);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS652
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1513, _M0L1jS651);
          moonbit_decref_cycle_free(_M0L6_2atmpS1513);
          if (_M0L1vS652 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS646, _M0L1jS651);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS647, _M0L1vS652);
          }
          _M0L6_2atmpS1514 = _M0L1jS651 + 1;
          _M0L1jS651 = _M0L6_2atmpS1514;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1515 = _M0L1iS649 + 1;
      _M0L1iS649 = _M0L6_2atmpS1515;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS568);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1516 = _M0MPC15array5Array6lengthGfE(_M0L4valsS647);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS645, _M0L4rowsS569, _M0L6_2atmpS1516);
  _block_2015
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2015)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 67, 0);
  _block_2015->$0 = _M0L4rowsS569;
  _block_2015->$1 = _M0L4colsS573;
  _block_2015->$2 = _M0L6rowptrS645;
  _block_2015->$3 = _M0L6colptrS646;
  _block_2015->$4 = _M0L4valsS647;
  return _block_2015;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS567
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS1468;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS1468 = _M0L1mS567->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS1468);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS565
) {
  struct _M0TUmmmmE* _M0L1sS564;
  uint64_t _M0L6_2atmpS1467;
  struct _M0TUmmmmE* _M0L1tS566;
  uint64_t _M0L6_2atmpS1463;
  uint64_t _M0L6_2atmpS1464;
  uint64_t _M0L6_2atmpS1465;
  uint64_t _M0L6_2atmpS1466;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2016;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS564 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS565);
  _M0L6_2atmpS1467 = _M0L1sS564->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS566 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1467);
  _M0L6_2atmpS1463 = _M0L1sS564->$0;
  _M0L6_2atmpS1464 = _M0L1sS564->$1;
  _M0L6_2atmpS1465 = _M0L1sS564->$2;
  moonbit_decref_cycle_free(_M0L1sS564);
  _M0L6_2atmpS1466 = _M0L1tS566->$0;
  moonbit_decref_cycle_free(_M0L1tS566);
  _block_2016
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2016)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2016->$0 = _M0L6_2atmpS1463;
  _block_2016->$1 = _M0L6_2atmpS1464;
  _block_2016->$2 = _M0L6_2atmpS1465;
  _block_2016->$3 = _M0L6_2atmpS1466;
  return _block_2016;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS556) {
  uint64_t _M0L2s1S555;
  uint64_t _M0L2z1S557;
  uint64_t _M0L2s2S558;
  uint64_t _M0L2z2S559;
  uint64_t _M0L2s3S560;
  uint64_t _M0L2z3S561;
  uint64_t _M0L2s4S562;
  uint64_t _M0L2z4S563;
  struct _M0TUmmmmE* _block_2017;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S555 = _M0L4seedS556 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S557 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S555);
  _M0L2s2S558 = _M0L2s1S555 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S559 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S558);
  _M0L2s3S560 = _M0L2s2S558 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S561 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S560);
  _M0L2s4S562 = _M0L2s3S560 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S563 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S562);
  _block_2017 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2017)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2017->$0 = _M0L2z1S557;
  _block_2017->$1 = _M0L2z2S559;
  _block_2017->$2 = _M0L2z3S561;
  _block_2017->$3 = _M0L2z4S563;
  return _block_2017;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS553) {
  uint64_t _M0L6_2atmpS1462;
  uint64_t _M0L6_2atmpS1461;
  uint64_t _M0L1zS552;
  uint64_t _M0L6_2atmpS1460;
  uint64_t _M0L6_2atmpS1459;
  uint64_t _M0L1zS554;
  uint64_t _M0L6_2atmpS1458;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1462 = _M0L1zS553 >> 30;
  _M0L6_2atmpS1461 = _M0L1zS553 ^ _M0L6_2atmpS1462;
  _M0L1zS552 = _M0L6_2atmpS1461 * 13787848793156543929ull;
  _M0L6_2atmpS1460 = _M0L1zS552 >> 27;
  _M0L6_2atmpS1459 = _M0L1zS552 ^ _M0L6_2atmpS1460;
  _M0L1zS554 = _M0L6_2atmpS1459 * 10723151780598845931ull;
  _M0L6_2atmpS1458 = _M0L1zS554 >> 31;
  return _M0L1zS554 ^ _M0L6_2atmpS1458;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS540
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS1442;
  int32_t _M0L6_2atmpS1441;
  float _M0L12noise__sigmaS1443;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS1442 = _M0L1sS540->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS1441 = _M0MPC15array5Array2atGbE(_M0L6activeS1442, 0);
  if (!_M0L6_2atmpS1441) {
    return 0;
  }
  _M0L12noise__sigmaS1443 = _M0L1sS540->$4;
  if (_M0L12noise__sigmaS1443 <= 0x0p+0f) {
    int32_t _M0L7_2abindS541 = 0;
    int32_t _M0L7_2abindS542 = _M0L1sS540->$3;
    int32_t _M0L1kS543 = _M0L7_2abindS541;
    while (1) {
      if (_M0L1kS543 < _M0L7_2abindS542) {
        struct _M0TPB5ArrayGfE* _M0L1iS1444 = _M0L1sS540->$2;
        float _M0L7i__baseS1445 = _M0L1sS540->$0;
        int32_t _M0L6_2atmpS1446;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS1444, _M0L1kS543, _M0L7i__baseS1445);
        _M0L6_2atmpS1446 = _M0L1kS543 + 1;
        _M0L1kS543 = _M0L6_2atmpS1446;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS545 = _M0L1sS540->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS546 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS546)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS546->$0 = 0;
    while (1) {
      int32_t _M0L3valS1447 = _M0L1kS546->$0;
      int32_t _M0L1nS1448 = _M0L1sS540->$3;
      if (_M0L3valS1447 < _M0L1nS1448) {
        double _M0L2z1S548;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1457 = _M0L1sS540->$5;
        struct _M0TUddE* _M0L7_2abindS549;
        double _M0L5_2az1S550;
        struct _M0TPB5ArrayGfE* _M0L1iS1449;
        int32_t _M0L3valS1450;
        float _M0L7i__baseS1452;
        float _M0L6_2atmpS1454;
        float _M0L6_2atmpS1453;
        float _M0L6_2atmpS1451;
        int32_t _M0L3valS1456;
        int32_t _M0L6_2atmpS1455;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS549 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1457);
        _M0L5_2az1S550 = _M0L7_2abindS549->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS549);
        _M0L2z1S548 = _M0L5_2az1S550;
        goto join_547;
        goto joinlet_2020;
        join_547:;
        _M0L1iS1449 = _M0L1sS540->$2;
        _M0L3valS1450 = _M0L1kS546->$0;
        _M0L7i__baseS1452 = _M0L1sS540->$0;
        _M0L6_2atmpS1454 = (float)_M0L2z1S548;
        _M0L6_2atmpS1453 = _M0L5sigmaS545 * _M0L6_2atmpS1454;
        _M0L6_2atmpS1451 = _M0L7i__baseS1452 + _M0L6_2atmpS1453;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS1449, _M0L3valS1450, _M0L6_2atmpS1451);
        _M0L3valS1456 = _M0L1kS546->$0;
        _M0L6_2atmpS1455 = _M0L3valS1456 + 1;
        _M0L1kS546->$0 = _M0L6_2atmpS1455;
        joinlet_2020:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS546);
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0MP26RiantR8snn__mbt20CurrentStimulusArray11new_2einner(
  struct _M0TPB5ArrayGfE* _M0L1iS536,
  int32_t _M0L1nS537,
  float _M0L7i__baseS535,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS539,
  float _M0L12noise__sigmaS538
) {
  uint8_t* _M0L6_2atmpS1440;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS1439;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _block_2021;
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS1440 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS1440[0] = 1;
  _M0L6_2atmpS1439
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS1439)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _M0L6_2atmpS1439->$0 = _M0L6_2atmpS1440;
  _M0L6_2atmpS1439->$1 = 1;
  moonbit_incref_cycle_free(_M0L1iS536);
  moonbit_incref_cycle_free(_M0L3rngS539);
  _block_2021
  = (struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray));
  Moonbit_object_header(_block_2021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 75, 0);
  _block_2021->$0 = _M0L7i__baseS535;
  _block_2021->$1 = _M0L6_2atmpS1439;
  _block_2021->$2 = _M0L1iS536;
  _block_2021->$3 = _M0L1nS537;
  _block_2021->$4 = _M0L12noise__sigmaS538;
  _block_2021->$5 = _M0L3rngS539;
  return _block_2021;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS530
) {
  double _M0L2u1S529;
  double _M0L8u1__safeS531;
  double _M0L2u2S532;
  double _M0L6_2atmpS1438;
  double _M0L6_2atmpS1437;
  double _M0L1rS533;
  double _M0L5thetaS534;
  double _M0L6_2atmpS1436;
  double _M0L6_2atmpS1433;
  double _M0L6_2atmpS1435;
  double _M0L6_2atmpS1434;
  struct _M0TUddE* _block_2022;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S529 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS530);
  if (_M0L2u1S529 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS531 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS531 = _M0L2u1S529;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S532 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS530);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1438 = _M0FPC14math2ln(_M0L8u1__safeS531);
  _M0L6_2atmpS1437 = -0x1p+1 * _M0L6_2atmpS1438;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS533 = sqrt(_M0L6_2atmpS1437);
  _M0L5thetaS534 = 0x1.921fb54442d18p+2 * _M0L2u2S532;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1436 = _M0FPC14math3cos(_M0L5thetaS534);
  _M0L6_2atmpS1433 = _M0L1rS533 * _M0L6_2atmpS1436;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1435 = _M0FPC14math3sin(_M0L5thetaS534);
  _M0L6_2atmpS1434 = _M0L1rS533 * _M0L6_2atmpS1435;
  _block_2022 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2022)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2022->$0 = _M0L6_2atmpS1433;
  _block_2022->$1 = _M0L6_2atmpS1434;
  return _block_2022;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS527
) {
  uint64_t _M0L1uS526;
  uint64_t _M0L4bitsS528;
  double _M0L6_2atmpS1432;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS526 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS527);
  _M0L4bitsS528 = _M0L1uS526 >> 11;
  _M0L6_2atmpS1432 = (double)_M0L4bitsS528;
  return _M0L6_2atmpS1432 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS524
) {
  uint32_t _M0L1uS523;
  uint32_t _M0L4bitsS525;
  double _M0L6_2atmpS1431;
  double _M0L6_2atmpS1430;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS523 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS524);
  _M0L4bitsS525 = _M0L1uS523 >> 8;
  _M0L6_2atmpS1431 = (double)_M0L4bitsS525;
  _M0L6_2atmpS1430 = _M0L6_2atmpS1431 * 0x1p-24;
  return (float)_M0L6_2atmpS1430;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS522
) {
  uint64_t _M0L1uS521;
  uint64_t _M0L6_2atmpS1429;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS521 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS522);
  _M0L6_2atmpS1429 = _M0L1uS521 >> 32;
  return (uint32_t)_M0L6_2atmpS1429;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS514
) {
  uint64_t _M0L2s0S513;
  uint64_t _M0L2s1S515;
  uint64_t _M0L2s2S516;
  uint64_t _M0L2s3S517;
  uint64_t _M0L3tmpS518;
  uint64_t _M0L6_2atmpS1428;
  uint64_t _M0L3resS519;
  uint64_t _M0L1tS520;
  uint64_t _M0L6_2atmpS1418;
  uint64_t _M0L6_2atmpS1419;
  uint64_t _M0L2s2S1421;
  uint64_t _M0L6_2atmpS1420;
  uint64_t _M0L2s3S1423;
  uint64_t _M0L6_2atmpS1422;
  uint64_t _M0L2s2S1425;
  uint64_t _M0L6_2atmpS1424;
  uint64_t _M0L2s3S1427;
  uint64_t _M0L6_2atmpS1426;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S513 = _M0L1rS514->$0;
  _M0L2s1S515 = _M0L1rS514->$1;
  _M0L2s2S516 = _M0L1rS514->$2;
  _M0L2s3S517 = _M0L1rS514->$3;
  _M0L3tmpS518 = _M0L2s0S513 + _M0L2s3S517;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1428 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS518, 23);
  _M0L3resS519 = _M0L6_2atmpS1428 + _M0L2s0S513;
  _M0L1tS520 = _M0L2s1S515 << 17;
  _M0L6_2atmpS1418 = _M0L2s2S516 ^ _M0L2s0S513;
  _M0L1rS514->$2 = _M0L6_2atmpS1418;
  _M0L6_2atmpS1419 = _M0L2s3S517 ^ _M0L2s1S515;
  _M0L1rS514->$3 = _M0L6_2atmpS1419;
  _M0L2s2S1421 = _M0L1rS514->$2;
  _M0L6_2atmpS1420 = _M0L2s1S515 ^ _M0L2s2S1421;
  _M0L1rS514->$1 = _M0L6_2atmpS1420;
  _M0L2s3S1423 = _M0L1rS514->$3;
  _M0L6_2atmpS1422 = _M0L2s0S513 ^ _M0L2s3S1423;
  _M0L1rS514->$0 = _M0L6_2atmpS1422;
  _M0L2s2S1425 = _M0L1rS514->$2;
  _M0L6_2atmpS1424 = _M0L2s2S1425 ^ _M0L1tS520;
  _M0L1rS514->$2 = _M0L6_2atmpS1424;
  _M0L2s3S1427 = _M0L1rS514->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1426 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1427, 45);
  _M0L1rS514->$3 = _M0L6_2atmpS1426;
  return _M0L3resS519;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS511, int32_t _M0L1kS512) {
  uint64_t _M0L6_2atmpS1415;
  int32_t _M0L6_2atmpS1417;
  uint64_t _M0L6_2atmpS1416;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1415 = _M0L1xS511 << (_M0L1kS512 & 63);
  _M0L6_2atmpS1417 = 64 - _M0L1kS512;
  _M0L6_2atmpS1416 = _M0L1xS511 >> (_M0L6_2atmpS1417 & 63);
  return _M0L6_2atmpS1415 | _M0L6_2atmpS1416;
}

double _M0FPC14math2ln(double _M0L1xS497) {
  struct _M0TUdiE* _M0L7_2abindS498;
  double _M0L5_2af1S499;
  int32_t _M0L5_2akiS500;
  double _M0L1fS502;
  double _M0L1kS503;
  double _M0L6_2atmpS1408;
  double _M0L1sS504;
  double _M0L2s2S505;
  double _M0L2s4S506;
  double _M0L6_2atmpS1407;
  double _M0L6_2atmpS1406;
  double _M0L6_2atmpS1405;
  double _M0L6_2atmpS1404;
  double _M0L6_2atmpS1403;
  double _M0L6_2atmpS1402;
  double _M0L2t1S507;
  double _M0L6_2atmpS1401;
  double _M0L6_2atmpS1400;
  double _M0L6_2atmpS1399;
  double _M0L6_2atmpS1398;
  double _M0L2t2S508;
  double _M0L1rS509;
  double _M0L6_2atmpS1397;
  double _M0L4hfsqS510;
  double _M0L6_2atmpS1390;
  double _M0L6_2atmpS1396;
  double _M0L6_2atmpS1394;
  double _M0L6_2atmpS1395;
  double _M0L6_2atmpS1393;
  double _M0L6_2atmpS1392;
  double _M0L6_2atmpS1391;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS497 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS497)
      || _M0MPC16double6Double7is__inf(_M0L1xS497)
    ) {
      return _M0L1xS497;
    } else if (_M0L1xS497 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS498 = _M0FPC14math5frexp(_M0L1xS497);
  _M0L5_2af1S499 = _M0L7_2abindS498->$0;
  _M0L5_2akiS500 = _M0L7_2abindS498->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS498);
  if (_M0L5_2af1S499 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1412 = _M0L5_2af1S499 * 0x1p+1;
    double _M0L6_2atmpS1409 = _M0L6_2atmpS1412 - 0x1p+0;
    int32_t _M0L6_2atmpS1411 = _M0L5_2akiS500 - 1;
    double _M0L6_2atmpS1410 = (double)_M0L6_2atmpS1411;
    _M0L1fS502 = _M0L6_2atmpS1409;
    _M0L1kS503 = _M0L6_2atmpS1410;
    goto join_501;
  } else {
    double _M0L6_2atmpS1413 = _M0L5_2af1S499 - 0x1p+0;
    double _M0L6_2atmpS1414 = (double)_M0L5_2akiS500;
    _M0L1fS502 = _M0L6_2atmpS1413;
    _M0L1kS503 = _M0L6_2atmpS1414;
    goto join_501;
  }
  join_501:;
  _M0L6_2atmpS1408 = 0x1p+1 + _M0L1fS502;
  _M0L1sS504 = _M0L1fS502 / _M0L6_2atmpS1408;
  _M0L2s2S505 = _M0L1sS504 * _M0L1sS504;
  _M0L2s4S506 = _M0L2s2S505 * _M0L2s2S505;
  _M0L6_2atmpS1407 = _M0L2s4S506 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1406 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1407;
  _M0L6_2atmpS1405 = _M0L2s4S506 * _M0L6_2atmpS1406;
  _M0L6_2atmpS1404 = 0x1.2492494229359p-2 + _M0L6_2atmpS1405;
  _M0L6_2atmpS1403 = _M0L2s4S506 * _M0L6_2atmpS1404;
  _M0L6_2atmpS1402 = 0x1.5555555555593p-1 + _M0L6_2atmpS1403;
  _M0L2t1S507 = _M0L2s2S505 * _M0L6_2atmpS1402;
  _M0L6_2atmpS1401 = _M0L2s4S506 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1400 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1401;
  _M0L6_2atmpS1399 = _M0L2s4S506 * _M0L6_2atmpS1400;
  _M0L6_2atmpS1398 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1399;
  _M0L2t2S508 = _M0L2s4S506 * _M0L6_2atmpS1398;
  _M0L1rS509 = _M0L2t1S507 + _M0L2t2S508;
  _M0L6_2atmpS1397 = 0x1p-1 * _M0L1fS502;
  _M0L4hfsqS510 = _M0L6_2atmpS1397 * _M0L1fS502;
  _M0L6_2atmpS1390 = _M0L1kS503 * 0x1.62e42feep-1;
  _M0L6_2atmpS1396 = _M0L4hfsqS510 + _M0L1rS509;
  _M0L6_2atmpS1394 = _M0L1sS504 * _M0L6_2atmpS1396;
  _M0L6_2atmpS1395 = _M0L1kS503 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1393 = _M0L6_2atmpS1394 + _M0L6_2atmpS1395;
  _M0L6_2atmpS1392 = _M0L4hfsqS510 - _M0L6_2atmpS1393;
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 - _M0L1fS502;
  return _M0L6_2atmpS1390 - _M0L6_2atmpS1391;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS490) {
  struct _M0TUdiE* _M0L7_2abindS491;
  double _M0L10_2anorm__fS492;
  int32_t _M0L6_2aexpS493;
  uint64_t _M0L1uS494;
  uint64_t _M0L6_2atmpS1389;
  uint64_t _M0L6_2atmpS1388;
  int32_t _M0L6_2atmpS1387;
  int32_t _M0L6_2atmpS1386;
  int32_t _M0L3expS495;
  uint64_t _M0L6_2atmpS1385;
  uint64_t _M0L6_2atmpS1384;
  uint64_t _M0L6_2atmpS1383;
  double _M0L4fracS496;
  struct _M0TUdiE* _block_2025;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS490 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS490)
    || _M0MPC16double6Double7is__nan(_M0L1fS490)
  ) {
    struct _M0TUdiE* _block_2024 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2024)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2024->$0 = _M0L1fS490;
    _block_2024->$1 = 0;
    return _block_2024;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS491 = _M0FPC14math9normalize(_M0L1fS490);
  _M0L10_2anorm__fS492 = _M0L7_2abindS491->$0;
  _M0L6_2aexpS493 = _M0L7_2abindS491->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS491);
  _M0L1uS494 = *(int64_t*)&_M0L10_2anorm__fS492;
  _M0L6_2atmpS1389 = _M0L1uS494 >> 52;
  _M0L6_2atmpS1388 = _M0L6_2atmpS1389 & 2047ull;
  _M0L6_2atmpS1387 = (int32_t)_M0L6_2atmpS1388;
  _M0L6_2atmpS1386 = _M0L6_2aexpS493 + _M0L6_2atmpS1387;
  _M0L3expS495 = _M0L6_2atmpS1386 - 1022;
  _M0L6_2atmpS1385 = ~9218868437227405312ull;
  _M0L6_2atmpS1384 = _M0L1uS494 & _M0L6_2atmpS1385;
  _M0L6_2atmpS1383 = _M0L6_2atmpS1384 | 4602678819172646912ull;
  _M0L4fracS496 = *(double*)&_M0L6_2atmpS1383;
  _block_2025 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2025)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2025->$0 = _M0L4fracS496;
  _block_2025->$1 = _M0L3expS495;
  return _block_2025;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS489) {
  double _M0L6_2atmpS1380;
  struct _M0TUdiE* _block_2027;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1380 = fabs(_M0L1fS489);
  if (_M0L6_2atmpS1380 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1382 = (double)4503599627370496ll;
    double _M0L6_2atmpS1381 = _M0L1fS489 * _M0L6_2atmpS1382;
    struct _M0TUdiE* _block_2026 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2026)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2026->$0 = _M0L6_2atmpS1381;
    _block_2026->$1 = -52;
    return _block_2026;
  }
  _block_2027 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2027)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2027->$0 = _M0L1fS489;
  _block_2027->$1 = 0;
  return _block_2027;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS488) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS488 != _M0L4selfS488) {
    return 0;
  } else if (_M0L4selfS488 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS488 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS488;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS469,
  float _M0L4elemS471
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS468;
  int32_t _M0L1iS470;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS468 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS469);
  _M0L1iS470 = 0;
  while (1) {
    if (_M0L1iS470 < _M0L3lenS469) {
      float* _M0L3bufS1372 = _M0L3arrS468->$0;
      int32_t _M0L6_2atmpS1373;
      _M0L3bufS1372[_M0L1iS470] = _M0L4elemS471;
      _M0L6_2atmpS1373 = _M0L1iS470 + 1;
      _M0L1iS470 = _M0L6_2atmpS1373;
      continue;
    }
    break;
  }
  return _M0L3arrS468;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS474,
  int32_t _M0L4elemS476
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS473;
  int32_t _M0L1iS475;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS473 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS474);
  _M0L1iS475 = 0;
  while (1) {
    if (_M0L1iS475 < _M0L3lenS474) {
      uint8_t* _M0L3bufS1374 = _M0L3arrS473->$0;
      int32_t _M0L6_2atmpS1375;
      _M0L3bufS1374[_M0L1iS475] = _M0L4elemS476;
      _M0L6_2atmpS1375 = _M0L1iS475 + 1;
      _M0L1iS475 = _M0L6_2atmpS1375;
      continue;
    }
    break;
  }
  return _M0L3arrS473;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS479,
  int32_t _M0L4elemS481
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS478;
  int32_t _M0L1iS480;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS478 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS479);
  _M0L1iS480 = 0;
  while (1) {
    if (_M0L1iS480 < _M0L3lenS479) {
      int32_t* _M0L3bufS1376 = _M0L3arrS478->$0;
      int32_t _M0L6_2atmpS1377;
      _M0L3bufS1376[_M0L1iS480] = _M0L4elemS481;
      _M0L6_2atmpS1377 = _M0L1iS480 + 1;
      _M0L1iS480 = _M0L6_2atmpS1377;
      continue;
    }
    break;
  }
  return _M0L3arrS478;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS484,
  struct _M0TPB5ArrayGfE* _M0L4elemS486
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS483;
  int32_t _M0L1iS485;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS483
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS484);
  _M0L1iS485 = 0;
  while (1) {
    if (_M0L1iS485 < _M0L3lenS484) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1378 = _M0L3arrS483->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS1919 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1378[_M0L1iS485];
      int32_t _M0L6_2atmpS1379;
      moonbit_incref_cycle_free(_M0L4elemS486);
      if (_M0L6_2aoldS1919) {
        moonbit_decref_cycle_free(_M0L6_2aoldS1919);
      }
      _M0L3bufS1378[_M0L1iS485] = _M0L4elemS486;
      _M0L6_2atmpS1379 = _M0L1iS485 + 1;
      _M0L1iS485 = _M0L6_2atmpS1379;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS486);
    }
    break;
  }
  return _M0L3arrS483;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS453,
  int32_t _M0L5indexS454,
  float _M0L5valueS455
) {
  int32_t _M0L3lenS452;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS452 = _M0L4selfS453->$1;
  if (_M0L5indexS454 >= 0 && _M0L5indexS454 < _M0L3lenS452) {
    float* _M0L6_2atmpS1368;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1368 = _M0MPC15array5Array6bufferGfE(_M0L4selfS453);
    _M0L6_2atmpS1368[_M0L5indexS454] = _M0L5valueS455;
    moonbit_decref_cycle_free(_M0L6_2atmpS1368);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS457,
  int32_t _M0L5indexS458,
  int32_t _M0L5valueS459
) {
  int32_t _M0L3lenS456;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS456 = _M0L4selfS457->$1;
  if (_M0L5indexS458 >= 0 && _M0L5indexS458 < _M0L3lenS456) {
    uint8_t* _M0L6_2atmpS1369;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1369 = _M0MPC15array5Array6bufferGbE(_M0L4selfS457);
    _M0L6_2atmpS1369[_M0L5indexS458] = _M0L5valueS459;
    moonbit_decref_cycle_free(_M0L6_2atmpS1369);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS461,
  int32_t _M0L5indexS462,
  int32_t _M0L5valueS463
) {
  int32_t _M0L3lenS460;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS460 = _M0L4selfS461->$1;
  if (_M0L5indexS462 >= 0 && _M0L5indexS462 < _M0L3lenS460) {
    int32_t* _M0L6_2atmpS1370;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1370 = _M0MPC15array5Array6bufferGiE(_M0L4selfS461);
    _M0L6_2atmpS1370[_M0L5indexS462] = _M0L5valueS463;
    moonbit_decref_cycle_free(_M0L6_2atmpS1370);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS465,
  int32_t _M0L5indexS466,
  struct _M0TPB5ArrayGfE* _M0L5valueS467
) {
  int32_t _M0L3lenS464;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS464 = _M0L4selfS465->$1;
  if (_M0L5indexS466 >= 0 && _M0L5indexS466 < _M0L3lenS464) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1371;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS1920;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1371
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS465);
    _M0L6_2aoldS1920
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1371[_M0L5indexS466];
    if (_M0L6_2aoldS1920) {
      moonbit_decref_cycle_free(_M0L6_2aoldS1920);
    }
    _M0L6_2atmpS1371[_M0L5indexS466] = _M0L5valueS467;
    moonbit_decref_cycle_free(_M0L6_2atmpS1371);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS467);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS438,
  int32_t _M0L5indexS439
) {
  int32_t _M0L3lenS437;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS437 = _M0L4selfS438->$1;
  if (_M0L5indexS439 >= 0 && _M0L5indexS439 < _M0L3lenS437) {
    uint8_t* _M0L6_2atmpS1363;
    int32_t _result_2032;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1363 = _M0MPC15array5Array6bufferGbE(_M0L4selfS438);
    _result_2032 = (int32_t)_M0L6_2atmpS1363[_M0L5indexS439];
    moonbit_decref_cycle_free(_M0L6_2atmpS1363);
    return _result_2032;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS441,
  int32_t _M0L5indexS442
) {
  int32_t _M0L3lenS440;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS440 = _M0L4selfS441->$1;
  if (_M0L5indexS442 >= 0 && _M0L5indexS442 < _M0L3lenS440) {
    float* _M0L6_2atmpS1364;
    float _result_2033;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1364 = _M0MPC15array5Array6bufferGfE(_M0L4selfS441);
    _result_2033 = (float)_M0L6_2atmpS1364[_M0L5indexS442];
    moonbit_decref_cycle_free(_M0L6_2atmpS1364);
    return _result_2033;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS444,
  int32_t _M0L5indexS445
) {
  int32_t _M0L3lenS443;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS443 = _M0L4selfS444->$1;
  if (_M0L5indexS445 >= 0 && _M0L5indexS445 < _M0L3lenS443) {
    int32_t* _M0L6_2atmpS1365;
    int32_t _result_2034;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1365 = _M0MPC15array5Array6bufferGiE(_M0L4selfS444);
    _result_2034 = (int32_t)_M0L6_2atmpS1365[_M0L5indexS445];
    moonbit_decref_cycle_free(_M0L6_2atmpS1365);
    return _result_2034;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS447,
  int32_t _M0L5indexS448
) {
  int32_t _M0L3lenS446;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS446 = _M0L4selfS447->$1;
  if (_M0L5indexS448 >= 0 && _M0L5indexS448 < _M0L3lenS446) {
    moonbit_string_t* _M0L6_2atmpS1366;
    moonbit_string_t _M0L6_2atmpS1921;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1366 = _M0MPC15array5Array6bufferGsE(_M0L4selfS447);
    _M0L6_2atmpS1921 = (moonbit_string_t)_M0L6_2atmpS1366[_M0L5indexS448];
    moonbit_incref_cycle_free(_M0L6_2atmpS1921);
    moonbit_decref_cycle_free(_M0L6_2atmpS1366);
    return _M0L6_2atmpS1921;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS450,
  int32_t _M0L5indexS451
) {
  int32_t _M0L3lenS449;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS449 = _M0L4selfS450->$1;
  if (_M0L5indexS451 >= 0 && _M0L5indexS451 < _M0L3lenS449) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1367;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS1922;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1367
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS450);
    _M0L6_2atmpS1922
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1367[_M0L5indexS451];
    if (_M0L6_2atmpS1922) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1922);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1367);
    return _M0L6_2atmpS1922;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS436) {
  moonbit_string_t _M0L6_2atmpS1362;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1362 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS436);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1362);
  moonbit_decref_cycle_free(_M0L6_2atmpS1362);
  return 0;
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS435) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS435 > _M0FPB18double__max__value
         || _M0L4selfS435 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS434) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS434 != _M0L4selfS434;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS430
) {
  float* _M0L6_2atmpS1358;
  struct _M0TPB5ArrayGfE* _block_2035;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1358 = (float*)moonbit_make_float_array_raw(_M0L3lenS430);
  _block_2035
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2035)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 61, 0);
  _block_2035->$0 = _M0L6_2atmpS1358;
  _block_2035->$1 = _M0L3lenS430;
  return _block_2035;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS431
) {
  uint8_t* _M0L6_2atmpS1359;
  struct _M0TPB5ArrayGbE* _block_2036;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1359 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS431);
  _block_2036
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2036)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _block_2036->$0 = _M0L6_2atmpS1359;
  _block_2036->$1 = _M0L3lenS431;
  return _block_2036;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS432
) {
  int32_t* _M0L6_2atmpS1360;
  struct _M0TPB5ArrayGiE* _block_2037;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1360 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS432);
  _block_2037
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2037)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
  _block_2037->$0 = _M0L6_2atmpS1360;
  _block_2037->$1 = _M0L3lenS432;
  return _block_2037;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS433
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1361;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2038;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1361
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS433, 0);
  _block_2038
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2038)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 80, 0);
  _block_2038->$0 = _M0L6_2atmpS1361;
  _block_2038->$1 = _M0L3lenS433;
  return _block_2038;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS429) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS429, 10);
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS417,
  moonbit_string_t _M0L5valueS419
) {
  int32_t _M0L3lenS1330;
  moonbit_string_t* _M0L6_2atmpS1332;
  int32_t _M0L6_2atmpS1331;
  int32_t _M0L6lengthS418;
  moonbit_string_t* _M0L3bufS1335;
  moonbit_string_t _M0L6_2aoldS1923;
  int32_t _M0L6_2atmpS1336;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1330 = _M0L4selfS417->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1332 = _M0MPC15array5Array6bufferGsE(_M0L4selfS417);
  _M0L6_2atmpS1331 = Moonbit_array_length(_M0L6_2atmpS1332);
  moonbit_decref_cycle_free(_M0L6_2atmpS1332);
  if (_M0L3lenS1330 == _M0L6_2atmpS1331) {
    int32_t _M0L3lenS1334 = _M0L4selfS417->$1;
    int32_t _M0L6_2atmpS1333 = _M0L3lenS1334 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS417, _M0L6_2atmpS1333);
  }
  _M0L6lengthS418 = _M0L4selfS417->$1;
  _M0L3bufS1335 = _M0L4selfS417->$0;
  _M0L6_2aoldS1923 = (moonbit_string_t)_M0L3bufS1335[_M0L6lengthS418];
  moonbit_decref_cycle_free(_M0L6_2aoldS1923);
  _M0L3bufS1335[_M0L6lengthS418] = _M0L5valueS419;
  _M0L6_2atmpS1336 = _M0L6lengthS418 + 1;
  _M0L4selfS417->$1 = _M0L6_2atmpS1336;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS420,
  struct _M0TUsiE* _M0L5valueS422
) {
  int32_t _M0L3lenS1337;
  struct _M0TUsiE** _M0L6_2atmpS1339;
  int32_t _M0L6_2atmpS1338;
  int32_t _M0L6lengthS421;
  struct _M0TUsiE** _M0L3bufS1342;
  struct _M0TUsiE* _M0L6_2aoldS1924;
  int32_t _M0L6_2atmpS1343;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1337 = _M0L4selfS420->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1339 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS420);
  _M0L6_2atmpS1338 = Moonbit_array_length(_M0L6_2atmpS1339);
  moonbit_decref_cycle_free(_M0L6_2atmpS1339);
  if (_M0L3lenS1337 == _M0L6_2atmpS1338) {
    int32_t _M0L3lenS1341 = _M0L4selfS420->$1;
    int32_t _M0L6_2atmpS1340 = _M0L3lenS1341 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS420, _M0L6_2atmpS1340);
  }
  _M0L6lengthS421 = _M0L4selfS420->$1;
  _M0L3bufS1342 = _M0L4selfS420->$0;
  _M0L6_2aoldS1924 = (struct _M0TUsiE*)_M0L3bufS1342[_M0L6lengthS421];
  if (_M0L6_2aoldS1924) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1924);
  }
  _M0L3bufS1342[_M0L6lengthS421] = _M0L5valueS422;
  _M0L6_2atmpS1343 = _M0L6lengthS421 + 1;
  _M0L4selfS420->$1 = _M0L6_2atmpS1343;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS423,
  int32_t _M0L5valueS425
) {
  int32_t _M0L3lenS1344;
  int32_t* _M0L6_2atmpS1346;
  int32_t _M0L6_2atmpS1345;
  int32_t _M0L6lengthS424;
  int32_t* _M0L3bufS1349;
  int32_t _M0L6_2atmpS1350;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1344 = _M0L4selfS423->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1346 = _M0MPC15array5Array6bufferGiE(_M0L4selfS423);
  _M0L6_2atmpS1345 = Moonbit_array_length(_M0L6_2atmpS1346);
  moonbit_decref_cycle_free(_M0L6_2atmpS1346);
  if (_M0L3lenS1344 == _M0L6_2atmpS1345) {
    int32_t _M0L3lenS1348 = _M0L4selfS423->$1;
    int32_t _M0L6_2atmpS1347 = _M0L3lenS1348 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS423, _M0L6_2atmpS1347);
  }
  _M0L6lengthS424 = _M0L4selfS423->$1;
  _M0L3bufS1349 = _M0L4selfS423->$0;
  _M0L3bufS1349[_M0L6lengthS424] = _M0L5valueS425;
  _M0L6_2atmpS1350 = _M0L6lengthS424 + 1;
  _M0L4selfS423->$1 = _M0L6_2atmpS1350;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS426,
  float _M0L5valueS428
) {
  int32_t _M0L3lenS1351;
  float* _M0L6_2atmpS1353;
  int32_t _M0L6_2atmpS1352;
  int32_t _M0L6lengthS427;
  float* _M0L3bufS1356;
  int32_t _M0L6_2atmpS1357;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1351 = _M0L4selfS426->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1353 = _M0MPC15array5Array6bufferGfE(_M0L4selfS426);
  _M0L6_2atmpS1352 = Moonbit_array_length(_M0L6_2atmpS1353);
  moonbit_decref_cycle_free(_M0L6_2atmpS1353);
  if (_M0L3lenS1351 == _M0L6_2atmpS1352) {
    int32_t _M0L3lenS1355 = _M0L4selfS426->$1;
    int32_t _M0L6_2atmpS1354 = _M0L3lenS1355 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS426, _M0L6_2atmpS1354);
  }
  _M0L6lengthS427 = _M0L4selfS426->$1;
  _M0L3bufS1356 = _M0L4selfS426->$0;
  _M0L3bufS1356[_M0L6lengthS427] = _M0L5valueS428;
  _M0L6_2atmpS1357 = _M0L6lengthS427 + 1;
  _M0L4selfS426->$1 = _M0L6_2atmpS1357;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS402,
  int32_t _M0L8requiredS404
) {
  int32_t _M0L8old__capS401;
  int32_t _M0L3lenS1326;
  int32_t _M0L8new__capS403;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS401 = _M0MPC15array5Array8capacityGsE(_M0L4selfS402);
  _M0L3lenS1326 = _M0L4selfS402->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS403
  = _M0FPB23array__growth__capacity(_M0L8old__capS401, _M0L3lenS1326, _M0L8requiredS404);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS402, _M0L8new__capS403);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS406,
  int32_t _M0L8requiredS408
) {
  int32_t _M0L8old__capS405;
  int32_t _M0L3lenS1327;
  int32_t _M0L8new__capS407;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS405 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS406);
  _M0L3lenS1327 = _M0L4selfS406->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS407
  = _M0FPB23array__growth__capacity(_M0L8old__capS405, _M0L3lenS1327, _M0L8requiredS408);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS406, _M0L8new__capS407);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS410,
  int32_t _M0L8requiredS412
) {
  int32_t _M0L8old__capS409;
  int32_t _M0L3lenS1328;
  int32_t _M0L8new__capS411;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS409 = _M0MPC15array5Array8capacityGiE(_M0L4selfS410);
  _M0L3lenS1328 = _M0L4selfS410->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS411
  = _M0FPB23array__growth__capacity(_M0L8old__capS409, _M0L3lenS1328, _M0L8requiredS412);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS410, _M0L8new__capS411);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS414,
  int32_t _M0L8requiredS416
) {
  int32_t _M0L8old__capS413;
  int32_t _M0L3lenS1329;
  int32_t _M0L8new__capS415;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS413 = _M0MPC15array5Array8capacityGfE(_M0L4selfS414);
  _M0L3lenS1329 = _M0L4selfS414->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS415
  = _M0FPB23array__growth__capacity(_M0L8old__capS413, _M0L3lenS1329, _M0L8requiredS416);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS414, _M0L8new__capS415);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS378,
  int32_t _M0L13new__capacityS381
) {
  moonbit_string_t* _M0L8old__bufS377;
  int32_t _M0L3lenS379;
  int32_t _M0L9copy__lenS380;
  moonbit_string_t* _M0L8new__bufS382;
  moonbit_string_t* _M0L6_2aoldS1925;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS377 = _M0L4selfS378->$0;
  _M0L3lenS379 = _M0L4selfS378->$1;
  if (_M0L3lenS379 < _M0L13new__capacityS381) {
    _M0L9copy__lenS380 = _M0L3lenS379;
  } else {
    _M0L9copy__lenS380 = _M0L13new__capacityS381;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS377);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS382
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS377, _M0L13new__capacityS381, _M0L9copy__lenS380, 0, 0);
  _M0L6_2aoldS1925 = _M0L4selfS378->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1925);
  _M0L4selfS378->$0 = _M0L8new__bufS382;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS384,
  int32_t _M0L13new__capacityS387
) {
  struct _M0TUsiE** _M0L8old__bufS383;
  int32_t _M0L3lenS385;
  int32_t _M0L9copy__lenS386;
  struct _M0TUsiE** _M0L8new__bufS388;
  struct _M0TUsiE** _M0L6_2aoldS1926;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS383 = _M0L4selfS384->$0;
  _M0L3lenS385 = _M0L4selfS384->$1;
  if (_M0L3lenS385 < _M0L13new__capacityS387) {
    _M0L9copy__lenS386 = _M0L3lenS385;
  } else {
    _M0L9copy__lenS386 = _M0L13new__capacityS387;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS383);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS388
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS383, _M0L13new__capacityS387, _M0L9copy__lenS386, 0, 0);
  _M0L6_2aoldS1926 = _M0L4selfS384->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1926);
  _M0L4selfS384->$0 = _M0L8new__bufS388;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS390,
  int32_t _M0L13new__capacityS393
) {
  int32_t* _M0L8old__bufS389;
  int32_t _M0L3lenS391;
  int32_t _M0L9copy__lenS392;
  int32_t* _M0L8new__bufS394;
  int32_t* _M0L6_2aoldS1927;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS389 = _M0L4selfS390->$0;
  _M0L3lenS391 = _M0L4selfS390->$1;
  if (_M0L3lenS391 < _M0L13new__capacityS393) {
    _M0L9copy__lenS392 = _M0L3lenS391;
  } else {
    _M0L9copy__lenS392 = _M0L13new__capacityS393;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS389);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS394
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS389, _M0L13new__capacityS393, _M0L9copy__lenS392, 0, 0);
  _M0L6_2aoldS1927 = _M0L4selfS390->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1927);
  _M0L4selfS390->$0 = _M0L8new__bufS394;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS396,
  int32_t _M0L13new__capacityS399
) {
  float* _M0L8old__bufS395;
  int32_t _M0L3lenS397;
  int32_t _M0L9copy__lenS398;
  float* _M0L8new__bufS400;
  float* _M0L6_2aoldS1928;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS395 = _M0L4selfS396->$0;
  _M0L3lenS397 = _M0L4selfS396->$1;
  if (_M0L3lenS397 < _M0L13new__capacityS399) {
    _M0L9copy__lenS398 = _M0L3lenS397;
  } else {
    _M0L9copy__lenS398 = _M0L13new__capacityS399;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS395);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS400
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS395, _M0L13new__capacityS399, _M0L9copy__lenS398, 0, 0);
  _M0L6_2aoldS1928 = _M0L4selfS396->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1928);
  _M0L4selfS396->$0 = _M0L8new__bufS400;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS373
) {
  moonbit_string_t* _M0L6_2atmpS1322;
  int32_t _result_2039;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1322 = _M0MPC15array5Array6bufferGsE(_M0L4selfS373);
  _result_2039 = Moonbit_array_length(_M0L6_2atmpS1322);
  moonbit_decref_cycle_free(_M0L6_2atmpS1322);
  return _result_2039;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS374
) {
  struct _M0TUsiE** _M0L6_2atmpS1323;
  int32_t _result_2040;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1323 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS374);
  _result_2040 = Moonbit_array_length(_M0L6_2atmpS1323);
  moonbit_decref_cycle_free(_M0L6_2atmpS1323);
  return _result_2040;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS375
) {
  int32_t* _M0L6_2atmpS1324;
  int32_t _result_2041;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1324 = _M0MPC15array5Array6bufferGiE(_M0L4selfS375);
  _result_2041 = Moonbit_array_length(_M0L6_2atmpS1324);
  moonbit_decref_cycle_free(_M0L6_2atmpS1324);
  return _result_2041;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS376
) {
  float* _M0L6_2atmpS1325;
  int32_t _result_2042;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1325 = _M0MPC15array5Array6bufferGfE(_M0L4selfS376);
  _result_2042 = Moonbit_array_length(_M0L6_2atmpS1325);
  moonbit_decref_cycle_free(_M0L6_2atmpS1325);
  return _result_2042;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS369,
  int32_t _M0L3lenS367,
  int32_t _M0L8requiredS366
) {
  int32_t _M0L5startS368;
  int32_t _M0L5spaceS370;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS366 < _M0L3lenS367) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L7currentS369 == 0) {
    _M0L5startS368 = 8;
  } else {
    _M0L5startS368 = _M0L7currentS369;
  }
  _M0L5spaceS370 = _M0L5startS368;
  while (1) {
    if (_M0L5spaceS370 < _M0L8requiredS366) {
      int32_t _M0L4nextS371 = _M0L5spaceS370 * 2;
      if (_M0L4nextS371 <= _M0L5spaceS370) {
        return _M0L8requiredS366;
      }
      _M0L5spaceS370 = _M0L4nextS371;
      continue;
    } else {
      return _M0L5spaceS370;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS365) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS365->$1;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS359) {
  uint8_t* _M0L8_2afieldS1929;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1929 = _M0L4selfS359->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1929);
  return _M0L8_2afieldS1929;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS360) {
  float* _M0L8_2afieldS1930;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1930 = _M0L4selfS360->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1930);
  return _M0L8_2afieldS1930;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS361) {
  int32_t* _M0L8_2afieldS1931;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1931 = _M0L4selfS361->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1931);
  return _M0L8_2afieldS1931;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS362
) {
  moonbit_string_t* _M0L8_2afieldS1932;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1932 = _M0L4selfS362->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1932);
  return _M0L8_2afieldS1932;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS363
) {
  struct _M0TUsiE** _M0L8_2afieldS1933;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1933 = _M0L4selfS363->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1933);
  return _M0L8_2afieldS1933;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS364
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS1934;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1934 = _M0L4selfS364->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1934);
  return _M0L8_2afieldS1934;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS358
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS358);
  return _M0L4selfS358;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS357,
  struct _M0TPC16string10StringView _M0L3strS355
) {
  int32_t _M0L3endS1320;
  int32_t _M0L5startS1321;
  int32_t _M0L8str__lenS354;
  int32_t _M0L3lenS1319;
  int32_t _M0L8requiredS356;
  uint16_t* _M0L4dataS1312;
  int32_t _M0L6_2atmpS1311;
  int32_t _if__result_2044;
  uint16_t* _M0L4dataS1313;
  int32_t _M0L3lenS1314;
  moonbit_string_t _M0L6_2atmpS1315;
  int32_t _M0L6_2atmpS1316;
  int32_t _M0L3lenS1318;
  int32_t _M0L6_2atmpS1317;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1320 = _M0L3strS355.$2;
  _M0L5startS1321 = _M0L3strS355.$1;
  _M0L8str__lenS354 = _M0L3endS1320 - _M0L5startS1321;
  if (_M0L8str__lenS354 == 0) {
    return 0;
  }
  _M0L3lenS1319 = _M0L4selfS357->$1;
  _M0L8requiredS356 = _M0L3lenS1319 + _M0L8str__lenS354;
  _M0L4dataS1312 = _M0L4selfS357->$0;
  _M0L6_2atmpS1311 = Moonbit_array_length(_M0L4dataS1312);
  if (_M0L8requiredS356 > _M0L6_2atmpS1311) {
    _if__result_2044 = 1;
  } else {
    int32_t _M0L3lenS1310 = _M0L4selfS357->$1;
    _if__result_2044 = _M0L8requiredS356 < _M0L3lenS1310;
  }
  if (_if__result_2044) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS357, _M0L8requiredS356);
  }
  _M0L4dataS1313 = _M0L4selfS357->$0;
  _M0L3lenS1314 = _M0L4selfS357->$1;
  moonbit_incref_cycle_free(_M0L4dataS1313);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1315 = _M0MPC16string10StringView4data(_M0L3strS355);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1316 = _M0MPC16string10StringView13start__offset(_M0L3strS355);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1313, _M0L3lenS1314, _M0L6_2atmpS1315, _M0L6_2atmpS1316, _M0L8str__lenS354);
  moonbit_decref_cycle_free(_M0L4dataS1313);
  moonbit_decref_cycle_free(_M0L6_2atmpS1315);
  _M0L3lenS1318 = _M0L4selfS357->$1;
  _M0L6_2atmpS1317 = _M0L3lenS1318 + _M0L8str__lenS354;
  _M0L4selfS357->$1 = _M0L6_2atmpS1317;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS351,
  int32_t _M0L5startS349,
  int32_t _M0L3endS350
) {
  int32_t _if__result_2045;
  int32_t _M0L3lenS352;
  int32_t _M0L6_2atmpS1309;
  moonbit_bytes_t _M0L5bytesS353;
  moonbit_bytes_t _M0L6_2atmpS1308;
  moonbit_string_t _result_2046;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS349 == 0) {
    int32_t _M0L6_2atmpS1307 = Moonbit_array_length(_M0L3strS351);
    _if__result_2045 = _M0L3endS350 == _M0L6_2atmpS1307;
  } else {
    _if__result_2045 = 0;
  }
  if (_if__result_2045) {
    moonbit_incref_cycle_free(_M0L3strS351);
    return _M0L3strS351;
  }
  _M0L3lenS352 = _M0L3endS350 - _M0L5startS349;
  _M0L6_2atmpS1309 = _M0L3lenS352 * 2;
  _M0L5bytesS353 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1309, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS353, 0, _M0L3strS351, _M0L5startS349, _M0L3lenS352);
  _M0L6_2atmpS1308 = _M0L5bytesS353;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2046
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1308, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1308);
  return _result_2046;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS344,
  int32_t _M0L6offsetS348,
  int64_t _M0L6lengthS346
) {
  int32_t _M0L3lenS343;
  int32_t _M0L6lengthS345;
  int32_t _if__result_2047;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS343 = Moonbit_array_length(_M0L4selfS344);
  if (_M0L6lengthS346 == 4294967296ll) {
    _M0L6lengthS345 = _M0L3lenS343 - _M0L6offsetS348;
  } else {
    int64_t _M0L7_2aSomeS347 = _M0L6lengthS346;
    _M0L6lengthS345 = (int32_t)_M0L7_2aSomeS347;
  }
  if (_M0L6offsetS348 >= 0) {
    if (_M0L6lengthS345 >= 0) {
      int32_t _M0L6_2atmpS1306 = _M0L6offsetS348 + _M0L6lengthS345;
      _if__result_2047 = _M0L6_2atmpS1306 <= _M0L3lenS343;
    } else {
      _if__result_2047 = 0;
    }
  } else {
    _if__result_2047 = 0;
  }
  if (_if__result_2047) {
    moonbit_incref_cycle_free(_M0L4selfS344);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS344, _M0L6offsetS348, _M0L6lengthS345);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS335,
  int32_t _M0L13bytes__offsetS330,
  moonbit_string_t _M0L3strS337,
  int32_t _M0L11str__offsetS333,
  int32_t _M0L6lengthS331
) {
  int32_t _M0L6_2atmpS1305;
  int32_t _M0L6_2atmpS1304;
  int32_t _M0L2e1S329;
  int32_t _M0L6_2atmpS1303;
  int32_t _M0L2e2S332;
  int32_t _M0L4len1S334;
  int32_t _M0L4len2S336;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1305 = _M0L6lengthS331 * 2;
  _M0L6_2atmpS1304 = _M0L13bytes__offsetS330 + _M0L6_2atmpS1305;
  _M0L2e1S329 = _M0L6_2atmpS1304 - 1;
  _M0L6_2atmpS1303 = _M0L11str__offsetS333 + _M0L6lengthS331;
  _M0L2e2S332 = _M0L6_2atmpS1303 - 1;
  _M0L4len1S334 = Moonbit_array_length(_M0L4selfS335);
  _M0L4len2S336 = Moonbit_array_length(_M0L3strS337);
  if (
    _M0L6lengthS331 >= 0
    && _M0L13bytes__offsetS330 >= 0
    && _M0L2e1S329 < _M0L4len1S334
    && _M0L11str__offsetS333 >= 0
    && _M0L2e2S332 < _M0L4len2S336
  ) {
    int32_t _M0L16end__str__offsetS338 =
      _M0L11str__offsetS333 + _M0L6lengthS331;
    int32_t _M0L1iS339 = _M0L11str__offsetS333;
    int32_t _M0L1jS340 = _M0L13bytes__offsetS330;
    while (1) {
      if (_M0L1iS339 < _M0L16end__str__offsetS338) {
        int32_t _M0L6_2atmpS1300 = _M0L3strS337[_M0L1iS339];
        int32_t _M0L6_2atmpS1299 = (int32_t)_M0L6_2atmpS1300;
        uint32_t _M0L1cS341 = *(uint32_t*)&_M0L6_2atmpS1299;
        uint32_t _M0L6_2atmpS1295 = _M0L1cS341 & 255u;
        int32_t _M0L6_2atmpS1294;
        int32_t _M0L6_2atmpS1296;
        uint32_t _M0L6_2atmpS1298;
        int32_t _M0L6_2atmpS1297;
        int32_t _M0L6_2atmpS1301;
        int32_t _M0L6_2atmpS1302;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1294 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1295);
        if (
          _M0L1jS340 < 0 || _M0L1jS340 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L1jS340] = _M0L6_2atmpS1294;
        _M0L6_2atmpS1296 = _M0L1jS340 + 1;
        _M0L6_2atmpS1298 = _M0L1cS341 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1297 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1298);
        if (
          _M0L6_2atmpS1296 < 0
          || _M0L6_2atmpS1296 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L6_2atmpS1296] = _M0L6_2atmpS1297;
        _M0L6_2atmpS1301 = _M0L1iS339 + 1;
        _M0L6_2atmpS1302 = _M0L1jS340 + 2;
        _M0L1iS339 = _M0L6_2atmpS1301;
        _M0L1jS340 = _M0L6_2atmpS1302;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS328) {
  int32_t _M0L6_2atmpS1293;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1293 = *(int32_t*)&_M0L4selfS328;
  return _M0L6_2atmpS1293 & 0xff;
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS312,
  int32_t _M0L5radixS311
) {
  int32_t _M0L12is__negativeS313;
  uint32_t _M0L3numS314;
  uint16_t* _M0L6bufferS315;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS311 < 2 || _M0L5radixS311 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS312 == 0) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  _M0L12is__negativeS313 = _M0L4selfS312 < 0;
  if (_M0L12is__negativeS313) {
    int32_t _M0L6_2atmpS1292 = -_M0L4selfS312;
    _M0L3numS314 = *(uint32_t*)&_M0L6_2atmpS1292;
  } else {
    _M0L3numS314 = *(uint32_t*)&_M0L4selfS312;
  }
  switch (_M0L5radixS311) {
    case 10: {
      int32_t _M0L10digit__lenS316;
      int32_t _M0L6_2atmpS1289;
      int32_t _M0L10total__lenS317;
      uint16_t* _M0L6bufferS318;
      int32_t _M0L12digit__startS319;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS316 = _M0FPB12dec__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1289 = 1;
      } else {
        _M0L6_2atmpS1289 = 0;
      }
      _M0L10total__lenS317 = _M0L10digit__lenS316 + _M0L6_2atmpS1289;
      _M0L6bufferS318
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS317, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS319 = 1;
      } else {
        _M0L12digit__startS319 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS318, _M0L3numS314, _M0L12digit__startS319, _M0L10total__lenS317);
      _M0L6bufferS315 = _M0L6bufferS318;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS320;
      int32_t _M0L6_2atmpS1290;
      int32_t _M0L10total__lenS321;
      uint16_t* _M0L6bufferS322;
      int32_t _M0L12digit__startS323;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS320 = _M0FPB12hex__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1290 = 1;
      } else {
        _M0L6_2atmpS1290 = 0;
      }
      _M0L10total__lenS321 = _M0L10digit__lenS320 + _M0L6_2atmpS1290;
      _M0L6bufferS322
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS321, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS323 = 1;
      } else {
        _M0L12digit__startS323 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS322, _M0L3numS314, _M0L12digit__startS323, _M0L10total__lenS321);
      _M0L6bufferS315 = _M0L6bufferS322;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS324;
      int32_t _M0L6_2atmpS1291;
      int32_t _M0L10total__lenS325;
      uint16_t* _M0L6bufferS326;
      int32_t _M0L12digit__startS327;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS324
      = _M0FPB14radix__count32(_M0L3numS314, _M0L5radixS311);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1291 = 1;
      } else {
        _M0L6_2atmpS1291 = 0;
      }
      _M0L10total__lenS325 = _M0L10digit__lenS324 + _M0L6_2atmpS1291;
      _M0L6bufferS326
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS325, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS327 = 1;
      } else {
        _M0L12digit__startS327 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS326, _M0L3numS314, _M0L12digit__startS327, _M0L10total__lenS325, _M0L5radixS311);
      _M0L6bufferS315 = _M0L6bufferS326;
      break;
    }
  }
  if (_M0L12is__negativeS313) {
    _M0L6bufferS315[0] = 45;
  }
  return _M0L6bufferS315;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS305,
  int32_t _M0L5radixS307
) {
  uint32_t _M0L4baseS306;
  uint32_t _M0L3numS308;
  int32_t _M0L5countS309;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS305 == 0u) {
    return 1;
  }
  _M0L4baseS306 = *(uint32_t*)&_M0L5radixS307;
  _M0L3numS308 = _M0L5valueS305;
  _M0L5countS309 = 0;
  while (1) {
    if (_M0L3numS308 > 0u) {
      uint32_t _M0L6_2atmpS1287 = _M0L3numS308 / _M0L4baseS306;
      int32_t _M0L6_2atmpS1288 = _M0L5countS309 + 1;
      _M0L3numS308 = _M0L6_2atmpS1287;
      _M0L5countS309 = _M0L6_2atmpS1288;
      continue;
    } else {
      return _M0L5countS309;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS303) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS303 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS304;
    int32_t _M0L6_2atmpS1286;
    int32_t _M0L6_2atmpS1285;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS304 = moonbit_clz32(_M0L5valueS303);
    _M0L6_2atmpS1286 = 31 - _M0L14leading__zerosS304;
    _M0L6_2atmpS1285 = _M0L6_2atmpS1286 / 4;
    return _M0L6_2atmpS1285 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS302) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS302 >= 100000u) {
    if (_M0L5valueS302 >= 10000000u) {
      if (_M0L5valueS302 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS302 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS302 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS302 >= 1000u) {
    if (_M0L5valueS302 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS302 >= 100u) {
    return 3;
  } else if (_M0L5valueS302 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS288,
  uint32_t _M0L3numS300,
  int32_t _M0L12digit__startS289,
  int32_t _M0L10total__lenS301
) {
  int32_t _M0L6_2atmpS1284;
  uint32_t _M0L3numS278;
  int32_t _M0L6offsetS279;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1284 = _M0L10total__lenS301 - _M0L12digit__startS289;
  _M0L3numS278 = _M0L3numS300;
  _M0L6offsetS279 = _M0L6_2atmpS1284;
  while (1) {
    if (_M0L3numS278 >= 10000u) {
      uint32_t _M0L1tS280 = _M0L3numS278 / 10000u;
      uint32_t _M0L6_2atmpS1261 = _M0L3numS278 % 10000u;
      int32_t _M0L1rS281 = *(int32_t*)&_M0L6_2atmpS1261;
      int32_t _M0L2d1S282 = _M0L1rS281 / 100;
      int32_t _M0L2d2S283 = _M0L1rS281 % 100;
      int32_t _M0L6_2atmpS1260 = _M0L2d1S282 / 10;
      int32_t _M0L6_2atmpS1259 = 48 + _M0L6_2atmpS1260;
      int32_t _M0L6d1__hiS284 = (uint16_t)_M0L6_2atmpS1259;
      int32_t _M0L6_2atmpS1258 = _M0L2d1S282 % 10;
      int32_t _M0L6_2atmpS1257 = 48 + _M0L6_2atmpS1258;
      int32_t _M0L6d1__loS285 = (uint16_t)_M0L6_2atmpS1257;
      int32_t _M0L6_2atmpS1256 = _M0L2d2S283 / 10;
      int32_t _M0L6_2atmpS1255 = 48 + _M0L6_2atmpS1256;
      int32_t _M0L6d2__hiS286 = (uint16_t)_M0L6_2atmpS1255;
      int32_t _M0L6_2atmpS1254 = _M0L2d2S283 % 10;
      int32_t _M0L6_2atmpS1253 = 48 + _M0L6_2atmpS1254;
      int32_t _M0L6d2__loS287 = (uint16_t)_M0L6_2atmpS1253;
      int32_t _M0L6_2atmpS1245 = _M0L12digit__startS289 + _M0L6offsetS279;
      int32_t _M0L6_2atmpS1244 = _M0L6_2atmpS1245 - 4;
      int32_t _M0L6_2atmpS1247;
      int32_t _M0L6_2atmpS1246;
      int32_t _M0L6_2atmpS1249;
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L6_2atmpS1251;
      int32_t _M0L6_2atmpS1250;
      int32_t _M0L6_2atmpS1252;
      _M0L6bufferS288[_M0L6_2atmpS1244] = _M0L6d1__hiS284;
      _M0L6_2atmpS1247 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1246 = _M0L6_2atmpS1247 - 3;
      _M0L6bufferS288[_M0L6_2atmpS1246] = _M0L6d1__loS285;
      _M0L6_2atmpS1249 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1248 = _M0L6_2atmpS1249 - 2;
      _M0L6bufferS288[_M0L6_2atmpS1248] = _M0L6d2__hiS286;
      _M0L6_2atmpS1251 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1250 = _M0L6_2atmpS1251 - 1;
      _M0L6bufferS288[_M0L6_2atmpS1250] = _M0L6d2__loS287;
      _M0L6_2atmpS1252 = _M0L6offsetS279 - 4;
      _M0L3numS278 = _M0L1tS280;
      _M0L6offsetS279 = _M0L6_2atmpS1252;
      continue;
    } else {
      int32_t _M0L6_2atmpS1283 = *(int32_t*)&_M0L3numS278;
      int32_t _M0L9remainingS291 = _M0L6_2atmpS1283;
      int32_t _M0L6offsetS292 = _M0L6offsetS279;
      while (1) {
        if (_M0L9remainingS291 >= 100) {
          int32_t _M0L1tS293 = _M0L9remainingS291 / 100;
          int32_t _M0L1dS294 = _M0L9remainingS291 % 100;
          int32_t _M0L6_2atmpS1270 = _M0L1dS294 / 10;
          int32_t _M0L6_2atmpS1269 = 48 + _M0L6_2atmpS1270;
          int32_t _M0L5d__hiS295 = (uint16_t)_M0L6_2atmpS1269;
          int32_t _M0L6_2atmpS1268 = _M0L1dS294 % 10;
          int32_t _M0L6_2atmpS1267 = 48 + _M0L6_2atmpS1268;
          int32_t _M0L5d__loS296 = (uint16_t)_M0L6_2atmpS1267;
          int32_t _M0L6_2atmpS1263 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1262 = _M0L6_2atmpS1263 - 2;
          int32_t _M0L6_2atmpS1265;
          int32_t _M0L6_2atmpS1264;
          int32_t _M0L6_2atmpS1266;
          _M0L6bufferS288[_M0L6_2atmpS1262] = _M0L5d__hiS295;
          _M0L6_2atmpS1265 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1264 = _M0L6_2atmpS1265 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1264] = _M0L5d__loS296;
          _M0L6_2atmpS1266 = _M0L6offsetS292 - 2;
          _M0L9remainingS291 = _M0L1tS293;
          _M0L6offsetS292 = _M0L6_2atmpS1266;
          continue;
        } else if (_M0L9remainingS291 >= 10) {
          int32_t _M0L6_2atmpS1278 = _M0L9remainingS291 / 10;
          int32_t _M0L6_2atmpS1277 = 48 + _M0L6_2atmpS1278;
          int32_t _M0L5d__hiS298 = (uint16_t)_M0L6_2atmpS1277;
          int32_t _M0L6_2atmpS1276 = _M0L9remainingS291 % 10;
          int32_t _M0L6_2atmpS1275 = 48 + _M0L6_2atmpS1276;
          int32_t _M0L5d__loS299 = (uint16_t)_M0L6_2atmpS1275;
          int32_t _M0L6_2atmpS1272 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1271 = _M0L6_2atmpS1272 - 2;
          int32_t _M0L6_2atmpS1274;
          int32_t _M0L6_2atmpS1273;
          _M0L6bufferS288[_M0L6_2atmpS1271] = _M0L5d__hiS298;
          _M0L6_2atmpS1274 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1273 = _M0L6_2atmpS1274 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1273] = _M0L5d__loS299;
        } else {
          int32_t _M0L6_2atmpS1282 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1279 = _M0L6_2atmpS1282 - 1;
          int32_t _M0L6_2atmpS1281 = 48 + _M0L9remainingS291;
          int32_t _M0L6_2atmpS1280 = (uint16_t)_M0L6_2atmpS1281;
          _M0L6bufferS288[_M0L6_2atmpS1279] = _M0L6_2atmpS1280;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS268,
  uint32_t _M0L3numS272,
  int32_t _M0L12digit__startS269,
  int32_t _M0L10total__lenS271,
  int32_t _M0L5radixS262
) {
  uint32_t _M0L4baseS261;
  int32_t _M0L6_2atmpS1229;
  int32_t _M0L6_2atmpS1228;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS261 = *(uint32_t*)&_M0L5radixS262;
  _M0L6_2atmpS1229 = _M0L5radixS262 - 1;
  _M0L6_2atmpS1228 = _M0L5radixS262 & _M0L6_2atmpS1229;
  if (_M0L6_2atmpS1228 == 0) {
    int32_t _M0L5shiftS263;
    uint32_t _M0L4maskS264;
    int32_t _M0L6_2atmpS1236;
    int32_t _M0L6offsetS265;
    uint32_t _M0L1nS266;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS263 = moonbit_ctz32(_M0L5radixS262);
    _M0L4maskS264 = _M0L4baseS261 - 1u;
    _M0L6_2atmpS1236 = _M0L10total__lenS271 - _M0L12digit__startS269;
    _M0L6offsetS265 = _M0L6_2atmpS1236;
    _M0L1nS266 = _M0L3numS272;
    while (1) {
      if (_M0L1nS266 > 0u) {
        uint32_t _M0L6_2atmpS1235 = _M0L1nS266 & _M0L4maskS264;
        int32_t _M0L5digitS267 = *(int32_t*)&_M0L6_2atmpS1235;
        int32_t _M0L6_2atmpS1232 = _M0L12digit__startS269 + _M0L6offsetS265;
        int32_t _M0L6_2atmpS1230 = _M0L6_2atmpS1232 - 1;
        int32_t _M0L6_2atmpS1231 =
          ((moonbit_string_t)moonbit_string_literal_14.data)[_M0L5digitS267];
        int32_t _M0L6_2atmpS1233;
        uint32_t _M0L6_2atmpS1234;
        _M0L6bufferS268[_M0L6_2atmpS1230] = _M0L6_2atmpS1231;
        _M0L6_2atmpS1233 = _M0L6offsetS265 - 1;
        _M0L6_2atmpS1234 = _M0L1nS266 >> (_M0L5shiftS263 & 31);
        _M0L6offsetS265 = _M0L6_2atmpS1233;
        _M0L1nS266 = _M0L6_2atmpS1234;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1243 = _M0L10total__lenS271 - _M0L12digit__startS269;
    int32_t _M0L6offsetS273 = _M0L6_2atmpS1243;
    uint32_t _M0L1nS274 = _M0L3numS272;
    while (1) {
      if (_M0L1nS274 > 0u) {
        uint32_t _M0L1qS275 = _M0L1nS274 / _M0L4baseS261;
        uint32_t _M0L6_2atmpS1242 = _M0L1qS275 * _M0L4baseS261;
        uint32_t _M0L6_2atmpS1241 = _M0L1nS274 - _M0L6_2atmpS1242;
        int32_t _M0L5digitS276 = *(int32_t*)&_M0L6_2atmpS1241;
        int32_t _M0L6_2atmpS1239 = _M0L12digit__startS269 + _M0L6offsetS273;
        int32_t _M0L6_2atmpS1237 = _M0L6_2atmpS1239 - 1;
        int32_t _M0L6_2atmpS1238 =
          ((moonbit_string_t)moonbit_string_literal_14.data)[_M0L5digitS276];
        int32_t _M0L6_2atmpS1240;
        _M0L6bufferS268[_M0L6_2atmpS1237] = _M0L6_2atmpS1238;
        _M0L6_2atmpS1240 = _M0L6offsetS273 - 1;
        _M0L6offsetS273 = _M0L6_2atmpS1240;
        _M0L1nS274 = _M0L1qS275;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS255,
  uint32_t _M0L3numS260,
  int32_t _M0L12digit__startS256,
  int32_t _M0L10total__lenS259
) {
  int32_t _M0L6_2atmpS1227;
  int32_t _M0L6offsetS250;
  uint32_t _M0L1nS251;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1227 = _M0L10total__lenS259 - _M0L12digit__startS256;
  _M0L6offsetS250 = _M0L6_2atmpS1227;
  _M0L1nS251 = _M0L3numS260;
  while (1) {
    if (_M0L6offsetS250 >= 2) {
      uint32_t _M0L6_2atmpS1224 = _M0L1nS251 & 255u;
      int32_t _M0L9byte__valS252 = *(int32_t*)&_M0L6_2atmpS1224;
      int32_t _M0L2hiS253 = _M0L9byte__valS252 / 16;
      int32_t _M0L2loS254 = _M0L9byte__valS252 % 16;
      int32_t _M0L6_2atmpS1218 = _M0L12digit__startS256 + _M0L6offsetS250;
      int32_t _M0L6_2atmpS1216 = _M0L6_2atmpS1218 - 2;
      int32_t _M0L6_2atmpS1217 =
        ((moonbit_string_t)moonbit_string_literal_14.data)[_M0L2hiS253];
      int32_t _M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1222;
      uint32_t _M0L6_2atmpS1223;
      _M0L6bufferS255[_M0L6_2atmpS1216] = _M0L6_2atmpS1217;
      _M0L6_2atmpS1221 = _M0L12digit__startS256 + _M0L6offsetS250;
      _M0L6_2atmpS1219 = _M0L6_2atmpS1221 - 1;
      _M0L6_2atmpS1220
      = ((moonbit_string_t)moonbit_string_literal_14.data)[
        _M0L2loS254
      ];
      _M0L6bufferS255[_M0L6_2atmpS1219] = _M0L6_2atmpS1220;
      _M0L6_2atmpS1222 = _M0L6offsetS250 - 2;
      _M0L6_2atmpS1223 = _M0L1nS251 >> 8;
      _M0L6offsetS250 = _M0L6_2atmpS1222;
      _M0L1nS251 = _M0L6_2atmpS1223;
      continue;
    } else if (_M0L6offsetS250 == 1) {
      uint32_t _M0L6_2atmpS1226 = _M0L1nS251 & 15u;
      int32_t _M0L6nibbleS258 = *(int32_t*)&_M0L6_2atmpS1226;
      int32_t _M0L6_2atmpS1225 =
        ((moonbit_string_t)moonbit_string_literal_14.data)[_M0L6nibbleS258];
      _M0L6bufferS255[_M0L12digit__startS256] = _M0L6_2atmpS1225;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS249
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS248;
  struct _M0TPB6Logger _M0L6_2atmpS1215;
  moonbit_string_t _result_2055;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS248);
  _M0L6_2atmpS1215
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS248
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS249, _M0L6_2atmpS1215);
  if (_M0L6_2atmpS1215.$1) {
    moonbit_decref(_M0L6_2atmpS1215.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2055 = _M0MPB13StringBuilder10to__string(_M0L6loggerS248);
  moonbit_decref_cycle_free(_M0L6loggerS248);
  return _result_2055;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS245,
  struct _M0TPB6Logger _M0L6loggerS244
) {
  moonbit_string_t _M0L6_2atmpS1213;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1213 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS245);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS244.$0->$method_0(_M0L6loggerS244.$1, _M0L6_2atmpS1213);
  moonbit_decref_cycle_free(_M0L6_2atmpS1213);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS247,
  struct _M0TPB6Logger _M0L6loggerS246
) {
  moonbit_string_t _M0L6_2atmpS1214;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1214 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS247);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS246.$0->$method_0(_M0L6loggerS246.$1, _M0L6_2atmpS1214);
  moonbit_decref_cycle_free(_M0L6_2atmpS1214);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS243
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS243.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS242
) {
  moonbit_string_t _M0L8_2afieldS1935;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1935 = _M0L4selfS242.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1935);
  return _M0L8_2afieldS1935;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS238,
  moonbit_string_t _M0L5valueS239,
  int32_t _M0L5startS240,
  int32_t _M0L3lenS241
) {
  int32_t _M0L6_2atmpS1212;
  int64_t _M0L6_2atmpS1211;
  struct _M0TPC16string10StringView _M0L6_2atmpS1210;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1212 = _M0L5startS240 + _M0L3lenS241;
  _M0L6_2atmpS1211 = (int64_t)_M0L6_2atmpS1212;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1210
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS239, _M0L5startS240, _M0L6_2atmpS1211);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS238, _M0L6_2atmpS1210);
  moonbit_decref_cycle_free(_M0L6_2atmpS1210.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS231,
  int32_t _M0L5startS233,
  int64_t _M0L3endS235
) {
  int32_t _M0L3lenS230;
  int32_t _M0Lm2loS232;
  int32_t _M0Lm2hiS234;
  int32_t _M0L6_2atmpS1194;
  int32_t _if__result_2056;
  int32_t _M0L6_2atmpS1202;
  int32_t _if__result_2057;
  int32_t _M0L6_2atmpS1204;
  int32_t _M0L6_2atmpS1205;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS230 = Moonbit_array_length(_M0L4selfS231);
  if (_M0L5startS233 < 0) {
    _M0Lm2loS232 = 0;
  } else if (_M0L5startS233 > _M0L3lenS230) {
    _M0Lm2loS232 = _M0L3lenS230;
  } else {
    _M0Lm2loS232 = _M0L5startS233;
  }
  if (_M0L3endS235 == 4294967296ll) {
    _M0Lm2hiS234 = _M0L3lenS230;
  } else {
    int64_t _M0L7_2aSomeS236 = _M0L3endS235;
    int32_t _M0L4_2aeS237 = (int32_t)_M0L7_2aSomeS236;
    if (_M0L4_2aeS237 < 0) {
      _M0Lm2hiS234 = 0;
    } else if (_M0L4_2aeS237 > _M0L3lenS230) {
      _M0Lm2hiS234 = _M0L3lenS230;
    } else {
      _M0Lm2hiS234 = _M0L4_2aeS237;
    }
  }
  _M0L6_2atmpS1194 = _M0Lm2loS232;
  if (_M0L6_2atmpS1194 > 0) {
    int32_t _M0L6_2atmpS1193 = _M0Lm2loS232;
    if (_M0L6_2atmpS1193 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1192 = _M0Lm2loS232;
      int32_t _M0L6_2atmpS1191 = _M0L4selfS231[_M0L6_2atmpS1192];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1191)) {
        int32_t _M0L6_2atmpS1190 = _M0Lm2loS232;
        int32_t _M0L6_2atmpS1189 = _M0L6_2atmpS1190 - 1;
        int32_t _M0L6_2atmpS1188 = _M0L4selfS231[_M0L6_2atmpS1189];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2056
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1188);
      } else {
        _if__result_2056 = 0;
      }
    } else {
      _if__result_2056 = 0;
    }
  } else {
    _if__result_2056 = 0;
  }
  if (_if__result_2056) {
    int32_t _M0L6_2atmpS1195 = _M0Lm2loS232;
    _M0Lm2loS232 = _M0L6_2atmpS1195 + 1;
  }
  _M0L6_2atmpS1202 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1202 > 0) {
    int32_t _M0L6_2atmpS1201 = _M0Lm2hiS234;
    if (_M0L6_2atmpS1201 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1200 = _M0Lm2hiS234;
      int32_t _M0L6_2atmpS1199 = _M0L4selfS231[_M0L6_2atmpS1200];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1199)) {
        int32_t _M0L6_2atmpS1198 = _M0Lm2hiS234;
        int32_t _M0L6_2atmpS1197 = _M0L6_2atmpS1198 - 1;
        int32_t _M0L6_2atmpS1196 = _M0L4selfS231[_M0L6_2atmpS1197];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2057
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1196);
      } else {
        _if__result_2057 = 0;
      }
    } else {
      _if__result_2057 = 0;
    }
  } else {
    _if__result_2057 = 0;
  }
  if (_if__result_2057) {
    int32_t _M0L6_2atmpS1203 = _M0Lm2hiS234;
    _M0Lm2hiS234 = _M0L6_2atmpS1203 - 1;
  }
  _M0L6_2atmpS1204 = _M0Lm2loS232;
  _M0L6_2atmpS1205 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1204 >= _M0L6_2atmpS1205) {
    int32_t _M0L6_2atmpS1206 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1207 = _M0Lm2loS232;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1206,
                                                 .$2 = _M0L6_2atmpS1207};
  } else {
    int32_t _M0L6_2atmpS1208 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1209 = _M0Lm2hiS234;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1208,
                                                 .$2 = _M0L6_2atmpS1209};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS229,
  struct _M0TPB4Show _M0L4showS228
) {
  struct _M0TPB6Logger _M0L6_2atmpS1187;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS1187
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS229
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS228.$0->$method_0(_M0L4showS228.$1, _M0L6_2atmpS1187);
  if (_M0L6_2atmpS1187.$1) {
    moonbit_decref(_M0L6_2atmpS1187.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS227,
  struct _M0TPB4Show _M0L4showS226
) {
  struct _M0TPB6Logger _M0L6_2atmpS1186;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1186
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS227
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS226.$0->$method_0(_M0L4showS226.$1, _M0L6_2atmpS1186);
  if (_M0L6_2atmpS1186.$1) {
    moonbit_decref(_M0L6_2atmpS1186.$1);
  }
  return 0;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS224,
  int32_t _M0L5quoteS225
) {
  struct _M0TPB13StringBuilder* _M0L3bufS223;
  int32_t _M0L6_2atmpS1185;
  struct _M0TPC16string10StringView _M0L6_2atmpS1183;
  struct _M0TPB6Logger _M0L6_2atmpS1184;
  moonbit_string_t _result_2058;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS223 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1185 = Moonbit_array_length(_M0L4selfS224);
  moonbit_incref_cycle_free(_M0L4selfS224);
  _M0L6_2atmpS1183
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS224, .$1 = 0, .$2 = _M0L6_2atmpS1185
  };
  moonbit_incref_cycle_free(_M0L3bufS223);
  _M0L6_2atmpS1184
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS223
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1183, _M0L6_2atmpS1184, _M0L5quoteS225);
  moonbit_decref_cycle_free(_M0L6_2atmpS1183.$0);
  if (_M0L6_2atmpS1184.$1) {
    moonbit_decref(_M0L6_2atmpS1184.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2058 = _M0MPB13StringBuilder10to__string(_M0L3bufS223);
  moonbit_decref_cycle_free(_M0L3bufS223);
  return _result_2058;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS213,
  int32_t _M0L5quoteS212
) {
  int32_t _M0L3endS1181;
  int32_t _M0L5startS1182;
  int32_t _M0L3lenS214;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS216;
  int32_t _M0L1iS217;
  int32_t _M0L3segS218;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS212) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  _M0L3endS1181 = _M0L4selfS215.$2;
  _M0L5startS1182 = _M0L4selfS215.$1;
  _M0L3lenS214 = _M0L3endS1181 - _M0L5startS1182;
  moonbit_incref_cycle_free(_M0L4selfS215.$0);
  if (_M0L6loggerS213.$1) {
    moonbit_incref(_M0L6loggerS213.$1);
  }
  _M0L6_2aenvS216
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _M0L6_2aenvS216->$0 = _M0L4selfS215;
  _M0L6_2aenvS216->$1 = _M0L6loggerS213;
  _M0L1iS217 = 0;
  _M0L3segS218 = 0;
  _2afor_219:;
  while (1) {
    moonbit_string_t _M0L3strS1178;
    int32_t _M0L5startS1180;
    int32_t _M0L6_2atmpS1179;
    int32_t _M0L4codeS220;
    int32_t _M0L1cS222;
    int32_t _M0L6_2atmpS1162;
    int32_t _M0L6_2atmpS1163;
    int32_t _M0L6_2atmpS1164;
    if (_M0L1iS217 >= _M0L3lenS214) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
      moonbit_decref_cycle_free(_M0L6_2aenvS216);
      break;
    }
    _M0L3strS1178 = _M0L4selfS215.$0;
    _M0L5startS1180 = _M0L4selfS215.$1;
    _M0L6_2atmpS1179 = _M0L5startS1180 + _M0L1iS217;
    _M0L4codeS220 = _M0L3strS1178[_M0L6_2atmpS1179];
    switch (_M0L4codeS220) {
      case 34: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 92: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1165;
        int32_t _M0L6_2atmpS1166;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_15.data);
        _M0L6_2atmpS1165 = _M0L1iS217 + 1;
        _M0L6_2atmpS1166 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1165;
        _M0L3segS218 = _M0L6_2atmpS1166;
        goto _2afor_219;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1167;
        int32_t _M0L6_2atmpS1168;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS1167 = _M0L1iS217 + 1;
        _M0L6_2atmpS1168 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1167;
        _M0L3segS218 = _M0L6_2atmpS1168;
        goto _2afor_219;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1169;
        int32_t _M0L6_2atmpS1170;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_17.data);
        _M0L6_2atmpS1169 = _M0L1iS217 + 1;
        _M0L6_2atmpS1170 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1169;
        _M0L3segS218 = _M0L6_2atmpS1170;
        goto _2afor_219;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1171;
        int32_t _M0L6_2atmpS1172;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1171 = _M0L1iS217 + 1;
        _M0L6_2atmpS1172 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1171;
        _M0L3segS218 = _M0L6_2atmpS1172;
        goto _2afor_219;
        break;
      }
      default: {
        if (_M0L4codeS220 < 32) {
          int32_t _M0L6_2atmpS1174;
          moonbit_string_t _M0L6_2atmpS1173;
          int32_t _M0L6_2atmpS1175;
          int32_t _M0L6_2atmpS1176;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_19.data);
          _M0L6_2atmpS1174 = _M0L4codeS220 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1173 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1174);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, _M0L6_2atmpS1173);
          moonbit_decref_cycle_free(_M0L6_2atmpS1173);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1175 = _M0L1iS217 + 1;
          _M0L6_2atmpS1176 = _M0L1iS217 + 1;
          _M0L1iS217 = _M0L6_2atmpS1175;
          _M0L3segS218 = _M0L6_2atmpS1176;
          goto _2afor_219;
        } else {
          int32_t _M0L6_2atmpS1177 = _M0L1iS217 + 1;
          int32_t _tmp_2061 = _M0L3segS218;
          _M0L1iS217 = _M0L6_2atmpS1177;
          _M0L3segS218 = _tmp_2061;
          goto _2afor_219;
        }
        break;
      }
    }
    goto joinlet_2060;
    join_221:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1162 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS222);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, _M0L6_2atmpS1162);
    _M0L6_2atmpS1163 = _M0L1iS217 + 1;
    _M0L6_2atmpS1164 = _M0L1iS217 + 1;
    _M0L1iS217 = _M0L6_2atmpS1163;
    _M0L3segS218 = _M0L6_2atmpS1164;
    continue;
    joinlet_2060:;
    break;
  }
  if (_M0L5quoteS212) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS208,
  int32_t _M0L3segS211,
  int32_t _M0L1iS210
) {
  struct _M0TPB6Logger _M0L6loggerS207;
  struct _M0TPC16string10StringView _M0L4selfS209;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS207 = _M0L6_2aenvS208->$1;
  _M0L4selfS209 = _M0L6_2aenvS208->$0;
  if (_M0L1iS210 > _M0L3segS211) {
    int64_t _M0L6_2atmpS1161 = (int64_t)_M0L1iS210;
    struct _M0TPC16string10StringView _M0L6_2atmpS1160;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1160
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS209, _M0L3segS211, _M0L6_2atmpS1161);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS207.$0->$method_2(_M0L6loggerS207.$1, _M0L6_2atmpS1160);
    moonbit_decref_cycle_free(_M0L6_2atmpS1160.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS198,
  int32_t _M0L5startS200,
  int64_t _M0L3endS202
) {
  int32_t _M0L3endS1158;
  int32_t _M0L5startS1159;
  int32_t _M0L3lenS197;
  int32_t _M0Lm2loS199;
  int32_t _M0Lm2hiS201;
  moonbit_string_t _M0L3strS205;
  int32_t _M0L4baseS206;
  int32_t _M0L6_2atmpS1136;
  int32_t _if__result_2062;
  int32_t _M0L6_2atmpS1146;
  int32_t _if__result_2063;
  int32_t _M0L6_2atmpS1148;
  int32_t _M0L6_2atmpS1149;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1158 = _M0L4selfS198.$2;
  _M0L5startS1159 = _M0L4selfS198.$1;
  _M0L3lenS197 = _M0L3endS1158 - _M0L5startS1159;
  if (_M0L5startS200 < 0) {
    _M0Lm2loS199 = 0;
  } else if (_M0L5startS200 > _M0L3lenS197) {
    _M0Lm2loS199 = _M0L3lenS197;
  } else {
    _M0Lm2loS199 = _M0L5startS200;
  }
  if (_M0L3endS202 == 4294967296ll) {
    _M0Lm2hiS201 = _M0L3lenS197;
  } else {
    int64_t _M0L7_2aSomeS203 = _M0L3endS202;
    int32_t _M0L4_2aeS204 = (int32_t)_M0L7_2aSomeS203;
    if (_M0L4_2aeS204 < 0) {
      _M0Lm2hiS201 = 0;
    } else if (_M0L4_2aeS204 > _M0L3lenS197) {
      _M0Lm2hiS201 = _M0L3lenS197;
    } else {
      _M0Lm2hiS201 = _M0L4_2aeS204;
    }
  }
  _M0L3strS205 = _M0L4selfS198.$0;
  _M0L4baseS206 = _M0L4selfS198.$1;
  _M0L6_2atmpS1136 = _M0Lm2loS199;
  if (_M0L6_2atmpS1136 > 0) {
    int32_t _M0L6_2atmpS1135 = _M0Lm2loS199;
    if (_M0L6_2atmpS1135 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1134 = _M0Lm2loS199;
      int32_t _M0L6_2atmpS1133 = _M0L4baseS206 + _M0L6_2atmpS1134;
      int32_t _M0L6_2atmpS1132 = _M0L3strS205[_M0L6_2atmpS1133];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1132)) {
        int32_t _M0L6_2atmpS1131 = _M0Lm2loS199;
        int32_t _M0L6_2atmpS1130 = _M0L4baseS206 + _M0L6_2atmpS1131;
        int32_t _M0L6_2atmpS1129 = _M0L6_2atmpS1130 - 1;
        int32_t _M0L6_2atmpS1128 = _M0L3strS205[_M0L6_2atmpS1129];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2062
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1128);
      } else {
        _if__result_2062 = 0;
      }
    } else {
      _if__result_2062 = 0;
    }
  } else {
    _if__result_2062 = 0;
  }
  if (_if__result_2062) {
    int32_t _M0L6_2atmpS1137 = _M0Lm2loS199;
    _M0Lm2loS199 = _M0L6_2atmpS1137 + 1;
  }
  _M0L6_2atmpS1146 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1146 > 0) {
    int32_t _M0L6_2atmpS1145 = _M0Lm2hiS201;
    if (_M0L6_2atmpS1145 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1144 = _M0Lm2hiS201;
      int32_t _M0L6_2atmpS1143 = _M0L4baseS206 + _M0L6_2atmpS1144;
      int32_t _M0L6_2atmpS1142 = _M0L3strS205[_M0L6_2atmpS1143];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1142)) {
        int32_t _M0L6_2atmpS1141 = _M0Lm2hiS201;
        int32_t _M0L6_2atmpS1140 = _M0L4baseS206 + _M0L6_2atmpS1141;
        int32_t _M0L6_2atmpS1139 = _M0L6_2atmpS1140 - 1;
        int32_t _M0L6_2atmpS1138 = _M0L3strS205[_M0L6_2atmpS1139];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2063
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1138);
      } else {
        _if__result_2063 = 0;
      }
    } else {
      _if__result_2063 = 0;
    }
  } else {
    _if__result_2063 = 0;
  }
  if (_if__result_2063) {
    int32_t _M0L6_2atmpS1147 = _M0Lm2hiS201;
    _M0Lm2hiS201 = _M0L6_2atmpS1147 - 1;
  }
  _M0L6_2atmpS1148 = _M0Lm2loS199;
  _M0L6_2atmpS1149 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1148 >= _M0L6_2atmpS1149) {
    int32_t _M0L6_2atmpS1153 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1150 = _M0L4baseS206 + _M0L6_2atmpS1153;
    int32_t _M0L6_2atmpS1152 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1151 = _M0L4baseS206 + _M0L6_2atmpS1152;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1150,
                                                 .$2 = _M0L6_2atmpS1151};
  } else {
    int32_t _M0L6_2atmpS1157 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1154 = _M0L4baseS206 + _M0L6_2atmpS1157;
    int32_t _M0L6_2atmpS1156 = _M0Lm2hiS201;
    int32_t _M0L6_2atmpS1155 = _M0L4baseS206 + _M0L6_2atmpS1156;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1154,
                                                 .$2 = _M0L6_2atmpS1155};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS196) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS195;
  int32_t _M0L6_2atmpS1125;
  int32_t _M0L6_2atmpS1124;
  int32_t _M0L6_2atmpS1127;
  int32_t _M0L6_2atmpS1126;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1123;
  moonbit_string_t _result_2064;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS195 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1125 = _M0IPC14byte4BytePB3Div3div(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1124
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1125);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1124);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1127 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1126
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1127);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1126);
  _M0L6_2atmpS1123 = _M0L7_2aselfS195;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2064 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1123);
  moonbit_decref_cycle_free(_M0L6_2atmpS1123);
  return _result_2064;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS194) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS194 < 10) {
    int32_t _M0L6_2atmpS1120;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1120 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1120);
  } else {
    int32_t _M0L6_2atmpS1122;
    int32_t _M0L6_2atmpS1121;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1122 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1121 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1122, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1121);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1118;
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1117;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1118 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1119 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1117 = _M0L6_2atmpS1118 - _M0L6_2atmpS1119;
  return _M0L6_2atmpS1117 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1115;
  int32_t _M0L6_2atmpS1116;
  int32_t _M0L6_2atmpS1114;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1115 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1116 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1114 = _M0L6_2atmpS1115 % _M0L6_2atmpS1116;
  return _M0L6_2atmpS1114 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS188,
  int32_t _M0L4thatS189
) {
  int32_t _M0L6_2atmpS1112;
  int32_t _M0L6_2atmpS1113;
  int32_t _M0L6_2atmpS1111;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1112 = (int32_t)_M0L4selfS188;
  _M0L6_2atmpS1113 = (int32_t)_M0L4thatS189;
  _M0L6_2atmpS1111 = _M0L6_2atmpS1112 / _M0L6_2atmpS1113;
  return _M0L6_2atmpS1111 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS186,
  int32_t _M0L4thatS187
) {
  int32_t _M0L6_2atmpS1109;
  int32_t _M0L6_2atmpS1110;
  int32_t _M0L6_2atmpS1108;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1109 = (int32_t)_M0L4selfS186;
  _M0L6_2atmpS1110 = (int32_t)_M0L4thatS187;
  _M0L6_2atmpS1108 = _M0L6_2atmpS1109 + _M0L6_2atmpS1110;
  return _M0L6_2atmpS1108 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS185) {
  int32_t _M0L6_2atmpS1107;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1107 = (int32_t)_M0L4selfS185;
  return _M0L6_2atmpS1107;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS184) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS184 >= 56320 && _M0L4selfS184 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS183) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS183 >= 55296 && _M0L4selfS183 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS182,
  moonbit_string_t _M0L3strS180
) {
  int32_t _M0L8str__lenS179;
  int32_t _M0L3lenS1106;
  int32_t _M0L8requiredS181;
  uint16_t* _M0L4dataS1101;
  int32_t _M0L6_2atmpS1100;
  int32_t _if__result_2065;
  uint16_t* _M0L4dataS1102;
  int32_t _M0L3lenS1103;
  int32_t _M0L3lenS1105;
  int32_t _M0L6_2atmpS1104;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS179 = Moonbit_array_length(_M0L3strS180);
  if (_M0L8str__lenS179 == 0) {
    return 0;
  }
  _M0L3lenS1106 = _M0L4selfS182->$1;
  _M0L8requiredS181 = _M0L3lenS1106 + _M0L8str__lenS179;
  _M0L4dataS1101 = _M0L4selfS182->$0;
  _M0L6_2atmpS1100 = Moonbit_array_length(_M0L4dataS1101);
  if (_M0L8requiredS181 > _M0L6_2atmpS1100) {
    _if__result_2065 = 1;
  } else {
    int32_t _M0L3lenS1099 = _M0L4selfS182->$1;
    _if__result_2065 = _M0L8requiredS181 < _M0L3lenS1099;
  }
  if (_if__result_2065) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS182, _M0L8requiredS181);
  }
  _M0L4dataS1102 = _M0L4selfS182->$0;
  _M0L3lenS1103 = _M0L4selfS182->$1;
  moonbit_incref_cycle_free(_M0L4dataS1102);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1102, _M0L3lenS1103, _M0L3strS180, 0, _M0L8str__lenS179);
  moonbit_decref_cycle_free(_M0L4dataS1102);
  _M0L3lenS1105 = _M0L4selfS182->$1;
  _M0L6_2atmpS1104 = _M0L3lenS1105 + _M0L8str__lenS179;
  _M0L4selfS182->$1 = _M0L6_2atmpS1104;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS175,
  int32_t _M0L11dst__offsetS178,
  moonbit_string_t _M0L3strS176,
  int32_t _M0L11str__offsetS171,
  int32_t _M0L3lenS172
) {
  int32_t _M0L16end__str__offsetS170;
  int32_t _M0L1iS173;
  int32_t _M0L1jS174;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS170 = _M0L11str__offsetS171 + _M0L3lenS172;
  _M0L1iS173 = _M0L11str__offsetS171;
  _M0L1jS174 = _M0L11dst__offsetS178;
  while (1) {
    if (_M0L1iS173 < _M0L16end__str__offsetS170) {
      int32_t _M0L6_2atmpS1096 = _M0L3strS176[_M0L1iS173];
      int32_t _M0L6_2atmpS1097;
      int32_t _M0L6_2atmpS1098;
      _M0L4selfS175[_M0L1jS174] = _M0L6_2atmpS1096;
      _M0L6_2atmpS1097 = _M0L1iS173 + 1;
      _M0L6_2atmpS1098 = _M0L1jS174 + 1;
      _M0L1iS173 = _M0L6_2atmpS1097;
      _M0L1jS174 = _M0L6_2atmpS1098;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS168,
  int32_t _M0L2chS167
) {
  uint32_t _M0L4codeS166;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS166 = _M0MPC14char4Char8to__uint(_M0L2chS167);
  if (_M0L4codeS166 <= 65535u) {
    int32_t _M0L3lenS1067 = _M0L4selfS168->$1;
    uint16_t* _M0L4dataS1069 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1068 = Moonbit_array_length(_M0L4dataS1069);
    uint16_t* _M0L4dataS1072;
    int32_t _M0L3lenS1073;
    int32_t _M0L6_2atmpS1074;
    int32_t _M0L3lenS1076;
    int32_t _M0L6_2atmpS1075;
    if (_M0L3lenS1067 >= _M0L6_2atmpS1068) {
      int32_t _M0L3lenS1071 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1070 = _M0L3lenS1071 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1070);
    }
    _M0L4dataS1072 = _M0L4selfS168->$0;
    _M0L3lenS1073 = _M0L4selfS168->$1;
    moonbit_incref_cycle_free(_M0L4dataS1072);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1074 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS166);
    if (
      _M0L3lenS1073 < 0
      || _M0L3lenS1073 >= Moonbit_array_length(_M0L4dataS1072)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1072[_M0L3lenS1073] = _M0L6_2atmpS1074;
    moonbit_decref_cycle_free(_M0L4dataS1072);
    _M0L3lenS1076 = _M0L4selfS168->$1;
    _M0L6_2atmpS1075 = _M0L3lenS1076 + 1;
    _M0L4selfS168->$1 = _M0L6_2atmpS1075;
  } else if (_M0L4codeS166 <= 1114111u) {
    uint16_t* _M0L4dataS1080 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1078 = Moonbit_array_length(_M0L4dataS1080);
    int32_t _M0L3lenS1079 = _M0L4selfS168->$1;
    int32_t _M0L6_2atmpS1077 = _M0L6_2atmpS1078 - _M0L3lenS1079;
    uint32_t _M0L4codeS169;
    uint16_t* _M0L4dataS1083;
    int32_t _M0L3lenS1084;
    uint32_t _M0L6_2atmpS1087;
    uint32_t _M0L6_2atmpS1086;
    int32_t _M0L6_2atmpS1085;
    uint16_t* _M0L4dataS1088;
    int32_t _M0L3lenS1093;
    int32_t _M0L6_2atmpS1089;
    uint32_t _M0L6_2atmpS1092;
    uint32_t _M0L6_2atmpS1091;
    int32_t _M0L6_2atmpS1090;
    int32_t _M0L3lenS1095;
    int32_t _M0L6_2atmpS1094;
    if (_M0L6_2atmpS1077 < 2) {
      int32_t _M0L3lenS1082 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1081 = _M0L3lenS1082 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1081);
    }
    _M0L4codeS169 = _M0L4codeS166 - 65536u;
    _M0L4dataS1083 = _M0L4selfS168->$0;
    _M0L3lenS1084 = _M0L4selfS168->$1;
    _M0L6_2atmpS1087 = _M0L4codeS169 >> 10;
    _M0L6_2atmpS1086 = 55296u + _M0L6_2atmpS1087;
    moonbit_incref_cycle_free(_M0L4dataS1083);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1085 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1086);
    if (
      _M0L3lenS1084 < 0
      || _M0L3lenS1084 >= Moonbit_array_length(_M0L4dataS1083)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1083[_M0L3lenS1084] = _M0L6_2atmpS1085;
    moonbit_decref_cycle_free(_M0L4dataS1083);
    _M0L4dataS1088 = _M0L4selfS168->$0;
    _M0L3lenS1093 = _M0L4selfS168->$1;
    _M0L6_2atmpS1089 = _M0L3lenS1093 + 1;
    _M0L6_2atmpS1092 = _M0L4codeS169 & 1023u;
    _M0L6_2atmpS1091 = 56320u + _M0L6_2atmpS1092;
    moonbit_incref_cycle_free(_M0L4dataS1088);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1090 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1091);
    if (
      _M0L6_2atmpS1089 < 0
      || _M0L6_2atmpS1089 >= Moonbit_array_length(_M0L4dataS1088)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1088[_M0L6_2atmpS1089] = _M0L6_2atmpS1090;
    moonbit_decref_cycle_free(_M0L4dataS1088);
    _M0L3lenS1095 = _M0L4selfS168->$1;
    _M0L6_2atmpS1094 = _M0L3lenS1095 + 2;
    _M0L4selfS168->$1 = _M0L6_2atmpS1094;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS163,
  int32_t _M0L8requiredS164
) {
  uint16_t* _M0L4dataS1066;
  int32_t _M0L6_2atmpS1064;
  int32_t _M0L3lenS1065;
  int32_t _M0L13new__capacityS162;
  uint16_t* _M0L4dataS1061;
  int32_t _M0L6_2atmpS1062;
  int32_t _M0L3lenS1063;
  uint16_t* _M0L9new__dataS165;
  uint16_t* _M0L6_2aoldS1936;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1066 = _M0L4selfS163->$0;
  _M0L6_2atmpS1064 = Moonbit_array_length(_M0L4dataS1066);
  _M0L3lenS1065 = _M0L4selfS163->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS162
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1064, _M0L3lenS1065, _M0L8requiredS164);
  _M0L4dataS1061 = _M0L4selfS163->$0;
  moonbit_incref_cycle_free(_M0L4dataS1061);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1062 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1063 = _M0L4selfS163->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS165
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1061, _M0L13new__capacityS162, _M0L6_2atmpS1062, _M0L3lenS1063, 0, 0);
  _M0L6_2aoldS1936 = _M0L4selfS163->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1936);
  _M0L4selfS163->$0 = _M0L9new__dataS165;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS161,
  int32_t _M0L3lenS157,
  int32_t _M0L8requiredS156
) {
  int32_t _M0L5spaceS158;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS156 < _M0L3lenS157) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  _M0L5spaceS158 = _M0L7currentS161;
  while (1) {
    if (_M0L5spaceS158 < _M0L8requiredS156) {
      int32_t _M0L4nextS159 = _M0L5spaceS158 * 2;
      if (_M0L4nextS159 <= _M0L5spaceS158) {
        return _M0L8requiredS156;
      }
      _M0L5spaceS158 = _M0L4nextS159;
      continue;
    } else {
      return _M0L5spaceS158;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS155) {
  int32_t _M0L6_2atmpS1060;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1060 = *(int32_t*)&_M0L4selfS155;
  return (uint16_t)_M0L6_2atmpS1060;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS154) {
  int32_t _M0L6_2atmpS1059;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1059 = _M0L4selfS154;
  return *(uint32_t*)&_M0L6_2atmpS1059;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS152
) {
  int32_t _M0L3lenS1050;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1050 = _M0L4selfS152->$1;
  if (_M0L3lenS1050 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1051 = _M0L4selfS152->$1;
    uint16_t* _M0L4dataS1053 = _M0L4selfS152->$0;
    int32_t _M0L6_2atmpS1052 = Moonbit_array_length(_M0L4dataS1053);
    if (_M0L3lenS1051 == _M0L6_2atmpS1052) {
      uint16_t* _M0L4dataS1054 = _M0L4selfS152->$0;
      moonbit_incref_cycle_free(_M0L4dataS1054);
      return _M0L4dataS1054;
    } else {
      uint16_t* _M0L4dataS1055 = _M0L4selfS152->$0;
      int32_t _M0L3lenS1056 = _M0L4selfS152->$1;
      int32_t _M0L6_2atmpS1057;
      int32_t _M0L3lenS1058;
      uint16_t* _M0L4dataS153;
      moonbit_incref_cycle_free(_M0L4dataS1055);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1057 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1058 = _M0L4selfS152->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS153
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1055, _M0L3lenS1056, _M0L6_2atmpS1057, _M0L3lenS1058, 0, 0);
      return _M0L4dataS153;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS149,
  int32_t _M0L13allocate__lenS145,
  int32_t _M0L4initS150,
  int32_t _M0L3lenS146,
  int32_t _M0L11src__offsetS147,
  int32_t _M0L11dst__offsetS148
) {
  int32_t _if__result_2068;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS145 >= 0) {
    if (_M0L3lenS146 >= 0) {
      if (_M0L11src__offsetS147 >= 0) {
        if (_M0L11dst__offsetS148 >= 0) {
          int32_t _M0L6_2atmpS1046 = _M0L11src__offsetS147 + _M0L3lenS146;
          int32_t _M0L6_2atmpS1047 = Moonbit_array_length(_M0L3srcS149);
          if (_M0L6_2atmpS1046 <= _M0L6_2atmpS1047) {
            int32_t _M0L6_2atmpS1045 = _M0L11dst__offsetS148 + _M0L3lenS146;
            _if__result_2068 = _M0L6_2atmpS1045 <= _M0L13allocate__lenS145;
          } else {
            _if__result_2068 = 0;
          }
        } else {
          _if__result_2068 = 0;
        }
      } else {
        _if__result_2068 = 0;
      }
    } else {
      _if__result_2068 = 0;
    }
  } else {
    _if__result_2068 = 0;
  }
  if (_if__result_2068) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS149, _M0L13allocate__lenS145, _M0L4initS150, _M0L11src__offsetS147, _M0L11dst__offsetS148, _M0L3lenS146);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS151;
    int32_t _M0L6_2atmpS1049;
    moonbit_string_t _M0L6_2atmpS1048;
    uint16_t* _result_2069;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS151
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L13allocate__lenS145);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11src__offsetS147);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11dst__offsetS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L3lenS146);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_26.data);
    _M0L6_2atmpS1049 = Moonbit_array_length(_M0L3srcS149);
    moonbit_decref_cycle_free(_M0L3srcS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L6_2atmpS1049);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1048
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS151);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS151);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2069 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1048);
    moonbit_decref_cycle_free(_M0L6_2atmpS1048);
    return _result_2069;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS142,
  int32_t _M0L13allocate__lenS139,
  int32_t _M0L4initS140,
  int32_t _M0L11src__offsetS143,
  int32_t _M0L11dst__offsetS141,
  int32_t _M0L9blit__lenS144
) {
  uint16_t* _M0L3dstS138;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS138
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS139, _M0L4initS140);
  moonbit_incref_cycle_free(_M0L3dstS138);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS138, _M0L11dst__offsetS141, _M0L3srcS142, _M0L11src__offsetS143, _M0L9blit__lenS144, sizeof(uint16_t));
  return _M0L3dstS138;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS136
) {
  int32_t _M0L7initialS135;
  uint16_t* _M0L4dataS137;
  struct _M0TPB13StringBuilder* _block_2070;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS136 < 1) {
    _M0L7initialS135 = 1;
  } else {
    int32_t _M0L6_2atmpS1044 = _M0L10size__hintS136 + 1;
    _M0L7initialS135 = _M0L6_2atmpS1044 / 2;
  }
  _M0L4dataS137 = (uint16_t*)moonbit_make_string(_M0L7initialS135, 0);
  _block_2070
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2070)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 95, 0);
  _block_2070->$0 = _M0L4dataS137;
  _block_2070->$1 = 0;
  return _block_2070;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1043;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1043 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1043;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_2071;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS1024 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS1025;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1025
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
          if (_M0L6_2atmpS1024 <= _M0L6_2atmpS1025) {
            int32_t _M0L6_2atmpS1023 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_2071 = _M0L6_2atmpS1023 <= _M0L13allocate__lenS110;
          } else {
            _if__result_2071 = 0;
          }
        } else {
          _if__result_2071 = 0;
        }
      } else {
        _if__result_2071 = 0;
      }
    } else {
      _if__result_2071 = 0;
    }
  } else {
    _if__result_2071 = 0;
  }
  if (_if__result_2071) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS110, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS114, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS115;
    int32_t _M0L6_2atmpS1027;
    moonbit_string_t _M0L6_2atmpS1026;
    moonbit_string_t* _result_2072;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS115
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L13allocate__lenS110);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11src__offsetS112);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11dst__offsetS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L3lenS111);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1027 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L6_2atmpS1027);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1026
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS115);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS115);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2072
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1026);
    moonbit_decref_cycle_free(_M0L6_2atmpS1026);
    return _result_2072;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_2073;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS1029 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS1030;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1030
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
          if (_M0L6_2atmpS1029 <= _M0L6_2atmpS1030) {
            int32_t _M0L6_2atmpS1028 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_2073 = _M0L6_2atmpS1028 <= _M0L13allocate__lenS116;
          } else {
            _if__result_2073 = 0;
          }
        } else {
          _if__result_2073 = 0;
        }
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS116, 0, _M0L3srcS120, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS1032;
    moonbit_string_t _M0L6_2atmpS1031;
    struct _M0TUsiE** _result_2074;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS121
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L13allocate__lenS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11src__offsetS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11dst__offsetS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L3lenS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1032 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS1032);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1031
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2074
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1031);
    moonbit_decref_cycle_free(_M0L6_2atmpS1031);
    return _result_2074;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_2075;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS1034 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS1035;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1035
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
          if (_M0L6_2atmpS1034 <= _M0L6_2atmpS1035) {
            int32_t _M0L6_2atmpS1033 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_2075 = _M0L6_2atmpS1033 <= _M0L13allocate__lenS122;
          } else {
            _if__result_2075 = 0;
          }
        } else {
          _if__result_2075 = 0;
        }
      } else {
        _if__result_2075 = 0;
      }
    } else {
      _if__result_2075 = 0;
    }
  } else {
    _if__result_2075 = 0;
  }
  if (_if__result_2075) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS126, _M0L13allocate__lenS122, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1037;
    moonbit_string_t _M0L6_2atmpS1036;
    int32_t* _result_2076;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1037 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1037);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1036
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2076
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1036);
    moonbit_decref_cycle_free(_M0L6_2atmpS1036);
    return _result_2076;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_2077;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1039 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1040;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1040
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS132);
          if (_M0L6_2atmpS1039 <= _M0L6_2atmpS1040) {
            int32_t _M0L6_2atmpS1038 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_2077 = _M0L6_2atmpS1038 <= _M0L13allocate__lenS128;
          } else {
            _if__result_2077 = 0;
          }
        } else {
          _if__result_2077 = 0;
        }
      } else {
        _if__result_2077 = 0;
      }
    } else {
      _if__result_2077 = 0;
    }
  } else {
    _if__result_2077 = 0;
  }
  if (_if__result_2077) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS132, _M0L13allocate__lenS128, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1042;
    moonbit_string_t _M0L6_2atmpS1041;
    float* _result_2078;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS133
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L13allocate__lenS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11src__offsetS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11dst__offsetS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L3lenS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1042 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1042);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1041
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2078
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1041);
    moonbit_decref_cycle_free(_M0L6_2atmpS1041);
    return _result_2078;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  moonbit_string_t _M0L3objS106
) {
  struct _M0TPB6Logger _M0L6_2atmpS1021;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS107);
  _M0L6_2atmpS1021
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS107
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS106, _M0L6_2atmpS1021);
  if (_M0L6_2atmpS1021.$1) {
    moonbit_decref(_M0L6_2atmpS1021.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  int32_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS1022;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS1022
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS108, _M0L6_2atmpS1022);
  if (_M0L6_2atmpS1022.$1) {
    moonbit_decref(_M0L6_2atmpS1022.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS85,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L11src__offsetS86,
  int32_t _M0L11dst__offsetS84,
  int32_t _M0L9blit__lenS87
) {
  moonbit_string_t* _M0L3dstS82;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS82
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS83, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS82, _M0L11dst__offsetS84, _M0L3srcS85, _M0L11src__offsetS86, _M0L9blit__lenS87);
  moonbit_decref_cycle_free(_M0L3srcS85);
  return _M0L3dstS82;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS91,
  int32_t _M0L13allocate__lenS89,
  int32_t _M0L11src__offsetS92,
  int32_t _M0L11dst__offsetS90,
  int32_t _M0L9blit__lenS93
) {
  struct _M0TUsiE** _M0L3dstS88;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS88
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS89, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS88, _M0L11dst__offsetS90, _M0L3srcS91, _M0L11src__offsetS92, _M0L9blit__lenS93);
  moonbit_decref_cycle_free(_M0L3srcS91);
  return _M0L3dstS88;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS97,
  int32_t _M0L13allocate__lenS95,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS96,
  int32_t _M0L9blit__lenS99
) {
  int32_t* _M0L3dstS94;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS94
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS95);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS94, _M0L11dst__offsetS96, _M0L3srcS97, _M0L11src__offsetS98, _M0L9blit__lenS99);
  moonbit_decref_cycle_free(_M0L3srcS97);
  return _M0L3dstS94;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS103,
  int32_t _M0L13allocate__lenS101,
  int32_t _M0L11src__offsetS104,
  int32_t _M0L11dst__offsetS102,
  int32_t _M0L9blit__lenS105
) {
  float* _M0L3dstS100;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS100
  = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS101);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS100, _M0L11dst__offsetS102, _M0L3srcS103, _M0L11src__offsetS104, _M0L9blit__lenS105);
  moonbit_decref_cycle_free(_M0L3srcS103);
  return _M0L3dstS100;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS62,
  int32_t _M0L11dst__offsetS63,
  moonbit_string_t* _M0L3srcS64,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS67,
  int32_t _M0L11dst__offsetS68,
  struct _M0TUsiE** _M0L3srcS69,
  int32_t _M0L11src__offsetS70,
  int32_t _M0L3lenS71
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS69);
  moonbit_incref_cycle_free(_M0L3dstS67);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS67, _M0L11dst__offsetS68, _M0L3srcS69, _M0L11src__offsetS70, _M0L3lenS71);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS72,
  int32_t _M0L11dst__offsetS73,
  int32_t* _M0L3srcS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L3lenS76
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS74);
  moonbit_incref_cycle_free(_M0L3dstS72);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS72, _M0L11dst__offsetS73, _M0L3srcS74, _M0L11src__offsetS75, _M0L3lenS76, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS77,
  int32_t _M0L11dst__offsetS78,
  float* _M0L3srcS79,
  int32_t _M0L11src__offsetS80,
  int32_t _M0L3lenS81
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS79);
  moonbit_incref_cycle_free(_M0L3dstS77);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS77, _M0L11dst__offsetS78, _M0L3srcS79, _M0L11src__offsetS80, _M0L3lenS81, sizeof(float));
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
        int32_t _M0L6_2atmpS976 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS978 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS977;
        int32_t _M0L6_2atmpS979;
        if (
          _M0L6_2atmpS978 < 0
          || _M0L6_2atmpS978 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS977 = (int32_t)_M0L3srcS18[_M0L6_2atmpS978];
        if (
          _M0L6_2atmpS976 < 0
          || _M0L6_2atmpS976 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS976] = _M0L6_2atmpS977;
        _M0L6_2atmpS979 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS979;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS984 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS984;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS980 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS982 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS981;
        int32_t _M0L6_2atmpS983;
        if (
          _M0L6_2atmpS982 < 0
          || _M0L6_2atmpS982 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS981 = (int32_t)_M0L3srcS18[_M0L6_2atmpS982];
        if (
          _M0L6_2atmpS980 < 0
          || _M0L6_2atmpS980 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS980] = _M0L6_2atmpS981;
        _M0L6_2atmpS983 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS983;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS26,
  int32_t _M0L11dst__offsetS28,
  moonbit_string_t* _M0L3srcS27,
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
        int32_t _M0L6_2atmpS985 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS987 = _M0L11src__offsetS29 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS986;
        moonbit_string_t _M0L6_2aoldS1937;
        int32_t _M0L6_2atmpS988;
        if (
          _M0L6_2atmpS987 < 0
          || _M0L6_2atmpS987 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS986 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS987];
        if (
          _M0L6_2atmpS985 < 0
          || _M0L6_2atmpS985 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1937 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS985];
        moonbit_incref_cycle_free(_M0L6_2atmpS986);
        moonbit_decref_cycle_free(_M0L6_2aoldS1937);
        _M0L3dstS26[_M0L6_2atmpS985] = _M0L6_2atmpS986;
        _M0L6_2atmpS988 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS988;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS993 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS993;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS989 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS991 = _M0L11src__offsetS29 + _M0L1iS33;
        moonbit_string_t _M0L6_2atmpS990;
        moonbit_string_t _M0L6_2aoldS1938;
        int32_t _M0L6_2atmpS992;
        if (
          _M0L6_2atmpS991 < 0
          || _M0L6_2atmpS991 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS990 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS991];
        if (
          _M0L6_2atmpS989 < 0
          || _M0L6_2atmpS989 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1938 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS989];
        moonbit_incref_cycle_free(_M0L6_2atmpS990);
        moonbit_decref_cycle_free(_M0L6_2aoldS1938);
        _M0L3dstS26[_M0L6_2atmpS989] = _M0L6_2atmpS990;
        _M0L6_2atmpS992 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS992;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS35,
  int32_t _M0L11dst__offsetS37,
  struct _M0TUsiE** _M0L3srcS36,
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
        int32_t _M0L6_2atmpS994 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS996 = _M0L11src__offsetS38 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS995;
        struct _M0TUsiE* _M0L6_2aoldS1939;
        int32_t _M0L6_2atmpS997;
        if (
          _M0L6_2atmpS996 < 0
          || _M0L6_2atmpS996 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS995 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS996];
        if (
          _M0L6_2atmpS994 < 0
          || _M0L6_2atmpS994 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1939 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS994];
        if (_M0L6_2atmpS995) {
          moonbit_incref_cycle_free(_M0L6_2atmpS995);
        }
        if (_M0L6_2aoldS1939) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1939);
        }
        _M0L3dstS35[_M0L6_2atmpS994] = _M0L6_2atmpS995;
        _M0L6_2atmpS997 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS997;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1002 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS1002;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS998 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS1000 = _M0L11src__offsetS38 + _M0L1iS42;
        struct _M0TUsiE* _M0L6_2atmpS999;
        struct _M0TUsiE* _M0L6_2aoldS1940;
        int32_t _M0L6_2atmpS1001;
        if (
          _M0L6_2atmpS1000 < 0
          || _M0L6_2atmpS1000 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS999 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS1000];
        if (
          _M0L6_2atmpS998 < 0
          || _M0L6_2atmpS998 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1940 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS998];
        if (_M0L6_2atmpS999) {
          moonbit_incref_cycle_free(_M0L6_2atmpS999);
        }
        if (_M0L6_2aoldS1940) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1940);
        }
        _M0L3dstS35[_M0L6_2atmpS998] = _M0L6_2atmpS999;
        _M0L6_2atmpS1001 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS1001;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  int32_t* _M0L3srcS45,
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
        int32_t _M0L6_2atmpS1003 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS1005 = _M0L11src__offsetS47 + _M0L1iS48;
        int32_t _M0L6_2atmpS1004;
        int32_t _M0L6_2atmpS1006;
        if (
          _M0L6_2atmpS1005 < 0
          || _M0L6_2atmpS1005 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1004 = (int32_t)_M0L3srcS45[_M0L6_2atmpS1005];
        if (
          _M0L6_2atmpS1003 < 0
          || _M0L6_2atmpS1003 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1003] = _M0L6_2atmpS1004;
        _M0L6_2atmpS1006 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS1006;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1011 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS1011;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS1007 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS1009 = _M0L11src__offsetS47 + _M0L1iS51;
        int32_t _M0L6_2atmpS1008;
        int32_t _M0L6_2atmpS1010;
        if (
          _M0L6_2atmpS1009 < 0
          || _M0L6_2atmpS1009 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1008 = (int32_t)_M0L3srcS45[_M0L6_2atmpS1009];
        if (
          _M0L6_2atmpS1007 < 0
          || _M0L6_2atmpS1007 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1007] = _M0L6_2atmpS1008;
        _M0L6_2atmpS1010 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS1010;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS53,
  int32_t _M0L11dst__offsetS55,
  float* _M0L3srcS54,
  int32_t _M0L11src__offsetS56,
  int32_t _M0L3lenS58
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS53 == _M0L3srcS54 && _M0L11dst__offsetS55 < _M0L11src__offsetS56
  ) {
    int32_t _M0L1iS57 = 0;
    while (1) {
      if (_M0L1iS57 < _M0L3lenS58) {
        int32_t _M0L6_2atmpS1012 = _M0L11dst__offsetS55 + _M0L1iS57;
        int32_t _M0L6_2atmpS1014 = _M0L11src__offsetS56 + _M0L1iS57;
        float _M0L6_2atmpS1013;
        int32_t _M0L6_2atmpS1015;
        if (
          _M0L6_2atmpS1014 < 0
          || _M0L6_2atmpS1014 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1013 = (float)_M0L3srcS54[_M0L6_2atmpS1014];
        if (
          _M0L6_2atmpS1012 < 0
          || _M0L6_2atmpS1012 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1012] = _M0L6_2atmpS1013;
        _M0L6_2atmpS1015 = _M0L1iS57 + 1;
        _M0L1iS57 = _M0L6_2atmpS1015;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1020 = _M0L3lenS58 - 1;
    int32_t _M0L1iS60 = _M0L6_2atmpS1020;
    while (1) {
      if (_M0L1iS60 >= 0) {
        int32_t _M0L6_2atmpS1016 = _M0L11dst__offsetS55 + _M0L1iS60;
        int32_t _M0L6_2atmpS1018 = _M0L11src__offsetS56 + _M0L1iS60;
        float _M0L6_2atmpS1017;
        int32_t _M0L6_2atmpS1019;
        if (
          _M0L6_2atmpS1018 < 0
          || _M0L6_2atmpS1018 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1017 = (float)_M0L3srcS54[_M0L6_2atmpS1018];
        if (
          _M0L6_2atmpS1016 < 0
          || _M0L6_2atmpS1016 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1016] = _M0L6_2atmpS1017;
        _M0L6_2atmpS1019 = _M0L1iS60 - 1;
        _M0L1iS60 = _M0L6_2atmpS1019;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS13) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS14
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS14);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_27.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_28.data);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS5
) {
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS945) {
  switch (Moonbit_object_tag(_M0L4_2aeS945)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_29.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS945);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_31.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS971,
  struct _M0TPB4Show _M0L8_2aparamS970
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS969 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS971;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS969, _M0L8_2aparamS970);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS968,
  struct _M0TPB4Show _M0L8_2aparamS967
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS966 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS968;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS966, _M0L8_2aparamS967);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS965,
  int32_t _M0L8_2aparamS964
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS963 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS965;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS963, _M0L8_2aparamS964);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS962,
  struct _M0TPC16string10StringView _M0L8_2aparamS961
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS960 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS962;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS960, _M0L8_2aparamS961);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS959,
  moonbit_string_t _M0L8_2aparamS956,
  int32_t _M0L8_2aparamS957,
  int32_t _M0L8_2aparamS958
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS955 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS959;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS955, _M0L8_2aparamS956, _M0L8_2aparamS957, _M0L8_2aparamS958);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS954,
  moonbit_string_t _M0L8_2aparamS953
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS952 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS954;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS952, _M0L8_2aparamS953);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2089 = 9218868437227405311ll;
  int64_t _tmp_2090;
  int64_t _tmp_2091;
  int64_t _tmp_2092;
  int64_t _tmp_2093;
  _M0FPB18double__max__value = *(double*)&_tmp_2089;
  _tmp_2090 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2090;
  _tmp_2091 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2091;
  _tmp_2092 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2092;
  _tmp_2093 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2093;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS975;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS938;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS939;
  int32_t _M0L7_2abindS940;
  struct _M0TUsiE** _M0L7_2abindS941;
  int32_t _M0L6_2acntS1945;
  int32_t _M0L2__S942;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS975
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS938
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 98, 0);
  _M0L12async__testsS938->$0 = _M0L6_2atmpS975;
  _M0L12async__testsS938->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS939
  = _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS940 = _M0L7_2abindS939->$1;
  _M0L7_2abindS941 = _M0L7_2abindS939->$0;
  _M0L6_2acntS1945
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS939));
  if (_M0L6_2acntS1945 > 1) {
    int32_t _M0L11_2anew__cntS1946 = _M0L6_2acntS1945 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS939), _M0L11_2anew__cntS1946);
    moonbit_incref_cycle_free(_M0L7_2abindS941);
  } else if (_M0L6_2acntS1945 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS939);
  }
  _M0L2__S942 = 0;
  while (1) {
    if (_M0L2__S942 < _M0L7_2abindS940) {
      struct _M0TUsiE* _M0L3argS943 =
        (struct _M0TUsiE*)_M0L7_2abindS941[_M0L2__S942];
      moonbit_string_t _M0L6_2atmpS972 = _M0L3argS943->$0;
      int32_t _M0L6_2atmpS973 = _M0L3argS943->$1;
      int32_t _M0L6_2atmpS974;
      moonbit_incref_cycle_free(_M0L6_2atmpS972);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples23tsodyks__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS938, _M0L6_2atmpS972, _M0L6_2atmpS973);
      moonbit_decref_cycle_free(_M0L6_2atmpS972);
      _M0L6_2atmpS974 = _M0L2__S942 + 1;
      _M0L2__S942 = _M0L6_2atmpS974;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS941);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tsodyks\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples23tsodyks__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23tsodyks__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS938);
  moonbit_decref_cycle_free(_M0L12async__testsS938);
  moonbit_flush_cycles();
  return 0;
}