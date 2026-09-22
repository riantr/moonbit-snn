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
struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt11WCParameter;

struct _M0TP26RiantR8snn__mbt11RateSynapse;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TUmmmmE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TUddE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TWRPC15error5ErrorEu;

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

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

struct _M0TP26RiantR8snn__mbt11WCParameter {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt11RateSynapse {
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* $0;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* $1;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $2;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
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

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt11WilsonCowan {
  struct _M0TP26RiantR8snn__mbt11WCParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TUmmmmE {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1114(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1107(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1102(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1079(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1072(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt22forward__rate__synapse(
  struct _M0TP26RiantR8snn__mbt11RateSynapse*
);

struct _M0TP26RiantR8snn__mbt11RateSynapse* _M0MP26RiantR8snn__mbt11RateSynapse3new(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0MP26RiantR8snn__mbt11WilsonCowan3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11WCParameter* _M0MP26RiantR8snn__mbt11WCParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  float
);

#define _M0FP26RiantR8snn__mbt5tanhf tanhf

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR13forward__rate(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  struct _M0TPB5ArrayGfE*,
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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

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

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
);

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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float*,
  int32_t,
  float*,
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

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

double sin(double);

float tanhf(float);

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
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 119, 105, 108, 115, 111, 110, 95, 
    99, 111, 119, 97, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 
    116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 
    0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 119, 105, 108, 115, 111, 110, 95, 
    99, 111, 119, 97, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 
    116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1114$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1114
  };

uint32_t const moonbit_layout_table_data[62] =
  {
    sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102)
    / 4, 1,
    offsetof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102, $1)
    / 4
    * 2,
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11RateSynapse) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11WilsonCowan) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $5) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2226
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1135,
  moonbit_string_t _M0L8filenameS1104,
  int32_t _M0L5indexS1106
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102* _closure_2254;
  struct _M0TWEu* _M0L13handle__startS1102;
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107* _closure_2255;
  struct _M0TWssbEu* _M0L14handle__resultS1107;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1114;
  void* _M0L11_2atry__errS1129;
  struct moonbit_result_0 _tmp_2257;
  int32_t _handle__error__result_2258;
  int32_t _M0L6_2atmpS2214;
  void* _M0L3errS1130;
  moonbit_string_t _M0L4nameS1132;
  struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1133;
  moonbit_string_t _M0L7_2anameS1134;
  int32_t _M0L6_2acntS2248;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1104);
  _closure_2254
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102*)moonbit_malloc(sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102));
  Moonbit_object_header(_closure_2254)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2254->code
  = &_M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1102;
  _closure_2254->$0 = _M0L5indexS1106;
  _closure_2254->$1 = _M0L8filenameS1104;
  _M0L13handle__startS1102 = (struct _M0TWEu*)_closure_2254;
  moonbit_incref_cycle_free(_M0L8filenameS1104);
  _closure_2255
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107));
  Moonbit_object_header(_closure_2255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2255->code
  = &_M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1107;
  _closure_2255->$0 = _M0L5indexS1106;
  _closure_2255->$1 = _M0L8filenameS1104;
  _M0L14handle__resultS1107 = (struct _M0TWssbEu*)_closure_2255;
  _M0L17error__to__stringS1114
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1114$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2257
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1135, _M0L8filenameS1104, _M0L5indexS1106, _M0L13handle__startS1102, _M0L14handle__resultS1107, _M0L17error__to__stringS1114);
  if (_tmp_2257.tag) {
    int32_t const _M0L5_2aokS2223 = _tmp_2257.data.ok;
    _handle__error__result_2258 = _M0L5_2aokS2223;
  } else {
    void* const _M0L6_2aerrS2224 = _tmp_2257.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
    moonbit_decref_cycle_free(_M0L13handle__startS1102);
    _M0L11_2atry__errS1129 = _M0L6_2aerrS2224;
    goto join_1128;
  }
  if (_handle__error__result_2258) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
    moonbit_decref_cycle_free(_M0L13handle__startS1102);
    _M0L6_2atmpS2214 = 1;
  } else {
    struct moonbit_result_0 _tmp_2259;
    int32_t _handle__error__result_2260;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2259
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1135, _M0L8filenameS1104, _M0L5indexS1106, _M0L13handle__startS1102, _M0L14handle__resultS1107, _M0L17error__to__stringS1114);
    if (_tmp_2259.tag) {
      int32_t const _M0L5_2aokS2221 = _tmp_2259.data.ok;
      _handle__error__result_2260 = _M0L5_2aokS2221;
    } else {
      void* const _M0L6_2aerrS2222 = _tmp_2259.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
      moonbit_decref_cycle_free(_M0L13handle__startS1102);
      _M0L11_2atry__errS1129 = _M0L6_2aerrS2222;
      goto join_1128;
    }
    if (_handle__error__result_2260) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
      moonbit_decref_cycle_free(_M0L13handle__startS1102);
      _M0L6_2atmpS2214 = 1;
    } else {
      struct moonbit_result_0 _tmp_2261;
      int32_t _handle__error__result_2262;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2261
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1135, _M0L8filenameS1104, _M0L5indexS1106, _M0L13handle__startS1102, _M0L14handle__resultS1107, _M0L17error__to__stringS1114);
      if (_tmp_2261.tag) {
        int32_t const _M0L5_2aokS2219 = _tmp_2261.data.ok;
        _handle__error__result_2262 = _M0L5_2aokS2219;
      } else {
        void* const _M0L6_2aerrS2220 = _tmp_2261.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
        moonbit_decref_cycle_free(_M0L13handle__startS1102);
        _M0L11_2atry__errS1129 = _M0L6_2aerrS2220;
        goto join_1128;
      }
      if (_handle__error__result_2262) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
        moonbit_decref_cycle_free(_M0L13handle__startS1102);
        _M0L6_2atmpS2214 = 1;
      } else {
        struct moonbit_result_0 _tmp_2263;
        int32_t _handle__error__result_2264;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2263
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1135, _M0L8filenameS1104, _M0L5indexS1106, _M0L13handle__startS1102, _M0L14handle__resultS1107, _M0L17error__to__stringS1114);
        if (_tmp_2263.tag) {
          int32_t const _M0L5_2aokS2217 = _tmp_2263.data.ok;
          _handle__error__result_2264 = _M0L5_2aokS2217;
        } else {
          void* const _M0L6_2aerrS2218 = _tmp_2263.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
          moonbit_decref_cycle_free(_M0L13handle__startS1102);
          _M0L11_2atry__errS1129 = _M0L6_2aerrS2218;
          goto join_1128;
        }
        if (_handle__error__result_2264) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
          moonbit_decref_cycle_free(_M0L13handle__startS1102);
          _M0L6_2atmpS2214 = 1;
        } else {
          struct moonbit_result_0 _tmp_2265;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2265
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1135, _M0L8filenameS1104, _M0L5indexS1106, _M0L13handle__startS1102, _M0L14handle__resultS1107, _M0L17error__to__stringS1114);
          moonbit_decref_cycle_free(_M0L13handle__startS1102);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1114);
          if (_tmp_2265.tag) {
            int32_t const _M0L5_2aokS2215 = _tmp_2265.data.ok;
            _M0L6_2atmpS2214 = _M0L5_2aokS2215;
          } else {
            void* const _M0L6_2aerrS2216 = _tmp_2265.data.err;
            _M0L11_2atry__errS1129 = _M0L6_2aerrS2216;
            goto join_1128;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2214) {
    void* _M0L132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2225 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2225)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2225)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1129
    = _M0L132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2225;
    goto join_1128;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1107);
  }
  goto joinlet_2256;
  join_1128:;
  _M0L3errS1130 = _M0L11_2atry__errS1129;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1133
  = (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1130;
  _M0L7_2anameS1134 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1133->$0;
  _M0L6_2acntS2248
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1133));
  if (_M0L6_2acntS2248 > 1) {
    int32_t _M0L11_2anew__cntS2249 = _M0L6_2acntS2248 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1133), _M0L11_2anew__cntS2249);
    moonbit_incref_cycle_free(_M0L7_2anameS1134);
  } else if (_M0L6_2acntS2248 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1133);
  }
  _M0L4nameS1132 = _M0L7_2anameS1134;
  goto join_1131;
  goto joinlet_2266;
  join_1131:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1107(_M0L14handle__resultS1107, _M0L4nameS1132, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1107);
  moonbit_decref_cycle_free(_M0L4nameS1132);
  joinlet_2266:;
  joinlet_2256:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1114(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2213,
  void* _M0L3errS1115
) {
  void* _M0L1eS1117;
  moonbit_string_t _M0L1eS1119;
  moonbit_string_t _result_2269;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1115)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1120 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1115;
      moonbit_string_t _M0L4_2aeS1121 = _M0L10_2aFailureS1120->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1121);
      _M0L1eS1119 = _M0L4_2aeS1121;
      goto join_1118;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1122 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1115;
      moonbit_string_t _M0L4_2aeS1123 = _M0L15_2aInspectErrorS1122->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1123);
      _M0L1eS1119 = _M0L4_2aeS1123;
      goto join_1118;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1124 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1115;
      moonbit_string_t _M0L4_2aeS1125 = _M0L16_2aSnapshotErrorS1124->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1125);
      _M0L1eS1119 = _M0L4_2aeS1125;
      goto join_1118;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1126 =
        (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1115;
      moonbit_string_t _M0L4_2aeS1127 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1126->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1127);
      _M0L1eS1119 = _M0L4_2aeS1127;
      goto join_1118;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1115);
      _M0L1eS1117 = _M0L3errS1115;
      goto join_1116;
      break;
    }
  }
  join_1118:;
  return _M0L1eS1119;
  join_1116:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2269 = _M0FP15Error10to__string(_M0L1eS1117);
  moonbit_decref_cycle_free(_M0L1eS1117);
  return _result_2269;
}

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1107(
  struct _M0TWssbEu* _M0L6_2aenvS2210,
  moonbit_string_t _M0L10__testnameS1108,
  moonbit_string_t _M0L7messageS1109,
  int32_t _M0L7skippedS1110
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107* _M0L14_2acasted__envS2211;
  moonbit_string_t _M0L8filenameS1104;
  int32_t _M0L5indexS1106;
  moonbit_string_t _M0L10file__nameS1111;
  moonbit_string_t _M0L7messageS1112;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1113;
  moonbit_string_t _M0L6_2atmpS2212;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2211
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1107*)_M0L6_2aenvS2210;
  _M0L8filenameS1104 = _M0L14_2acasted__envS2211->$1;
  _M0L5indexS1106 = _M0L14_2acasted__envS2211->$0;
  if (!_M0L7skippedS1110 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1111
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1104, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1112
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1109, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1113
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1113, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1113, _M0L10file__nameS1111);
  moonbit_decref_cycle_free(_M0L10file__nameS1111);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1113, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1113, _M0L5indexS1106);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1113, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1113, _M0L7messageS1112);
  moonbit_decref_cycle_free(_M0L7messageS1112);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1113, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2212
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1113);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1113);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2212);
  moonbit_decref_cycle_free(_M0L6_2atmpS2212);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1102(
  struct _M0TWEu* _M0L6_2aenvS2207
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102* _M0L14_2acasted__envS2208;
  moonbit_string_t _M0L8filenameS1104;
  int32_t _M0L5indexS1106;
  moonbit_string_t _M0L10file__nameS1103;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1105;
  moonbit_string_t _M0L6_2atmpS2209;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2208
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fwilson__cowan__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1102*)_M0L6_2aenvS2207;
  _M0L8filenameS1104 = _M0L14_2acasted__envS2208->$1;
  _M0L5indexS1106 = _M0L14_2acasted__envS2208->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1103
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1104, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1105
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1105, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1105, _M0L10file__nameS1103);
  moonbit_decref_cycle_free(_M0L10file__nameS1103);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1105, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1105, _M0L5indexS1106);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1105, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2209
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1105);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1105);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2209);
  moonbit_decref_cycle_free(_M0L6_2atmpS2209);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1072;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1079;
  struct _M0TUsiE** _M0L6_2atmpS2206;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1086;
  moonbit_string_t* _M0L9cli__argsS1087;
  moonbit_string_t _M0L6_2atmpS2205;
  moonbit_string_t _M0L6_2atmpS2204;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1088;
  int32_t _M0L7_2abindS1089;
  moonbit_string_t* _M0L7_2abindS1090;
  int32_t _M0L6_2acntS2250;
  int32_t _M0L2__S1091;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1072 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1079 = 0;
  _M0L6_2atmpS2206 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1086
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1086->$0 = _M0L6_2atmpS2206;
  _M0L16file__and__indexS1086->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1087
  = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1087)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2205 = (moonbit_string_t)_M0L9cli__argsS1087[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2205);
  moonbit_decref_cycle_free(_M0L9cli__argsS1087);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2204
  = _M0MP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2205);
  moonbit_decref_cycle_free(_M0L6_2atmpS2205);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1088
  = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1079(_M0L51moonbit__test__driver__internal__split__mbt__stringS1079, _M0L6_2atmpS2204, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2204);
  _M0L7_2abindS1089 = _M0L10test__argsS1088->$1;
  _M0L7_2abindS1090 = _M0L10test__argsS1088->$0;
  _M0L6_2acntS2250
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1088));
  if (_M0L6_2acntS2250 > 1) {
    int32_t _M0L11_2anew__cntS2251 = _M0L6_2acntS2250 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1088), _M0L11_2anew__cntS2251);
    moonbit_incref_cycle_free(_M0L7_2abindS1090);
  } else if (_M0L6_2acntS2250 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1088);
  }
  _M0L2__S1091 = 0;
  while (1) {
    if (_M0L2__S1091 < _M0L7_2abindS1089) {
      moonbit_string_t _M0L3argS1092 =
        (moonbit_string_t)_M0L7_2abindS1090[_M0L2__S1091];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1093;
      moonbit_string_t _M0L4fileS1094;
      moonbit_string_t _M0L5rangeS1095;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1096;
      moonbit_string_t _M0L6_2atmpS2202;
      int32_t _M0L5startS1097;
      moonbit_string_t _M0L6_2atmpS2201;
      int32_t _M0L3endS1098;
      int32_t _M0L1iS1099;
      int32_t _M0L6_2atmpS2203;
      moonbit_incref_cycle_free(_M0L3argS1092);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1093
      = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1079(_M0L51moonbit__test__driver__internal__split__mbt__stringS1079, _M0L3argS1092, 58);
      moonbit_decref_cycle_free(_M0L3argS1092);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1094
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1093, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1095
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1093, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1093);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1096
      = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1079(_M0L51moonbit__test__driver__internal__split__mbt__stringS1079, _M0L5rangeS1095, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1095);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2202
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1096, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1097
      = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1072(_M0L45moonbit__test__driver__internal__parse__int__S1072, _M0L6_2atmpS2202);
      moonbit_decref_cycle_free(_M0L6_2atmpS2202);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2201
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1096, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1096);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1098
      = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1072(_M0L45moonbit__test__driver__internal__parse__int__S1072, _M0L6_2atmpS2201);
      moonbit_decref_cycle_free(_M0L6_2atmpS2201);
      _M0L1iS1099 = _M0L5startS1097;
      while (1) {
        if (_M0L1iS1099 < _M0L3endS1098) {
          struct _M0TUsiE* _M0L8_2atupleS2199;
          int32_t _M0L6_2atmpS2200;
          moonbit_incref_cycle_free(_M0L4fileS1094);
          _M0L8_2atupleS2199
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2199)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2199->$0 = _M0L4fileS1094;
          _M0L8_2atupleS2199->$1 = _M0L1iS1099;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1086, _M0L8_2atupleS2199);
          _M0L6_2atmpS2200 = _M0L1iS1099 + 1;
          _M0L1iS1099 = _M0L6_2atmpS2200;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1094);
        }
        break;
      }
      _M0L6_2atmpS2203 = _M0L2__S1091 + 1;
      _M0L2__S1091 = _M0L6_2atmpS2203;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1090);
    }
    break;
  }
  return _M0L16file__and__indexS1086;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1079(
  int32_t _M0L6_2aenvS2180,
  moonbit_string_t _M0L1sS1080,
  int32_t _M0L3sepS1081
) {
  moonbit_string_t* _M0L6_2atmpS2198;
  struct _M0TPB5ArrayGsE* _M0L3resS1082;
  struct _M0TPB8MutLocalGiE* _M0L1iS1083;
  struct _M0TPB8MutLocalGiE* _M0L5startS1084;
  int32_t _M0L3valS2193;
  int32_t _M0L6_2atmpS2194;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2198 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1082
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1082)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1082->$0 = _M0L6_2atmpS2198;
  _M0L3resS1082->$1 = 0;
  _M0L1iS1083
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1083)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1083->$0 = 0;
  _M0L5startS1084
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1084)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1084->$0 = 0;
  while (1) {
    int32_t _M0L3valS2181 = _M0L1iS1083->$0;
    int32_t _M0L6_2atmpS2182 = Moonbit_array_length(_M0L1sS1080);
    if (_M0L3valS2181 < _M0L6_2atmpS2182) {
      int32_t _M0L3valS2185 = _M0L1iS1083->$0;
      int32_t _M0L6_2atmpS2184;
      int32_t _M0L6_2atmpS2183;
      int32_t _M0L3valS2192;
      int32_t _M0L6_2atmpS2191;
      if (
        _M0L3valS2185 < 0
        || _M0L3valS2185 >= Moonbit_array_length(_M0L1sS1080)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2184 = _M0L1sS1080[_M0L3valS2185];
      _M0L6_2atmpS2183 = _M0L6_2atmpS2184;
      if (_M0L6_2atmpS2183 == _M0L3sepS1081) {
        int32_t _M0L3valS2187 = _M0L5startS1084->$0;
        int32_t _M0L3valS2188 = _M0L1iS1083->$0;
        moonbit_string_t _M0L6_2atmpS2186;
        int32_t _M0L3valS2190;
        int32_t _M0L6_2atmpS2189;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2186
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1080, _M0L3valS2187, _M0L3valS2188);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1082, _M0L6_2atmpS2186);
        _M0L3valS2190 = _M0L1iS1083->$0;
        _M0L6_2atmpS2189 = _M0L3valS2190 + 1;
        _M0L5startS1084->$0 = _M0L6_2atmpS2189;
      }
      _M0L3valS2192 = _M0L1iS1083->$0;
      _M0L6_2atmpS2191 = _M0L3valS2192 + 1;
      _M0L1iS1083->$0 = _M0L6_2atmpS2191;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1083);
    }
    break;
  }
  _M0L3valS2193 = _M0L5startS1084->$0;
  _M0L6_2atmpS2194 = Moonbit_array_length(_M0L1sS1080);
  if (_M0L3valS2193 < _M0L6_2atmpS2194) {
    int32_t _M0L3valS2196 = _M0L5startS1084->$0;
    int32_t _M0L6_2atmpS2197;
    moonbit_string_t _M0L6_2atmpS2195;
    moonbit_decref_cycle_free(_M0L5startS1084);
    _M0L6_2atmpS2197 = Moonbit_array_length(_M0L1sS1080);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2195
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1080, _M0L3valS2196, _M0L6_2atmpS2197);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1082, _M0L6_2atmpS2195);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1084);
  }
  return _M0L3resS1082;
}

int32_t _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1072(
  int32_t _M0L6_2aenvS2173,
  moonbit_string_t _M0L1sS1073
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1074;
  int32_t _M0L3lenS1075;
  int32_t _M0L7_2abindS1076;
  int32_t _M0L1iS1077;
  int32_t _result_2274;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1074
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1074)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1074->$0 = 0;
  _M0L3lenS1075 = Moonbit_array_length(_M0L1sS1073);
  _M0L7_2abindS1076 = 0;
  _M0L1iS1077 = _M0L7_2abindS1076;
  while (1) {
    if (_M0L1iS1077 < _M0L3lenS1075) {
      int32_t _M0L3valS2178 = _M0L3resS1074->$0;
      int32_t _M0L6_2atmpS2175 = _M0L3valS2178 * 10;
      int32_t _M0L6_2atmpS2177;
      int32_t _M0L6_2atmpS2176;
      int32_t _M0L6_2atmpS2174;
      int32_t _M0L6_2atmpS2179;
      if (
        _M0L1iS1077 < 0 || _M0L1iS1077 >= Moonbit_array_length(_M0L1sS1073)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2177 = _M0L1sS1073[_M0L1iS1077];
      _M0L6_2atmpS2176 = _M0L6_2atmpS2177 - 48;
      _M0L6_2atmpS2174 = _M0L6_2atmpS2175 + _M0L6_2atmpS2176;
      _M0L3resS1074->$0 = _M0L6_2atmpS2174;
      _M0L6_2atmpS2179 = _M0L1iS1077 + 1;
      _M0L1iS1077 = _M0L6_2atmpS2179;
      continue;
    }
    break;
  }
  _result_2274 = _M0L3resS1074->$0;
  moonbit_decref_cycle_free(_M0L3resS1074);
  return _result_2274;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1071
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1071);
  return _M0L4selfS1071;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1041,
  moonbit_string_t _M0L12_2adiscard__S1042,
  int32_t _M0L12_2adiscard__S1043,
  struct _M0TWEu* _M0L12_2adiscard__S1044,
  struct _M0TWssbEu* _M0L12_2adiscard__S1045,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1046
) {
  struct moonbit_result_0 _result_2275;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2275.tag = 1;
  _result_2275.data.ok = 0;
  return _result_2275;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1047,
  moonbit_string_t _M0L12_2adiscard__S1048,
  int32_t _M0L12_2adiscard__S1049,
  struct _M0TWEu* _M0L12_2adiscard__S1050,
  struct _M0TWssbEu* _M0L12_2adiscard__S1051,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1052
) {
  struct moonbit_result_0 _result_2276;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2276.tag = 1;
  _result_2276.data.ok = 0;
  return _result_2276;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1053,
  moonbit_string_t _M0L12_2adiscard__S1054,
  int32_t _M0L12_2adiscard__S1055,
  struct _M0TWEu* _M0L12_2adiscard__S1056,
  struct _M0TWssbEu* _M0L12_2adiscard__S1057,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1058
) {
  struct moonbit_result_0 _result_2277;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2277.tag = 1;
  _result_2277.data.ok = 0;
  return _result_2277;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1059,
  moonbit_string_t _M0L12_2adiscard__S1060,
  int32_t _M0L12_2adiscard__S1061,
  struct _M0TWEu* _M0L12_2adiscard__S1062,
  struct _M0TWssbEu* _M0L12_2adiscard__S1063,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1064
) {
  struct moonbit_result_0 _result_2278;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2278.tag = 1;
  _result_2278.data.ok = 0;
  return _result_2278;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1065,
  moonbit_string_t _M0L12_2adiscard__S1066,
  int32_t _M0L12_2adiscard__S1067,
  struct _M0TWEu* _M0L12_2adiscard__S1068,
  struct _M0TWssbEu* _M0L12_2adiscard__S1069,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1070
) {
  struct moonbit_result_0 _result_2279;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _result_2279.tag = 1;
  _result_2279.data.ok = 0;
  return _result_2279;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1040
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22forward__rate__synapse(
  struct _M0TP26RiantR8snn__mbt11RateSynapse* _M0L1cS999
) {
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2168;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L3preS2172;
  struct _M0TPB5ArrayGfE* _M0L1rS2169;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4postS2171;
  struct _M0TPB5ArrayGfE* _M0L1gS2170;
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L6matrixS2168 = _M0L1cS999->$2;
  _M0L3preS2172 = _M0L1cS999->$0;
  _M0L1rS2169 = _M0L3preS2172->$3;
  _M0L4postS2171 = _M0L1cS999->$1;
  _M0L1gS2170 = _M0L4postS2171->$4;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR13forward__rate(_M0L6matrixS2168, _M0L1rS2169, _M0L1gS2170);
  return 0;
}

struct _M0TP26RiantR8snn__mbt11RateSynapse* _M0MP26RiantR8snn__mbt11RateSynapse3new(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L3preS989,
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4postS997,
  float _M0L2muS993,
  float _M0L5sigmaS995,
  float _M0L1pS991,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS998
) {
  int32_t _M0L1nS2167;
  float _M0L6n__preS988;
  float _M0L6_2atmpS2166;
  float _M0L5denomS990;
  float _M0L11weight__stdS992;
  float _M0L13weight__sigmaS994;
  int32_t _M0L1nS2164;
  int32_t _M0L1nS2165;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS996;
  struct _M0TP26RiantR8snn__mbt11RateSynapse* _block_2280;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L1nS2167 = _M0L3preS989->$1;
  _M0L6n__preS988 = (float)_M0L1nS2167;
  _M0L6_2atmpS2166 = _M0L6n__preS988 * _M0L1pS991;
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L5denomS990 = sqrtf(_M0L6_2atmpS2166);
  if (_M0L5denomS990 > 0x0p+0f) {
    _M0L11weight__stdS992 = _M0L2muS993 / _M0L5denomS990;
  } else {
    _M0L11weight__stdS992 = _M0L2muS993;
  }
  if (_M0L5denomS990 > 0x0p+0f) {
    _M0L13weight__sigmaS994 = _M0L5sigmaS995 / _M0L5denomS990;
  } else {
    _M0L13weight__sigmaS994 = _M0L5sigmaS995;
  }
  _M0L1nS2164 = _M0L3preS989->$1;
  _M0L1nS2165 = _M0L4postS997->$1;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L6matrixS996
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2164, _M0L1nS2165, 0x0p+0f, _M0L11weight__stdS992, _M0L1pS991, _M0L3rngS998);
  moonbit_incref_cycle_free(_M0L3preS989);
  moonbit_incref_cycle_free(_M0L4postS997);
  _block_2280
  = (struct _M0TP26RiantR8snn__mbt11RateSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11RateSynapse));
  Moonbit_object_header(_block_2280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2280->$0 = _M0L3preS989;
  _block_2280->$1 = _M0L4postS997;
  _block_2280->$2 = _M0L6matrixS996;
  return _block_2280;
}

struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0MP26RiantR8snn__mbt11WilsonCowan3new(
  int32_t _M0L1nS976,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS983
) {
  struct _M0TPB5ArrayGfE* _M0L1xS975;
  struct _M0TPB5ArrayGfE* _M0L1rS977;
  int32_t _M0L7_2abindS978;
  int32_t _M0L1kS979;
  struct _M0TPB5ArrayGfE* _M0L1gS986;
  struct _M0TPB5ArrayGfE* _M0L1iS987;
  struct _M0TP26RiantR8snn__mbt11WCParameter* _M0L6_2atmpS2163;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _block_2283;
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1xS975 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1rS977 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  _M0L7_2abindS978 = 0;
  _M0L1kS979 = _M0L7_2abindS978;
  while (1) {
    if (_M0L1kS979 < _M0L1nS976) {
      double _M0L2z1S981;
      struct _M0TUddE* _M0L7_2abindS982;
      double _M0L5_2az1S984;
      float _M0L6_2atmpS2159;
      float _M0L6_2atmpS2158;
      float _M0L6_2atmpS2161;
      float _M0L6_2atmpS2160;
      int32_t _M0L6_2atmpS2162;
      #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L7_2abindS982 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS983);
      _M0L5_2az1S984 = _M0L7_2abindS982->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS982);
      _M0L2z1S981 = _M0L5_2az1S984;
      goto join_980;
      goto joinlet_2282;
      join_980:;
      _M0L6_2atmpS2159 = (float)_M0L2z1S981;
      _M0L6_2atmpS2158 = 0x1p-1f * _M0L6_2atmpS2159;
      #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS975, _M0L1kS979, _M0L6_2atmpS2158);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2161 = _M0MPC15array5Array2atGfE(_M0L1xS975, _M0L1kS979);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2160 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS2161);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS977, _M0L1kS979, _M0L6_2atmpS2160);
      joinlet_2282:;
      _M0L6_2atmpS2162 = _M0L1kS979 + 1;
      _M0L1kS979 = _M0L6_2atmpS2162;
      continue;
    }
    break;
  }
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1gS986 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1iS987 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L6_2atmpS2163 = _M0MP26RiantR8snn__mbt11WCParameter3new();
  _block_2283
  = (struct _M0TP26RiantR8snn__mbt11WilsonCowan*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11WilsonCowan));
  Moonbit_object_header(_block_2283)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 23, 0);
  _block_2283->$0 = _M0L6_2atmpS2163;
  _block_2283->$1 = _M0L1nS976;
  _block_2283->$2 = _M0L1xS975;
  _block_2283->$3 = _M0L1rS977;
  _block_2283->$4 = _M0L1gS986;
  _block_2283->$5 = _M0L1iS987;
  return _block_2283;
}

struct _M0TP26RiantR8snn__mbt11WCParameter* _M0MP26RiantR8snn__mbt11WCParameter3new(
  
) {
  struct _M0TP26RiantR8snn__mbt11WCParameter* _block_2284;
  #line 19 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _block_2284
  = (struct _M0TP26RiantR8snn__mbt11WCParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11WCParameter));
  Moonbit_object_header(_block_2284)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2284->$0 = 0x0p+0f;
  return _block_2284;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS970,
  float _M0L2dtS973
) {
  int32_t _M0L1nS969;
  int32_t _M0L7_2abindS971;
  int32_t _M0L1kS972;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS969 = _M0L1pS970->$1;
  _M0L7_2abindS971 = 0;
  _M0L1kS972 = _M0L7_2abindS971;
  while (1) {
    if (_M0L1kS972 < _M0L1nS969) {
      struct _M0TPB5ArrayGfE* _M0L1xS2138 = _M0L1pS970->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS2151 = _M0L1pS970->$2;
      float _M0L6_2atmpS2140;
      struct _M0TPB5ArrayGfE* _M0L1xS2150;
      float _M0L6_2atmpS2149;
      float _M0L6_2atmpS2146;
      struct _M0TPB5ArrayGfE* _M0L1gS2148;
      float _M0L6_2atmpS2147;
      float _M0L6_2atmpS2143;
      struct _M0TPB5ArrayGfE* _M0L1iS2145;
      float _M0L6_2atmpS2144;
      float _M0L6_2atmpS2142;
      float _M0L6_2atmpS2141;
      float _M0L6_2atmpS2139;
      struct _M0TPB5ArrayGfE* _M0L1rS2152;
      struct _M0TPB5ArrayGfE* _M0L1xS2155;
      float _M0L6_2atmpS2154;
      float _M0L6_2atmpS2153;
      struct _M0TPB5ArrayGfE* _M0L1gS2156;
      int32_t _M0L6_2atmpS2157;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2140 = _M0MPC15array5Array2atGfE(_M0L1xS2151, _M0L1kS972);
      _M0L1xS2150 = _M0L1pS970->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2149 = _M0MPC15array5Array2atGfE(_M0L1xS2150, _M0L1kS972);
      _M0L6_2atmpS2146 = -_M0L6_2atmpS2149;
      _M0L1gS2148 = _M0L1pS970->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2147 = _M0MPC15array5Array2atGfE(_M0L1gS2148, _M0L1kS972);
      _M0L6_2atmpS2143 = _M0L6_2atmpS2146 + _M0L6_2atmpS2147;
      _M0L1iS2145 = _M0L1pS970->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2144 = _M0MPC15array5Array2atGfE(_M0L1iS2145, _M0L1kS972);
      _M0L6_2atmpS2142 = _M0L6_2atmpS2143 + _M0L6_2atmpS2144;
      _M0L6_2atmpS2141 = _M0L2dtS973 * _M0L6_2atmpS2142;
      _M0L6_2atmpS2139 = _M0L6_2atmpS2140 + _M0L6_2atmpS2141;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS2138, _M0L1kS972, _M0L6_2atmpS2139);
      _M0L1rS2152 = _M0L1pS970->$3;
      _M0L1xS2155 = _M0L1pS970->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2154 = _M0MPC15array5Array2atGfE(_M0L1xS2155, _M0L1kS972);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2153 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS2154);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS2152, _M0L1kS972, _M0L6_2atmpS2153);
      _M0L1gS2156 = _M0L1pS970->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS2156, _M0L1kS972, 0x0p+0f);
      _M0L6_2atmpS2157 = _M0L1kS972 + 1;
      _M0L1kS972 = _M0L6_2atmpS2157;
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR13forward__rate(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS955,
  struct _M0TPB5ArrayGfE* _M0L9pre__rateS961,
  struct _M0TPB5ArrayGfE* _M0L7post__gS967
) {
  int32_t _M0L4rowsS954;
  int32_t _M0L7_2abindS956;
  int32_t _M0L1iS957;
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS954 = _M0L1mS955->$0;
  _M0L7_2abindS956 = 0;
  _M0L1iS957 = _M0L7_2abindS956;
  while (1) {
    if (_M0L1iS957 < _M0L4rowsS954) {
      float _M0L4r__iS960;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2137;
      int32_t _M0L5startS962;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2135;
      int32_t _M0L6_2atmpS2136;
      int32_t _M0L3endS963;
      int32_t _M0L1kS964;
      int32_t _M0L6_2atmpS2128;
      #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L4r__iS960
      = _M0MPC15array5Array2atGfE(_M0L9pre__rateS961, _M0L1iS957);
      if (_M0L4r__iS960 == 0x0p+0f) {
        goto join_958;
      }
      _M0L6rowptrS2137 = _M0L1mS955->$2;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L5startS962
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2137, _M0L1iS957);
      _M0L6rowptrS2135 = _M0L1mS955->$2;
      _M0L6_2atmpS2136 = _M0L1iS957 + 1;
      #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3endS963
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2135, _M0L6_2atmpS2136);
      _M0L1kS964 = _M0L5startS962;
      while (1) {
        if (_M0L1kS964 < _M0L3endS963) {
          struct _M0TPB5ArrayGiE* _M0L6colptrS2133 = _M0L1mS955->$3;
          int32_t _M0L9post__idxS965;
          struct _M0TPB5ArrayGfE* _M0L4valsS2132;
          float _M0L1wS966;
          float _M0L6_2atmpS2130;
          float _M0L6_2atmpS2131;
          float _M0L6_2atmpS2129;
          int32_t _M0L6_2atmpS2134;
          #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L9post__idxS965
          = _M0MPC15array5Array2atGiE(_M0L6colptrS2133, _M0L1kS964);
          _M0L4valsS2132 = _M0L1mS955->$4;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1wS966 = _M0MPC15array5Array2atGfE(_M0L4valsS2132, _M0L1kS964);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2130
          = _M0MPC15array5Array2atGfE(_M0L7post__gS967, _M0L9post__idxS965);
          _M0L6_2atmpS2131 = _M0L1wS966 * _M0L4r__iS960;
          _M0L6_2atmpS2129 = _M0L6_2atmpS2130 + _M0L6_2atmpS2131;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L7post__gS967, _M0L9post__idxS965, _M0L6_2atmpS2129);
          _M0L6_2atmpS2134 = _M0L1kS964 + 1;
          _M0L1kS964 = _M0L6_2atmpS2134;
          continue;
        }
        break;
      }
      goto join_958;
      goto joinlet_2287;
      join_958:;
      _M0L6_2atmpS2128 = _M0L1iS957 + 1;
      _M0L1iS957 = _M0L6_2atmpS2128;
      continue;
      joinlet_2287:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS948,
  int32_t _M0L4colsS949,
  float _M0L2muS950,
  float _M0L5sigmaS951,
  float _M0L1pS952,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS953
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS948, _M0L4colsS949, _M0L2muS950, _M0L5sigmaS951, _M0L1pS952, 0, _M0L3rngS953);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS862,
  int32_t _M0L4colsS866,
  float _M0L2muS872,
  float _M0L5sigmaS873,
  float _M0L1pS885,
  int32_t _M0L4ruleS879,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS875
) {
  float* _M0L6_2atmpS2127;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2126;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS861;
  int32_t _M0L7_2abindS863;
  int32_t _M0L1iS864;
  int32_t _M0L6_2atmpS2125;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS938;
  int32_t* _M0L6_2atmpS2124;
  struct _M0TPB5ArrayGiE* _M0L6colptrS939;
  float* _M0L6_2atmpS2123;
  struct _M0TPB5ArrayGfE* _M0L4valsS940;
  int32_t _M0L7_2abindS941;
  int32_t _M0L1iS942;
  int32_t _M0L6_2atmpS2122;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2308;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2127 = moonbit_empty_float_array;
  _M0L6_2atmpS2126
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _M0L6_2atmpS2126->$0 = _M0L6_2atmpS2127;
  _M0L6_2atmpS2126->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS861
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS862, _M0L6_2atmpS2126);
  _M0L7_2abindS863 = 0;
  _M0L1iS864 = _M0L7_2abindS863;
  while (1) {
    if (_M0L1iS864 < _M0L4rowsS862) {
      struct _M0TPB5ArrayGfE* _M0L3rowS865;
      int32_t _M0L7_2abindS867;
      int32_t _M0L1jS868;
      int32_t _M0L6_2atmpS2078;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS865 = _M0MPC15array5Array4makeGfE(_M0L4colsS866, 0x0p+0f);
      _M0L7_2abindS867 = 0;
      _M0L1jS868 = _M0L7_2abindS867;
      while (1) {
        if (_M0L1jS868 < _M0L4colsS866) {
          double _M0L2z1S870;
          struct _M0TUddE* _M0L7_2abindS874;
          double _M0L5_2az1S876;
          float _M0L6_2atmpS2076;
          float _M0L6_2atmpS2075;
          float _M0L1wS871;
          int32_t _M0L6_2atmpS2077;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS874
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS875);
          _M0L5_2az1S876 = _M0L7_2abindS874->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS874);
          _M0L2z1S870 = _M0L5_2az1S876;
          goto join_869;
          goto joinlet_2291;
          join_869:;
          _M0L6_2atmpS2076 = (float)_M0L2z1S870;
          _M0L6_2atmpS2075 = _M0L5sigmaS873 * _M0L6_2atmpS2076;
          _M0L1wS871 = _M0L2muS872 + _M0L6_2atmpS2075;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS865, _M0L1jS868, _M0L1wS871);
          joinlet_2291:;
          _M0L6_2atmpS2077 = _M0L1jS868 + 1;
          _M0L1jS868 = _M0L6_2atmpS2077;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS864, _M0L3rowS865);
      _M0L6_2atmpS2078 = _M0L1iS864 + 1;
      _M0L1iS864 = _M0L6_2atmpS2078;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS879) {
    case 0: {
      int32_t _M0L7_2abindS880 = 0;
      int32_t _M0L1iS881 = _M0L7_2abindS880;
      while (1) {
        if (_M0L1iS881 < _M0L4rowsS862) {
          int32_t _M0L7_2abindS882 = 0;
          int32_t _M0L1jS883 = _M0L7_2abindS882;
          int32_t _M0L6_2atmpS2081;
          while (1) {
            if (_M0L1jS883 < _M0L4colsS866) {
              float _M0L1uS884;
              int32_t _M0L6_2atmpS2080;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS884 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
              if (_M0L1uS884 >= _M0L1pS885) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2079;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2079
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS881);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2079, _M0L1jS883, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2079);
              }
              _M0L6_2atmpS2080 = _M0L1jS883 + 1;
              _M0L1jS883 = _M0L6_2atmpS2080;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2081 = _M0L1iS881 + 1;
          _M0L1iS881 = _M0L6_2atmpS2081;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2099 = (float)_M0L4rowsS862;
      float _M0L6_2atmpS2098 = _M0L6_2atmpS2099 * _M0L1pS885;
      int32_t _M0L7n__keepS888;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS888 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2098);
      if (_M0L7n__keepS888 > 0 && _M0L7n__keepS888 <= _M0L4rowsS862) {
        int32_t _M0L7_2abindS889 = 0;
        int32_t _M0L1jS890 = _M0L7_2abindS889;
        while (1) {
          if (_M0L1jS890 < _M0L4colsS866) {
            int32_t* _M0L6_2atmpS2093 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS891 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS892;
            int32_t _M0L1kS893;
            int32_t _M0L7n__dropS895;
            int32_t _M0L7_2abindS896;
            int32_t _M0L1kS897;
            int32_t _M0L7_2abindS903;
            int32_t _M0L1kS904;
            int32_t _M0L6_2atmpS2094;
            Moonbit_object_header(_M0L8pre__idxS891)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
            _M0L8pre__idxS891->$0 = _M0L6_2atmpS2093;
            _M0L8pre__idxS891->$1 = 0;
            _M0L7_2abindS892 = 0;
            _M0L1kS893 = _M0L7_2abindS892;
            while (1) {
              if (_M0L1kS893 < _M0L4rowsS862) {
                int32_t _M0L6_2atmpS2082;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS891, _M0L1kS893);
                _M0L6_2atmpS2082 = _M0L1kS893 + 1;
                _M0L1kS893 = _M0L6_2atmpS2082;
                continue;
              }
              break;
            }
            _M0L7n__dropS895 = _M0L4rowsS862 - _M0L7n__keepS888;
            _M0L7_2abindS896 = 0;
            _M0L1kS897 = _M0L7_2abindS896;
            while (1) {
              if (_M0L1kS897 < _M0L7n__dropS895) {
                float _M0L1uS898;
                float _M0L6_2atmpS2086;
                float _M0L6_2atmpS2088;
                float _M0L6_2atmpS2087;
                float _M0L6_2atmpS2085;
                int32_t _M0L6_2atmpS2084;
                int32_t _M0L6r__idxS899;
                int32_t _M0L10r__clampedS900;
                int32_t _M0L3tmpS901;
                int32_t _M0L6_2atmpS2083;
                int32_t _M0L6_2atmpS2089;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS898 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
                _M0L6_2atmpS2086 = (float)_M0L4rowsS862;
                _M0L6_2atmpS2088 = (float)_M0L1kS897;
                _M0L6_2atmpS2087 = _M0L6_2atmpS2088 * _M0L1uS898;
                _M0L6_2atmpS2085 = _M0L6_2atmpS2086 - _M0L6_2atmpS2087;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2084
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2085);
                _M0L6r__idxS899 = _M0L1kS897 + _M0L6_2atmpS2084;
                if (_M0L6r__idxS899 >= _M0L4rowsS862) {
                  _M0L10r__clampedS900 = _M0L4rowsS862 - 1;
                } else {
                  _M0L10r__clampedS900 = _M0L6r__idxS899;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS901
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L1kS897);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2083
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L10r__clampedS900);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS891, _M0L1kS897, _M0L6_2atmpS2083);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS891, _M0L10r__clampedS900, _M0L3tmpS901);
                _M0L6_2atmpS2089 = _M0L1kS897 + 1;
                _M0L1kS897 = _M0L6_2atmpS2089;
                continue;
              }
              break;
            }
            _M0L7_2abindS903 = 0;
            _M0L1kS904 = _M0L7_2abindS903;
            while (1) {
              if (_M0L1kS904 < _M0L7n__dropS895) {
                int32_t _M0L6_2atmpS2091;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2090;
                int32_t _M0L6_2atmpS2092;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2091
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L1kS904);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2090
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L6_2atmpS2091);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2090, _M0L1jS890, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2090);
                _M0L6_2atmpS2092 = _M0L1kS904 + 1;
                _M0L1kS904 = _M0L6_2atmpS2092;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS891);
              }
              break;
            }
            _M0L6_2atmpS2094 = _M0L1jS890 + 1;
            _M0L1jS890 = _M0L6_2atmpS2094;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS888 == 0) {
        int32_t _M0L7_2abindS907 = 0;
        int32_t _M0L1iS908 = _M0L7_2abindS907;
        while (1) {
          if (_M0L1iS908 < _M0L4rowsS862) {
            int32_t _M0L7_2abindS909 = 0;
            int32_t _M0L1jS910 = _M0L7_2abindS909;
            int32_t _M0L6_2atmpS2097;
            while (1) {
              if (_M0L1jS910 < _M0L4colsS866) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2095;
                int32_t _M0L6_2atmpS2096;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2095
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS908);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2095, _M0L1jS910, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2095);
                _M0L6_2atmpS2096 = _M0L1jS910 + 1;
                _M0L1jS910 = _M0L6_2atmpS2096;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2097 = _M0L1iS908 + 1;
            _M0L1iS908 = _M0L6_2atmpS2097;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2117 = (float)_M0L4colsS866;
      float _M0L6_2atmpS2116 = _M0L6_2atmpS2117 * _M0L1pS885;
      int32_t _M0L7n__keepS913;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS913 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2116);
      if (_M0L7n__keepS913 > 0 && _M0L7n__keepS913 <= _M0L4colsS866) {
        int32_t _M0L7_2abindS914 = 0;
        int32_t _M0L1iS915 = _M0L7_2abindS914;
        while (1) {
          if (_M0L1iS915 < _M0L4rowsS862) {
            int32_t* _M0L6_2atmpS2111 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS916 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS917;
            int32_t _M0L1kS918;
            int32_t _M0L7n__dropS920;
            int32_t _M0L7_2abindS921;
            int32_t _M0L1kS922;
            int32_t _M0L7_2abindS928;
            int32_t _M0L1kS929;
            int32_t _M0L6_2atmpS2112;
            Moonbit_object_header(_M0L9post__idxS916)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
            _M0L9post__idxS916->$0 = _M0L6_2atmpS2111;
            _M0L9post__idxS916->$1 = 0;
            _M0L7_2abindS917 = 0;
            _M0L1kS918 = _M0L7_2abindS917;
            while (1) {
              if (_M0L1kS918 < _M0L4colsS866) {
                int32_t _M0L6_2atmpS2100;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS916, _M0L1kS918);
                _M0L6_2atmpS2100 = _M0L1kS918 + 1;
                _M0L1kS918 = _M0L6_2atmpS2100;
                continue;
              }
              break;
            }
            _M0L7n__dropS920 = _M0L4colsS866 - _M0L7n__keepS913;
            _M0L7_2abindS921 = 0;
            _M0L1kS922 = _M0L7_2abindS921;
            while (1) {
              if (_M0L1kS922 < _M0L7n__dropS920) {
                float _M0L1uS923;
                float _M0L6_2atmpS2104;
                float _M0L6_2atmpS2106;
                float _M0L6_2atmpS2105;
                float _M0L6_2atmpS2103;
                int32_t _M0L6_2atmpS2102;
                int32_t _M0L6r__idxS924;
                int32_t _M0L10r__clampedS925;
                int32_t _M0L3tmpS926;
                int32_t _M0L6_2atmpS2101;
                int32_t _M0L6_2atmpS2107;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS923 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
                _M0L6_2atmpS2104 = (float)_M0L4colsS866;
                _M0L6_2atmpS2106 = (float)_M0L1kS922;
                _M0L6_2atmpS2105 = _M0L6_2atmpS2106 * _M0L1uS923;
                _M0L6_2atmpS2103 = _M0L6_2atmpS2104 - _M0L6_2atmpS2105;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2102
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2103);
                _M0L6r__idxS924 = _M0L1kS922 + _M0L6_2atmpS2102;
                if (_M0L6r__idxS924 >= _M0L4colsS866) {
                  _M0L10r__clampedS925 = _M0L4colsS866 - 1;
                } else {
                  _M0L10r__clampedS925 = _M0L6r__idxS924;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS926
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L1kS922);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2101
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L10r__clampedS925);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS916, _M0L1kS922, _M0L6_2atmpS2101);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS916, _M0L10r__clampedS925, _M0L3tmpS926);
                _M0L6_2atmpS2107 = _M0L1kS922 + 1;
                _M0L1kS922 = _M0L6_2atmpS2107;
                continue;
              }
              break;
            }
            _M0L7_2abindS928 = 0;
            _M0L1kS929 = _M0L7_2abindS928;
            while (1) {
              if (_M0L1kS929 < _M0L7n__dropS920) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2108;
                int32_t _M0L6_2atmpS2109;
                int32_t _M0L6_2atmpS2110;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2108
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS915);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2109
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L1kS929);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2108, _M0L6_2atmpS2109, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2108);
                _M0L6_2atmpS2110 = _M0L1kS929 + 1;
                _M0L1kS929 = _M0L6_2atmpS2110;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS916);
              }
              break;
            }
            _M0L6_2atmpS2112 = _M0L1iS915 + 1;
            _M0L1iS915 = _M0L6_2atmpS2112;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS913 == 0) {
        int32_t _M0L7_2abindS932 = 0;
        int32_t _M0L1iS933 = _M0L7_2abindS932;
        while (1) {
          if (_M0L1iS933 < _M0L4rowsS862) {
            int32_t _M0L7_2abindS934 = 0;
            int32_t _M0L1jS935 = _M0L7_2abindS934;
            int32_t _M0L6_2atmpS2115;
            while (1) {
              if (_M0L1jS935 < _M0L4colsS866) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2113;
                int32_t _M0L6_2atmpS2114;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2113
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS933);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2113, _M0L1jS935, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2113);
                _M0L6_2atmpS2114 = _M0L1jS935 + 1;
                _M0L1jS935 = _M0L6_2atmpS2114;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2115 = _M0L1iS933 + 1;
            _M0L1iS933 = _M0L6_2atmpS2115;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2125 = _M0L4rowsS862 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS938 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2125, 0);
  _M0L6_2atmpS2124 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS939
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _M0L6colptrS939->$0 = _M0L6_2atmpS2124;
  _M0L6colptrS939->$1 = 0;
  _M0L6_2atmpS2123 = moonbit_empty_float_array;
  _M0L4valsS940
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS940)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _M0L4valsS940->$0 = _M0L6_2atmpS2123;
  _M0L4valsS940->$1 = 0;
  _M0L7_2abindS941 = 0;
  _M0L1iS942 = _M0L7_2abindS941;
  while (1) {
    if (_M0L1iS942 < _M0L4rowsS862) {
      int32_t _M0L6_2atmpS2118;
      int32_t _M0L7_2abindS943;
      int32_t _M0L1jS944;
      int32_t _M0L6_2atmpS2121;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2118 = _M0MPC15array5Array6lengthGfE(_M0L4valsS940);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS938, _M0L1iS942, _M0L6_2atmpS2118);
      _M0L7_2abindS943 = 0;
      _M0L1jS944 = _M0L7_2abindS943;
      while (1) {
        if (_M0L1jS944 < _M0L4colsS866) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2119;
          float _M0L1vS945;
          int32_t _M0L6_2atmpS2120;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2119
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS942);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS945
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2119, _M0L1jS944);
          moonbit_decref_cycle_free(_M0L6_2atmpS2119);
          if (_M0L1vS945 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS939, _M0L1jS944);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS940, _M0L1vS945);
          }
          _M0L6_2atmpS2120 = _M0L1jS944 + 1;
          _M0L1jS944 = _M0L6_2atmpS2120;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2121 = _M0L1iS942 + 1;
      _M0L1iS942 = _M0L6_2atmpS2121;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS861);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2122 = _M0MPC15array5Array6lengthGfE(_M0L4valsS940);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS938, _M0L4rowsS862, _M0L6_2atmpS2122);
  _block_2308
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2308)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2308->$0 = _M0L4rowsS862;
  _block_2308->$1 = _M0L4colsS866;
  _block_2308->$2 = _M0L6rowptrS938;
  _block_2308->$3 = _M0L6colptrS939;
  _block_2308->$4 = _M0L4valsS940;
  return _block_2308;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS859
) {
  struct _M0TUmmmmE* _M0L1sS858;
  uint64_t _M0L6_2atmpS2074;
  struct _M0TUmmmmE* _M0L1tS860;
  uint64_t _M0L6_2atmpS2070;
  uint64_t _M0L6_2atmpS2071;
  uint64_t _M0L6_2atmpS2072;
  uint64_t _M0L6_2atmpS2073;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2309;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS858 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS859);
  _M0L6_2atmpS2074 = _M0L1sS858->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS860 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2074);
  _M0L6_2atmpS2070 = _M0L1sS858->$0;
  _M0L6_2atmpS2071 = _M0L1sS858->$1;
  _M0L6_2atmpS2072 = _M0L1sS858->$2;
  moonbit_decref_cycle_free(_M0L1sS858);
  _M0L6_2atmpS2073 = _M0L1tS860->$0;
  moonbit_decref_cycle_free(_M0L1tS860);
  _block_2309
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2309)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2309->$0 = _M0L6_2atmpS2070;
  _block_2309->$1 = _M0L6_2atmpS2071;
  _block_2309->$2 = _M0L6_2atmpS2072;
  _block_2309->$3 = _M0L6_2atmpS2073;
  return _block_2309;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS850) {
  uint64_t _M0L2s1S849;
  uint64_t _M0L2z1S851;
  uint64_t _M0L2s2S852;
  uint64_t _M0L2z2S853;
  uint64_t _M0L2s3S854;
  uint64_t _M0L2z3S855;
  uint64_t _M0L2s4S856;
  uint64_t _M0L2z4S857;
  struct _M0TUmmmmE* _block_2310;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S849 = _M0L4seedS850 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S851 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S849);
  _M0L2s2S852 = _M0L2s1S849 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S853 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S852);
  _M0L2s3S854 = _M0L2s2S852 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S855 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S854);
  _M0L2s4S856 = _M0L2s3S854 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S857 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S856);
  _block_2310 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2310)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2310->$0 = _M0L2z1S851;
  _block_2310->$1 = _M0L2z2S853;
  _block_2310->$2 = _M0L2z3S855;
  _block_2310->$3 = _M0L2z4S857;
  return _block_2310;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS847) {
  uint64_t _M0L6_2atmpS2069;
  uint64_t _M0L6_2atmpS2068;
  uint64_t _M0L1zS846;
  uint64_t _M0L6_2atmpS2067;
  uint64_t _M0L6_2atmpS2066;
  uint64_t _M0L1zS848;
  uint64_t _M0L6_2atmpS2065;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2069 = _M0L1zS847 >> 30;
  _M0L6_2atmpS2068 = _M0L1zS847 ^ _M0L6_2atmpS2069;
  _M0L1zS846 = _M0L6_2atmpS2068 * 13787848793156543929ull;
  _M0L6_2atmpS2067 = _M0L1zS846 >> 27;
  _M0L6_2atmpS2066 = _M0L1zS846 ^ _M0L6_2atmpS2067;
  _M0L1zS848 = _M0L6_2atmpS2066 * 10723151780598845931ull;
  _M0L6_2atmpS2065 = _M0L1zS848 >> 31;
  return _M0L1zS848 ^ _M0L6_2atmpS2065;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS841
) {
  double _M0L2u1S840;
  double _M0L8u1__safeS842;
  double _M0L2u2S843;
  double _M0L6_2atmpS2064;
  double _M0L6_2atmpS2063;
  double _M0L1rS844;
  double _M0L5thetaS845;
  double _M0L6_2atmpS2062;
  double _M0L6_2atmpS2059;
  double _M0L6_2atmpS2061;
  double _M0L6_2atmpS2060;
  struct _M0TUddE* _block_2311;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S840 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS841);
  if (_M0L2u1S840 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS842 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS842 = _M0L2u1S840;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S843 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS841);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2064 = _M0FPC14math2ln(_M0L8u1__safeS842);
  _M0L6_2atmpS2063 = -0x1p+1 * _M0L6_2atmpS2064;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS844 = sqrt(_M0L6_2atmpS2063);
  _M0L5thetaS845 = 0x1.921fb54442d18p+2 * _M0L2u2S843;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2062 = _M0FPC14math3cos(_M0L5thetaS845);
  _M0L6_2atmpS2059 = _M0L1rS844 * _M0L6_2atmpS2062;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2061 = _M0FPC14math3sin(_M0L5thetaS845);
  _M0L6_2atmpS2060 = _M0L1rS844 * _M0L6_2atmpS2061;
  _block_2311 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2311)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2311->$0 = _M0L6_2atmpS2059;
  _block_2311->$1 = _M0L6_2atmpS2060;
  return _block_2311;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS838
) {
  uint64_t _M0L1uS837;
  uint64_t _M0L4bitsS839;
  double _M0L6_2atmpS2058;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS837 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS838);
  _M0L4bitsS839 = _M0L1uS837 >> 11;
  _M0L6_2atmpS2058 = (double)_M0L4bitsS839;
  return _M0L6_2atmpS2058 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS835
) {
  uint32_t _M0L1uS834;
  uint32_t _M0L4bitsS836;
  double _M0L6_2atmpS2057;
  double _M0L6_2atmpS2056;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS834 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS835);
  _M0L4bitsS836 = _M0L1uS834 >> 8;
  _M0L6_2atmpS2057 = (double)_M0L4bitsS836;
  _M0L6_2atmpS2056 = _M0L6_2atmpS2057 * 0x1p-24;
  return (float)_M0L6_2atmpS2056;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS833
) {
  uint64_t _M0L1uS832;
  uint64_t _M0L6_2atmpS2055;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS832 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS833);
  _M0L6_2atmpS2055 = _M0L1uS832 >> 32;
  return (uint32_t)_M0L6_2atmpS2055;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS825
) {
  uint64_t _M0L2s0S824;
  uint64_t _M0L2s1S826;
  uint64_t _M0L2s2S827;
  uint64_t _M0L2s3S828;
  uint64_t _M0L3tmpS829;
  uint64_t _M0L6_2atmpS2054;
  uint64_t _M0L3resS830;
  uint64_t _M0L1tS831;
  uint64_t _M0L6_2atmpS2044;
  uint64_t _M0L6_2atmpS2045;
  uint64_t _M0L2s2S2047;
  uint64_t _M0L6_2atmpS2046;
  uint64_t _M0L2s3S2049;
  uint64_t _M0L6_2atmpS2048;
  uint64_t _M0L2s2S2051;
  uint64_t _M0L6_2atmpS2050;
  uint64_t _M0L2s3S2053;
  uint64_t _M0L6_2atmpS2052;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S824 = _M0L1rS825->$0;
  _M0L2s1S826 = _M0L1rS825->$1;
  _M0L2s2S827 = _M0L1rS825->$2;
  _M0L2s3S828 = _M0L1rS825->$3;
  _M0L3tmpS829 = _M0L2s0S824 + _M0L2s3S828;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2054 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS829, 23);
  _M0L3resS830 = _M0L6_2atmpS2054 + _M0L2s0S824;
  _M0L1tS831 = _M0L2s1S826 << 17;
  _M0L6_2atmpS2044 = _M0L2s2S827 ^ _M0L2s0S824;
  _M0L1rS825->$2 = _M0L6_2atmpS2044;
  _M0L6_2atmpS2045 = _M0L2s3S828 ^ _M0L2s1S826;
  _M0L1rS825->$3 = _M0L6_2atmpS2045;
  _M0L2s2S2047 = _M0L1rS825->$2;
  _M0L6_2atmpS2046 = _M0L2s1S826 ^ _M0L2s2S2047;
  _M0L1rS825->$1 = _M0L6_2atmpS2046;
  _M0L2s3S2049 = _M0L1rS825->$3;
  _M0L6_2atmpS2048 = _M0L2s0S824 ^ _M0L2s3S2049;
  _M0L1rS825->$0 = _M0L6_2atmpS2048;
  _M0L2s2S2051 = _M0L1rS825->$2;
  _M0L6_2atmpS2050 = _M0L2s2S2051 ^ _M0L1tS831;
  _M0L1rS825->$2 = _M0L6_2atmpS2050;
  _M0L2s3S2053 = _M0L1rS825->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2052 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2053, 45);
  _M0L1rS825->$3 = _M0L6_2atmpS2052;
  return _M0L3resS830;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS822, int32_t _M0L1kS823) {
  uint64_t _M0L6_2atmpS2041;
  int32_t _M0L6_2atmpS2043;
  uint64_t _M0L6_2atmpS2042;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2041 = _M0L1xS822 << (_M0L1kS823 & 63);
  _M0L6_2atmpS2043 = 64 - _M0L1kS823;
  _M0L6_2atmpS2042 = _M0L1xS822 >> (_M0L6_2atmpS2043 & 63);
  return _M0L6_2atmpS2041 | _M0L6_2atmpS2042;
}

double _M0FPC14math2ln(double _M0L1xS808) {
  struct _M0TUdiE* _M0L7_2abindS809;
  double _M0L5_2af1S810;
  int32_t _M0L5_2akiS811;
  double _M0L1fS813;
  double _M0L1kS814;
  double _M0L6_2atmpS2034;
  double _M0L1sS815;
  double _M0L2s2S816;
  double _M0L2s4S817;
  double _M0L6_2atmpS2033;
  double _M0L6_2atmpS2032;
  double _M0L6_2atmpS2031;
  double _M0L6_2atmpS2030;
  double _M0L6_2atmpS2029;
  double _M0L6_2atmpS2028;
  double _M0L2t1S818;
  double _M0L6_2atmpS2027;
  double _M0L6_2atmpS2026;
  double _M0L6_2atmpS2025;
  double _M0L6_2atmpS2024;
  double _M0L2t2S819;
  double _M0L1rS820;
  double _M0L6_2atmpS2023;
  double _M0L4hfsqS821;
  double _M0L6_2atmpS2016;
  double _M0L6_2atmpS2022;
  double _M0L6_2atmpS2020;
  double _M0L6_2atmpS2021;
  double _M0L6_2atmpS2019;
  double _M0L6_2atmpS2018;
  double _M0L6_2atmpS2017;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS808 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS808)
      || _M0MPC16double6Double7is__inf(_M0L1xS808)
    ) {
      return _M0L1xS808;
    } else if (_M0L1xS808 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS809 = _M0FPC14math5frexp(_M0L1xS808);
  _M0L5_2af1S810 = _M0L7_2abindS809->$0;
  _M0L5_2akiS811 = _M0L7_2abindS809->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS809);
  if (_M0L5_2af1S810 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2038 = _M0L5_2af1S810 * 0x1p+1;
    double _M0L6_2atmpS2035 = _M0L6_2atmpS2038 - 0x1p+0;
    int32_t _M0L6_2atmpS2037 = _M0L5_2akiS811 - 1;
    double _M0L6_2atmpS2036 = (double)_M0L6_2atmpS2037;
    _M0L1fS813 = _M0L6_2atmpS2035;
    _M0L1kS814 = _M0L6_2atmpS2036;
    goto join_812;
  } else {
    double _M0L6_2atmpS2039 = _M0L5_2af1S810 - 0x1p+0;
    double _M0L6_2atmpS2040 = (double)_M0L5_2akiS811;
    _M0L1fS813 = _M0L6_2atmpS2039;
    _M0L1kS814 = _M0L6_2atmpS2040;
    goto join_812;
  }
  join_812:;
  _M0L6_2atmpS2034 = 0x1p+1 + _M0L1fS813;
  _M0L1sS815 = _M0L1fS813 / _M0L6_2atmpS2034;
  _M0L2s2S816 = _M0L1sS815 * _M0L1sS815;
  _M0L2s4S817 = _M0L2s2S816 * _M0L2s2S816;
  _M0L6_2atmpS2033 = _M0L2s4S817 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2032 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2033;
  _M0L6_2atmpS2031 = _M0L2s4S817 * _M0L6_2atmpS2032;
  _M0L6_2atmpS2030 = 0x1.2492494229359p-2 + _M0L6_2atmpS2031;
  _M0L6_2atmpS2029 = _M0L2s4S817 * _M0L6_2atmpS2030;
  _M0L6_2atmpS2028 = 0x1.5555555555593p-1 + _M0L6_2atmpS2029;
  _M0L2t1S818 = _M0L2s2S816 * _M0L6_2atmpS2028;
  _M0L6_2atmpS2027 = _M0L2s4S817 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2026 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2027;
  _M0L6_2atmpS2025 = _M0L2s4S817 * _M0L6_2atmpS2026;
  _M0L6_2atmpS2024 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2025;
  _M0L2t2S819 = _M0L2s4S817 * _M0L6_2atmpS2024;
  _M0L1rS820 = _M0L2t1S818 + _M0L2t2S819;
  _M0L6_2atmpS2023 = 0x1p-1 * _M0L1fS813;
  _M0L4hfsqS821 = _M0L6_2atmpS2023 * _M0L1fS813;
  _M0L6_2atmpS2016 = _M0L1kS814 * 0x1.62e42feep-1;
  _M0L6_2atmpS2022 = _M0L4hfsqS821 + _M0L1rS820;
  _M0L6_2atmpS2020 = _M0L1sS815 * _M0L6_2atmpS2022;
  _M0L6_2atmpS2021 = _M0L1kS814 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2019 = _M0L6_2atmpS2020 + _M0L6_2atmpS2021;
  _M0L6_2atmpS2018 = _M0L4hfsqS821 - _M0L6_2atmpS2019;
  _M0L6_2atmpS2017 = _M0L6_2atmpS2018 - _M0L1fS813;
  return _M0L6_2atmpS2016 - _M0L6_2atmpS2017;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS801) {
  struct _M0TUdiE* _M0L7_2abindS802;
  double _M0L10_2anorm__fS803;
  int32_t _M0L6_2aexpS804;
  uint64_t _M0L1uS805;
  uint64_t _M0L6_2atmpS2015;
  uint64_t _M0L6_2atmpS2014;
  int32_t _M0L6_2atmpS2013;
  int32_t _M0L6_2atmpS2012;
  int32_t _M0L3expS806;
  uint64_t _M0L6_2atmpS2011;
  uint64_t _M0L6_2atmpS2010;
  uint64_t _M0L6_2atmpS2009;
  double _M0L4fracS807;
  struct _M0TUdiE* _block_2314;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS801 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS801)
    || _M0MPC16double6Double7is__nan(_M0L1fS801)
  ) {
    struct _M0TUdiE* _block_2313 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2313)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2313->$0 = _M0L1fS801;
    _block_2313->$1 = 0;
    return _block_2313;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS802 = _M0FPC14math9normalize(_M0L1fS801);
  _M0L10_2anorm__fS803 = _M0L7_2abindS802->$0;
  _M0L6_2aexpS804 = _M0L7_2abindS802->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS802);
  _M0L1uS805 = *(int64_t*)&_M0L10_2anorm__fS803;
  _M0L6_2atmpS2015 = _M0L1uS805 >> 52;
  _M0L6_2atmpS2014 = _M0L6_2atmpS2015 & 2047ull;
  _M0L6_2atmpS2013 = (int32_t)_M0L6_2atmpS2014;
  _M0L6_2atmpS2012 = _M0L6_2aexpS804 + _M0L6_2atmpS2013;
  _M0L3expS806 = _M0L6_2atmpS2012 - 1022;
  _M0L6_2atmpS2011 = ~9218868437227405312ull;
  _M0L6_2atmpS2010 = _M0L1uS805 & _M0L6_2atmpS2011;
  _M0L6_2atmpS2009 = _M0L6_2atmpS2010 | 4602678819172646912ull;
  _M0L4fracS807 = *(double*)&_M0L6_2atmpS2009;
  _block_2314 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2314)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2314->$0 = _M0L4fracS807;
  _block_2314->$1 = _M0L3expS806;
  return _block_2314;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS800) {
  double _M0L6_2atmpS2006;
  struct _M0TUdiE* _block_2316;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2006 = fabs(_M0L1fS800);
  if (_M0L6_2atmpS2006 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2008 = (double)4503599627370496ll;
    double _M0L6_2atmpS2007 = _M0L1fS800 * _M0L6_2atmpS2008;
    struct _M0TUdiE* _block_2315 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2315)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2315->$0 = _M0L6_2atmpS2007;
    _block_2315->$1 = -52;
    return _block_2315;
  }
  _block_2316 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2316)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2316->$0 = _M0L1fS800;
  _block_2316->$1 = 0;
  return _block_2316;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS799) {
  double _M0L6_2atmpS2005;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2005 = (double)_M0L4selfS799;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2005);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS798) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS798 != _M0L4selfS798) {
    return 0;
  } else if (_M0L4selfS798 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS798 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS798;
  }
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS784,
  struct _M0TPB5ArrayGfE* _M0L4elemS786
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS783;
  int32_t _M0L1iS785;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS783
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS784);
  _M0L1iS785 = 0;
  while (1) {
    if (_M0L1iS785 < _M0L3lenS784) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1999 = _M0L3arrS783->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2227 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1999[_M0L1iS785];
      int32_t _M0L6_2atmpS2000;
      moonbit_incref_cycle_free(_M0L4elemS786);
      if (_M0L6_2aoldS2227) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2227);
      }
      _M0L3bufS1999[_M0L1iS785] = _M0L4elemS786;
      _M0L6_2atmpS2000 = _M0L1iS785 + 1;
      _M0L1iS785 = _M0L6_2atmpS2000;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS786);
    }
    break;
  }
  return _M0L3arrS783;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS789,
  float _M0L4elemS791
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS788;
  int32_t _M0L1iS790;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS788 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS789);
  _M0L1iS790 = 0;
  while (1) {
    if (_M0L1iS790 < _M0L3lenS789) {
      float* _M0L3bufS2001 = _M0L3arrS788->$0;
      int32_t _M0L6_2atmpS2002;
      _M0L3bufS2001[_M0L1iS790] = _M0L4elemS791;
      _M0L6_2atmpS2002 = _M0L1iS790 + 1;
      _M0L1iS790 = _M0L6_2atmpS2002;
      continue;
    }
    break;
  }
  return _M0L3arrS788;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS794,
  int32_t _M0L4elemS796
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS793;
  int32_t _M0L1iS795;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS793 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS794);
  _M0L1iS795 = 0;
  while (1) {
    if (_M0L1iS795 < _M0L3lenS794) {
      int32_t* _M0L3bufS2003 = _M0L3arrS793->$0;
      int32_t _M0L6_2atmpS2004;
      _M0L3bufS2003[_M0L1iS795] = _M0L4elemS796;
      _M0L6_2atmpS2004 = _M0L1iS795 + 1;
      _M0L1iS795 = _M0L6_2atmpS2004;
      continue;
    }
    break;
  }
  return _M0L3arrS793;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS772,
  int32_t _M0L5indexS773,
  float _M0L5valueS774
) {
  int32_t _M0L3lenS771;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    float* _M0L6_2atmpS1996;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1996 = _M0MPC15array5Array6bufferGfE(_M0L4selfS772);
    _M0L6_2atmpS1996[_M0L5indexS773] = _M0L5valueS774;
    moonbit_decref_cycle_free(_M0L6_2atmpS1996);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS776,
  int32_t _M0L5indexS777,
  struct _M0TPB5ArrayGfE* _M0L5valueS778
) {
  int32_t _M0L3lenS775;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS775 = _M0L4selfS776->$1;
  if (_M0L5indexS777 >= 0 && _M0L5indexS777 < _M0L3lenS775) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1997;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2228;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1997
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS776);
    _M0L6_2aoldS2228
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1997[_M0L5indexS777];
    if (_M0L6_2aoldS2228) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2228);
    }
    _M0L6_2atmpS1997[_M0L5indexS777] = _M0L5valueS778;
    moonbit_decref_cycle_free(_M0L6_2atmpS1997);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS778);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS780,
  int32_t _M0L5indexS781,
  int32_t _M0L5valueS782
) {
  int32_t _M0L3lenS779;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS779 = _M0L4selfS780->$1;
  if (_M0L5indexS781 >= 0 && _M0L5indexS781 < _M0L3lenS779) {
    int32_t* _M0L6_2atmpS1998;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1998 = _M0MPC15array5Array6bufferGiE(_M0L4selfS780);
    _M0L6_2atmpS1998[_M0L5indexS781] = _M0L5valueS782;
    moonbit_decref_cycle_free(_M0L6_2atmpS1998);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS760,
  int32_t _M0L5indexS761
) {
  int32_t _M0L3lenS759;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS759 = _M0L4selfS760->$1;
  if (_M0L5indexS761 >= 0 && _M0L5indexS761 < _M0L3lenS759) {
    float* _M0L6_2atmpS1992;
    float _result_2320;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1992 = _M0MPC15array5Array6bufferGfE(_M0L4selfS760);
    _result_2320 = (float)_M0L6_2atmpS1992[_M0L5indexS761];
    moonbit_decref_cycle_free(_M0L6_2atmpS1992);
    return _result_2320;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    int32_t* _M0L6_2atmpS1993;
    int32_t _result_2321;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1993 = _M0MPC15array5Array6bufferGiE(_M0L4selfS763);
    _result_2321 = (int32_t)_M0L6_2atmpS1993[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS1993);
    return _result_2321;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1994;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2229;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1994
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS766);
    _M0L6_2atmpS2229
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1994[_M0L5indexS767];
    if (_M0L6_2atmpS2229) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2229);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1994);
    return _M0L6_2atmpS2229;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    moonbit_string_t* _M0L6_2atmpS1995;
    moonbit_string_t _M0L6_2atmpS2230;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1995 = _M0MPC15array5Array6bufferGsE(_M0L4selfS769);
    _M0L6_2atmpS2230 = (moonbit_string_t)_M0L6_2atmpS1995[_M0L5indexS770];
    moonbit_incref_cycle_free(_M0L6_2atmpS2230);
    moonbit_decref_cycle_free(_M0L6_2atmpS1995);
    return _M0L6_2atmpS2230;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS758) {
  moonbit_string_t _M0L6_2atmpS1991;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1991 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS758);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1991);
  moonbit_decref_cycle_free(_M0L6_2atmpS1991);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS757) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS757);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS756) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS756 > _M0FPB18double__max__value
         || _M0L4selfS756 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS755) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS755 != _M0L4selfS755;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS740) {
  uint64_t _M0L4bitsS743;
  uint64_t _M0L6_2atmpS1990;
  uint64_t _M0L6_2atmpS1989;
  int32_t _M0L8ieeeSignS744;
  uint64_t _M0L12ieeeMantissaS745;
  uint64_t _M0L6_2atmpS1988;
  uint64_t _M0L6_2atmpS1987;
  int32_t _M0L12ieeeExponentS746;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS747;
  struct _M0TPB17FloatingDecimal64* _M0L1vS748;
  moonbit_string_t _result_2323;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS740 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS740 >= -0x1p+53 && _M0L3valS740 <= 0x1p+53) {
    if (_M0L3valS740 >= -0x1p+31 && _M0L3valS740 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS741;
      double _M0L6_2atmpS1976;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS741 = _M0MPC16double6Double7to__int(_M0L3valS740);
      _M0L6_2atmpS1976 = (double)_M0L1iS741;
      if (_M0L6_2atmpS1976 == _M0L3valS740) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS741, 10);
      }
    } else {
      int64_t _M0L1iS742;
      double _M0L6_2atmpS1977;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS742 = _M0MPC16double6Double9to__int64(_M0L3valS740);
      _M0L6_2atmpS1977 = (double)_M0L1iS742;
      if (_M0L6_2atmpS1977 == _M0L3valS740) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS742, 10);
      }
    }
  }
  _M0L4bitsS743 = *(int64_t*)&_M0L3valS740;
  _M0L6_2atmpS1990 = _M0L4bitsS743 >> 63;
  _M0L6_2atmpS1989 = _M0L6_2atmpS1990 & 1ull;
  _M0L8ieeeSignS744 = _M0L6_2atmpS1989 != 0ull;
  _M0L12ieeeMantissaS745 = _M0L4bitsS743 & 4503599627370495ull;
  _M0L6_2atmpS1988 = _M0L4bitsS743 >> 52;
  _M0L6_2atmpS1987 = _M0L6_2atmpS1988 & 2047ull;
  _M0L12ieeeExponentS746 = (int32_t)_M0L6_2atmpS1987;
  if (
    _M0L12ieeeExponentS746 == 2047
    || _M0L12ieeeExponentS746 == 0 && _M0L12ieeeMantissaS745 == 0ull
  ) {
    int32_t _M0L6_2atmpS1978 = _M0L12ieeeExponentS746 != 0;
    int32_t _M0L6_2atmpS1979 = _M0L12ieeeMantissaS745 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS744, _M0L6_2atmpS1978, _M0L6_2atmpS1979);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS747
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS745, _M0L12ieeeExponentS746);
  if (_M0L7_2abindS747 == 0) {
    uint32_t _M0L6_2atmpS1980;
    if (_M0L7_2abindS747) {
      moonbit_decref_cycle_free(_M0L7_2abindS747);
    }
    _M0L6_2atmpS1980 = *(uint32_t*)&_M0L12ieeeExponentS746;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS748 = _M0FPB3d2d(_M0L12ieeeMantissaS745, _M0L6_2atmpS1980);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS749 = _M0L7_2abindS747;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS750 = _M0L7_2aSomeS749;
    struct _M0TPB17FloatingDecimal64* _M0L1xS751 = _M0L4_2afS750;
    while (1) {
      uint64_t _M0L8mantissaS1986 = _M0L1xS751->$0;
      uint64_t _M0L1qS752 = _M0L8mantissaS1986 / 10ull;
      uint64_t _M0L8mantissaS1984 = _M0L1xS751->$0;
      uint64_t _M0L6_2atmpS1985 = 10ull * _M0L1qS752;
      uint64_t _M0L1rS753 = _M0L8mantissaS1984 - _M0L6_2atmpS1985;
      int32_t _M0L8exponentS1983;
      int32_t _M0L6_2atmpS1982;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1981;
      if (_M0L1rS753 != 0ull) {
        _M0L1vS748 = _M0L1xS751;
        break;
      }
      _M0L8exponentS1983 = _M0L1xS751->$1;
      moonbit_decref_cycle_free(_M0L1xS751);
      _M0L6_2atmpS1982 = _M0L8exponentS1983 + 1;
      _M0L6_2atmpS1981
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1981)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1981->$0 = _M0L1qS752;
      _M0L6_2atmpS1981->$1 = _M0L6_2atmpS1982;
      _M0L1xS751 = _M0L6_2atmpS1981;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2323 = _M0FPB9to__chars(_M0L1vS748, _M0L8ieeeSignS744);
  moonbit_decref_cycle_free(_M0L1vS748);
  return _result_2323;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS735,
  int32_t _M0L12ieeeExponentS737
) {
  uint64_t _M0L2m2S734;
  int32_t _M0L6_2atmpS1975;
  int32_t _M0L2e2S736;
  int32_t _M0L6_2atmpS1974;
  uint64_t _M0L6_2atmpS1973;
  uint64_t _M0L4maskS738;
  uint64_t _M0L8fractionS739;
  int32_t _M0L6_2atmpS1972;
  uint64_t _M0L6_2atmpS1971;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1970;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S734 = 4503599627370496ull | _M0L12ieeeMantissaS735;
  _M0L6_2atmpS1975 = _M0L12ieeeExponentS737 - 1023;
  _M0L2e2S736 = _M0L6_2atmpS1975 - 52;
  if (_M0L2e2S736 > 0) {
    return 0;
  }
  if (_M0L2e2S736 < -52) {
    return 0;
  }
  _M0L6_2atmpS1974 = -_M0L2e2S736;
  _M0L6_2atmpS1973 = 1ull << (_M0L6_2atmpS1974 & 63);
  _M0L4maskS738 = _M0L6_2atmpS1973 - 1ull;
  _M0L8fractionS739 = _M0L2m2S734 & _M0L4maskS738;
  if (_M0L8fractionS739 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1972 = -_M0L2e2S736;
  _M0L6_2atmpS1971 = _M0L2m2S734 >> (_M0L6_2atmpS1972 & 63);
  _M0L6_2atmpS1970
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1970)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1970->$0 = _M0L6_2atmpS1971;
  _M0L6_2atmpS1970->$1 = 0;
  return _M0L6_2atmpS1970;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS702,
  int32_t _M0L4signS700
) {
  moonbit_bytes_t _M0L6resultS698;
  int32_t _M0Lm5indexS699;
  uint64_t _M0L6outputS701;
  int32_t _M0L7olengthS703;
  int32_t _M0L8exponentS1969;
  int32_t _M0L6_2atmpS1968;
  int32_t _M0Lm3expS704;
  int32_t _M0L6_2atmpS1967;
  int32_t _M0L6_2atmpS1965;
  int32_t _M0L18scientificNotationS705;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS698 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS699 = 0;
  if (_M0L4signS700) {
    int32_t _M0L6_2atmpS1839 = _M0Lm5indexS699;
    int32_t _M0L6_2atmpS1840;
    if (
      _M0L6_2atmpS1839 < 0
      || _M0L6_2atmpS1839 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1839] = 45;
    _M0L6_2atmpS1840 = _M0Lm5indexS699;
    _M0Lm5indexS699 = _M0L6_2atmpS1840 + 1;
  }
  _M0L6outputS701 = _M0L1vS702->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS703 = _M0FPB17decimal__length17(_M0L6outputS701);
  _M0L8exponentS1969 = _M0L1vS702->$1;
  _M0L6_2atmpS1968 = _M0L8exponentS1969 + _M0L7olengthS703;
  _M0Lm3expS704 = _M0L6_2atmpS1968 - 1;
  _M0L6_2atmpS1967 = _M0Lm3expS704;
  if (_M0L6_2atmpS1967 >= -6) {
    int32_t _M0L6_2atmpS1966 = _M0Lm3expS704;
    _M0L6_2atmpS1965 = _M0L6_2atmpS1966 < 21;
  } else {
    _M0L6_2atmpS1965 = 0;
  }
  _M0L18scientificNotationS705 = !_M0L6_2atmpS1965;
  if (_M0L18scientificNotationS705) {
    int32_t _M0L7_2abindS706 = _M0L7olengthS703 - 1;
    uint64_t _M0L6outputS707;
    int32_t _M0L1iS708 = 0;
    uint64_t _M0L6outputS709 = _M0L6outputS701;
    int32_t _M0L6_2atmpS1841;
    int32_t _M0L6_2atmpS1845;
    int32_t _M0L6_2atmpS1844;
    int32_t _M0L6_2atmpS1843;
    int32_t _M0L6_2atmpS1842;
    int32_t _M0L6_2atmpS1849;
    int32_t _M0L6_2atmpS1850;
    int32_t _M0L6_2atmpS1851;
    int32_t _M0L6_2atmpS1852;
    int32_t _M0L6_2atmpS1853;
    int32_t _M0L6_2atmpS1859;
    int32_t _M0L6_2atmpS1892;
    moonbit_string_t _result_2325;
    while (1) {
      if (_M0L1iS708 < _M0L7_2abindS706) {
        uint64_t _M0L1cS710 = _M0L6outputS709 % 10ull;
        int32_t _M0L6_2atmpS1898 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1897 = _M0L6_2atmpS1898 + _M0L7olengthS703;
        int32_t _M0L6_2atmpS1893 = _M0L6_2atmpS1897 - _M0L1iS708;
        int32_t _M0L6_2atmpS1896 = (int32_t)_M0L1cS710;
        int32_t _M0L6_2atmpS1895 = 48 + _M0L6_2atmpS1896;
        int32_t _M0L6_2atmpS1894 = _M0L6_2atmpS1895 & 0xff;
        int32_t _M0L6_2atmpS1899;
        uint64_t _M0L6_2atmpS1900;
        if (
          _M0L6_2atmpS1893 < 0
          || _M0L6_2atmpS1893 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1893] = _M0L6_2atmpS1894;
        _M0L6_2atmpS1899 = _M0L1iS708 + 1;
        _M0L6_2atmpS1900 = _M0L6outputS709 / 10ull;
        _M0L1iS708 = _M0L6_2atmpS1899;
        _M0L6outputS709 = _M0L6_2atmpS1900;
        continue;
      } else {
        _M0L6outputS707 = _M0L6outputS709;
      }
      break;
    }
    _M0L6_2atmpS1841 = _M0Lm5indexS699;
    _M0L6_2atmpS1845 = (int32_t)_M0L6outputS707;
    _M0L6_2atmpS1844 = _M0L6_2atmpS1845 % 10;
    _M0L6_2atmpS1843 = 48 + _M0L6_2atmpS1844;
    _M0L6_2atmpS1842 = _M0L6_2atmpS1843 & 0xff;
    if (
      _M0L6_2atmpS1841 < 0
      || _M0L6_2atmpS1841 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1841] = _M0L6_2atmpS1842;
    if (_M0L7olengthS703 > 1) {
      int32_t _M0L6_2atmpS1847 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1846 = _M0L6_2atmpS1847 + 1;
      if (
        _M0L6_2atmpS1846 < 0
        || _M0L6_2atmpS1846 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1846] = 46;
    } else {
      int32_t _M0L6_2atmpS1848 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1848 - 1;
    }
    _M0L6_2atmpS1849 = _M0Lm5indexS699;
    _M0L6_2atmpS1850 = _M0L7olengthS703 + 1;
    _M0Lm5indexS699 = _M0L6_2atmpS1849 + _M0L6_2atmpS1850;
    _M0L6_2atmpS1851 = _M0Lm5indexS699;
    if (
      _M0L6_2atmpS1851 < 0
      || _M0L6_2atmpS1851 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1851] = 101;
    _M0L6_2atmpS1852 = _M0Lm5indexS699;
    _M0Lm5indexS699 = _M0L6_2atmpS1852 + 1;
    _M0L6_2atmpS1853 = _M0Lm3expS704;
    if (_M0L6_2atmpS1853 < 0) {
      int32_t _M0L6_2atmpS1854 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1855;
      int32_t _M0L6_2atmpS1856;
      if (
        _M0L6_2atmpS1854 < 0
        || _M0L6_2atmpS1854 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1854] = 45;
      _M0L6_2atmpS1855 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1855 + 1;
      _M0L6_2atmpS1856 = _M0Lm3expS704;
      _M0Lm3expS704 = -_M0L6_2atmpS1856;
    } else {
      int32_t _M0L6_2atmpS1857 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1858;
      if (
        _M0L6_2atmpS1857 < 0
        || _M0L6_2atmpS1857 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1857] = 43;
      _M0L6_2atmpS1858 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1858 + 1;
    }
    _M0L6_2atmpS1859 = _M0Lm3expS704;
    if (_M0L6_2atmpS1859 >= 100) {
      int32_t _M0L6_2atmpS1875 = _M0Lm3expS704;
      int32_t _M0L1aS712 = _M0L6_2atmpS1875 / 100;
      int32_t _M0L6_2atmpS1874 = _M0Lm3expS704;
      int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 / 10;
      int32_t _M0L1bS713 = _M0L6_2atmpS1873 % 10;
      int32_t _M0L6_2atmpS1872 = _M0Lm3expS704;
      int32_t _M0L1cS714 = _M0L6_2atmpS1872 % 10;
      int32_t _M0L6_2atmpS1860 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1862 = 48 + _M0L1aS712;
      int32_t _M0L6_2atmpS1861 = _M0L6_2atmpS1862 & 0xff;
      int32_t _M0L6_2atmpS1866;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1870;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1869;
      int32_t _M0L6_2atmpS1868;
      int32_t _M0L6_2atmpS1871;
      if (
        _M0L6_2atmpS1860 < 0
        || _M0L6_2atmpS1860 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1860] = _M0L6_2atmpS1861;
      _M0L6_2atmpS1866 = _M0Lm5indexS699;
      _M0L6_2atmpS1863 = _M0L6_2atmpS1866 + 1;
      _M0L6_2atmpS1865 = 48 + _M0L1bS713;
      _M0L6_2atmpS1864 = _M0L6_2atmpS1865 & 0xff;
      if (
        _M0L6_2atmpS1863 < 0
        || _M0L6_2atmpS1863 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1863] = _M0L6_2atmpS1864;
      _M0L6_2atmpS1870 = _M0Lm5indexS699;
      _M0L6_2atmpS1867 = _M0L6_2atmpS1870 + 2;
      _M0L6_2atmpS1869 = 48 + _M0L1cS714;
      _M0L6_2atmpS1868 = _M0L6_2atmpS1869 & 0xff;
      if (
        _M0L6_2atmpS1867 < 0
        || _M0L6_2atmpS1867 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1867] = _M0L6_2atmpS1868;
      _M0L6_2atmpS1871 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1871 + 3;
    } else {
      int32_t _M0L6_2atmpS1876 = _M0Lm3expS704;
      if (_M0L6_2atmpS1876 >= 10) {
        int32_t _M0L6_2atmpS1886 = _M0Lm3expS704;
        int32_t _M0L1aS715 = _M0L6_2atmpS1886 / 10;
        int32_t _M0L6_2atmpS1885 = _M0Lm3expS704;
        int32_t _M0L1bS716 = _M0L6_2atmpS1885 % 10;
        int32_t _M0L6_2atmpS1877 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1879 = 48 + _M0L1aS715;
        int32_t _M0L6_2atmpS1878 = _M0L6_2atmpS1879 & 0xff;
        int32_t _M0L6_2atmpS1883;
        int32_t _M0L6_2atmpS1880;
        int32_t _M0L6_2atmpS1882;
        int32_t _M0L6_2atmpS1881;
        int32_t _M0L6_2atmpS1884;
        if (
          _M0L6_2atmpS1877 < 0
          || _M0L6_2atmpS1877 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1877] = _M0L6_2atmpS1878;
        _M0L6_2atmpS1883 = _M0Lm5indexS699;
        _M0L6_2atmpS1880 = _M0L6_2atmpS1883 + 1;
        _M0L6_2atmpS1882 = 48 + _M0L1bS716;
        _M0L6_2atmpS1881 = _M0L6_2atmpS1882 & 0xff;
        if (
          _M0L6_2atmpS1880 < 0
          || _M0L6_2atmpS1880 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1880] = _M0L6_2atmpS1881;
        _M0L6_2atmpS1884 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1884 + 2;
      } else {
        int32_t _M0L6_2atmpS1887 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1890 = _M0Lm3expS704;
        int32_t _M0L6_2atmpS1889 = 48 + _M0L6_2atmpS1890;
        int32_t _M0L6_2atmpS1888 = _M0L6_2atmpS1889 & 0xff;
        int32_t _M0L6_2atmpS1891;
        if (
          _M0L6_2atmpS1887 < 0
          || _M0L6_2atmpS1887 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1887] = _M0L6_2atmpS1888;
        _M0L6_2atmpS1891 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1891 + 1;
      }
    }
    _M0L6_2atmpS1892 = _M0Lm5indexS699;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2325
    = _M0FPB19string__from__bytes(_M0L6resultS698, 0, _M0L6_2atmpS1892);
    moonbit_decref_cycle_free(_M0L6resultS698);
    return _result_2325;
  } else {
    int32_t _M0L6_2atmpS1901 = _M0Lm3expS704;
    int32_t _M0L6_2atmpS1964;
    moonbit_string_t _result_2331;
    if (_M0L6_2atmpS1901 < 0) {
      int32_t _M0L6_2atmpS1902 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1904;
      int32_t _M0L6_2atmpS1903;
      int32_t _M0L6_2atmpS1905;
      int32_t _M0L1iS717;
      int32_t _M0L6_2atmpS1920;
      int32_t _M0L6_2atmpS1922;
      int32_t _M0L6_2atmpS1921;
      int32_t _M0L7currentS719;
      int32_t _M0L1iS720;
      uint64_t _M0L6outputS721;
      if (
        _M0L6_2atmpS1902 < 0
        || _M0L6_2atmpS1902 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1902] = 48;
      _M0L6_2atmpS1904 = _M0Lm5indexS699;
      _M0L6_2atmpS1903 = _M0L6_2atmpS1904 + 1;
      if (
        _M0L6_2atmpS1903 < 0
        || _M0L6_2atmpS1903 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1903] = 46;
      _M0L6_2atmpS1905 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1905 + 2;
      _M0L1iS717 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1906 = _M0Lm3expS704;
        if (_M0L1iS717 > _M0L6_2atmpS1906) {
          int32_t _M0L6_2atmpS1909 = _M0Lm5indexS699;
          int32_t _M0L6_2atmpS1908 = _M0L6_2atmpS1909 - _M0L1iS717;
          int32_t _M0L6_2atmpS1907 = _M0L6_2atmpS1908 - 1;
          int32_t _M0L6_2atmpS1910;
          if (
            _M0L6_2atmpS1907 < 0
            || _M0L6_2atmpS1907 >= Moonbit_array_length(_M0L6resultS698)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS698[_M0L6_2atmpS1907] = 48;
          _M0L6_2atmpS1910 = _M0L1iS717 - 1;
          _M0L1iS717 = _M0L6_2atmpS1910;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1920 = _M0Lm5indexS699;
      _M0L6_2atmpS1922 = _M0Lm3expS704;
      _M0L6_2atmpS1921 = -1 - _M0L6_2atmpS1922;
      _M0L7currentS719 = _M0L6_2atmpS1920 + _M0L6_2atmpS1921;
      _M0L1iS720 = 0;
      _M0L6outputS721 = _M0L6outputS701;
      while (1) {
        if (_M0L1iS720 < _M0L7olengthS703) {
          int32_t _M0L6_2atmpS1917 = _M0L7currentS719 + _M0L7olengthS703;
          int32_t _M0L6_2atmpS1916 = _M0L6_2atmpS1917 - _M0L1iS720;
          int32_t _M0L6_2atmpS1911 = _M0L6_2atmpS1916 - 1;
          uint64_t _M0L6_2atmpS1915 = _M0L6outputS721 % 10ull;
          int32_t _M0L6_2atmpS1914 = (int32_t)_M0L6_2atmpS1915;
          int32_t _M0L6_2atmpS1913 = 48 + _M0L6_2atmpS1914;
          int32_t _M0L6_2atmpS1912 = _M0L6_2atmpS1913 & 0xff;
          int32_t _M0L6_2atmpS1918;
          uint64_t _M0L6_2atmpS1919;
          if (
            _M0L6_2atmpS1911 < 0
            || _M0L6_2atmpS1911 >= Moonbit_array_length(_M0L6resultS698)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS698[_M0L6_2atmpS1911] = _M0L6_2atmpS1912;
          _M0L6_2atmpS1918 = _M0L1iS720 + 1;
          _M0L6_2atmpS1919 = _M0L6outputS721 / 10ull;
          _M0L1iS720 = _M0L6_2atmpS1918;
          _M0L6outputS721 = _M0L6_2atmpS1919;
          continue;
        }
        break;
      }
      _M0Lm5indexS699 = _M0L7currentS719 + _M0L7olengthS703;
    } else {
      int32_t _M0L6_2atmpS1924 = _M0Lm3expS704;
      int32_t _M0L6_2atmpS1923 = _M0L6_2atmpS1924 + 1;
      if (_M0L6_2atmpS1923 >= _M0L7olengthS703) {
        int32_t _M0L1iS723 = 0;
        uint64_t _M0L6outputS724 = _M0L6outputS701;
        int32_t _M0L6_2atmpS1935;
        int32_t _M0L6_2atmpS1940;
        int32_t _M0L7_2abindS726;
        int32_t _M0L1iS727;
        int32_t _M0L6_2atmpS1941;
        int32_t _M0L6_2atmpS1944;
        int32_t _M0L6_2atmpS1943;
        int32_t _M0L6_2atmpS1942;
        while (1) {
          if (_M0L1iS723 < _M0L7olengthS703) {
            int32_t _M0L6_2atmpS1932 = _M0Lm5indexS699;
            int32_t _M0L6_2atmpS1931 = _M0L6_2atmpS1932 + _M0L7olengthS703;
            int32_t _M0L6_2atmpS1930 = _M0L6_2atmpS1931 - _M0L1iS723;
            int32_t _M0L6_2atmpS1925 = _M0L6_2atmpS1930 - 1;
            uint64_t _M0L6_2atmpS1929 = _M0L6outputS724 % 10ull;
            int32_t _M0L6_2atmpS1928 = (int32_t)_M0L6_2atmpS1929;
            int32_t _M0L6_2atmpS1927 = 48 + _M0L6_2atmpS1928;
            int32_t _M0L6_2atmpS1926 = _M0L6_2atmpS1927 & 0xff;
            int32_t _M0L6_2atmpS1933;
            uint64_t _M0L6_2atmpS1934;
            if (
              _M0L6_2atmpS1925 < 0
              || _M0L6_2atmpS1925 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1925] = _M0L6_2atmpS1926;
            _M0L6_2atmpS1933 = _M0L1iS723 + 1;
            _M0L6_2atmpS1934 = _M0L6outputS724 / 10ull;
            _M0L1iS723 = _M0L6_2atmpS1933;
            _M0L6outputS724 = _M0L6_2atmpS1934;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1935 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1935 + _M0L7olengthS703;
        _M0L6_2atmpS1940 = _M0Lm3expS704;
        _M0L7_2abindS726 = _M0L6_2atmpS1940 + 1;
        _M0L1iS727 = _M0L7olengthS703;
        while (1) {
          if (_M0L1iS727 < _M0L7_2abindS726) {
            int32_t _M0L6_2atmpS1938 = _M0Lm5indexS699;
            int32_t _M0L6_2atmpS1937 = _M0L6_2atmpS1938 + _M0L1iS727;
            int32_t _M0L6_2atmpS1936 = _M0L6_2atmpS1937 - _M0L7olengthS703;
            int32_t _M0L6_2atmpS1939;
            if (
              _M0L6_2atmpS1936 < 0
              || _M0L6_2atmpS1936 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1936] = 48;
            _M0L6_2atmpS1939 = _M0L1iS727 + 1;
            _M0L1iS727 = _M0L6_2atmpS1939;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1941 = _M0Lm5indexS699;
        _M0L6_2atmpS1944 = _M0Lm3expS704;
        _M0L6_2atmpS1943 = _M0L6_2atmpS1944 + 1;
        _M0L6_2atmpS1942 = _M0L6_2atmpS1943 - _M0L7olengthS703;
        _M0Lm5indexS699 = _M0L6_2atmpS1941 + _M0L6_2atmpS1942;
      } else {
        int32_t _M0L6_2atmpS1961 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1960 = _M0L6_2atmpS1961 + 1;
        int32_t _M0L1iS729 = 0;
        int32_t _M0L7currentS730 = _M0L6_2atmpS1960;
        uint64_t _M0L6outputS731 = _M0L6outputS701;
        int32_t _M0L6_2atmpS1962;
        int32_t _M0L6_2atmpS1963;
        while (1) {
          if (_M0L1iS729 < _M0L7olengthS703) {
            int32_t _M0L6_2atmpS1956 = _M0L7olengthS703 - _M0L1iS729;
            int32_t _M0L6_2atmpS1954 = _M0L6_2atmpS1956 - 1;
            int32_t _M0L6_2atmpS1955 = _M0Lm3expS704;
            int32_t _M0L7currentS732;
            int32_t _M0L6_2atmpS1951;
            int32_t _M0L6_2atmpS1950;
            int32_t _M0L6_2atmpS1945;
            uint64_t _M0L6_2atmpS1949;
            int32_t _M0L6_2atmpS1948;
            int32_t _M0L6_2atmpS1947;
            int32_t _M0L6_2atmpS1946;
            int32_t _M0L6_2atmpS1952;
            uint64_t _M0L6_2atmpS1953;
            if (_M0L6_2atmpS1954 == _M0L6_2atmpS1955) {
              int32_t _M0L6_2atmpS1959 = _M0L7currentS730 + _M0L7olengthS703;
              int32_t _M0L6_2atmpS1958 = _M0L6_2atmpS1959 - _M0L1iS729;
              int32_t _M0L6_2atmpS1957 = _M0L6_2atmpS1958 - 1;
              if (
                _M0L6_2atmpS1957 < 0
                || _M0L6_2atmpS1957 >= Moonbit_array_length(_M0L6resultS698)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS698[_M0L6_2atmpS1957] = 46;
              _M0L7currentS732 = _M0L7currentS730 - 1;
            } else {
              _M0L7currentS732 = _M0L7currentS730;
            }
            _M0L6_2atmpS1951 = _M0L7currentS732 + _M0L7olengthS703;
            _M0L6_2atmpS1950 = _M0L6_2atmpS1951 - _M0L1iS729;
            _M0L6_2atmpS1945 = _M0L6_2atmpS1950 - 1;
            _M0L6_2atmpS1949 = _M0L6outputS731 % 10ull;
            _M0L6_2atmpS1948 = (int32_t)_M0L6_2atmpS1949;
            _M0L6_2atmpS1947 = 48 + _M0L6_2atmpS1948;
            _M0L6_2atmpS1946 = _M0L6_2atmpS1947 & 0xff;
            if (
              _M0L6_2atmpS1945 < 0
              || _M0L6_2atmpS1945 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1945] = _M0L6_2atmpS1946;
            _M0L6_2atmpS1952 = _M0L1iS729 + 1;
            _M0L6_2atmpS1953 = _M0L6outputS731 / 10ull;
            _M0L1iS729 = _M0L6_2atmpS1952;
            _M0L7currentS730 = _M0L7currentS732;
            _M0L6outputS731 = _M0L6_2atmpS1953;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1962 = _M0Lm5indexS699;
        _M0L6_2atmpS1963 = _M0L7olengthS703 + 1;
        _M0Lm5indexS699 = _M0L6_2atmpS1962 + _M0L6_2atmpS1963;
      }
    }
    _M0L6_2atmpS1964 = _M0Lm5indexS699;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2331
    = _M0FPB19string__from__bytes(_M0L6resultS698, 0, _M0L6_2atmpS1964);
    moonbit_decref_cycle_free(_M0L6resultS698);
    return _result_2331;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS644,
  uint32_t _M0L12ieeeExponentS643
) {
  int32_t _M0Lm2e2S641;
  uint64_t _M0Lm2m2S642;
  uint64_t _M0L6_2atmpS1838;
  uint64_t _M0L6_2atmpS1837;
  int32_t _M0L4evenS645;
  uint64_t _M0L6_2atmpS1836;
  uint64_t _M0L2mvS646;
  int32_t _M0L7mmShiftS647;
  uint64_t _M0Lm2vrS648;
  uint64_t _M0Lm2vpS649;
  uint64_t _M0Lm2vmS650;
  int32_t _M0Lm3e10S651;
  int32_t _M0Lm17vmIsTrailingZerosS652;
  int32_t _M0Lm17vrIsTrailingZerosS653;
  int32_t _M0L6_2atmpS1738;
  int32_t _M0Lm7removedS672;
  int32_t _M0Lm16lastRemovedDigitS673;
  uint64_t _M0Lm6outputS674;
  int32_t _M0L6_2atmpS1834;
  int32_t _M0L6_2atmpS1835;
  int32_t _M0L3expS697;
  uint64_t _M0L6_2atmpS1833;
  struct _M0TPB17FloatingDecimal64* _block_2337;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S641 = 0;
  _M0Lm2m2S642 = 0ull;
  if (_M0L12ieeeExponentS643 == 0u) {
    _M0Lm2e2S641 = -1076;
    _M0Lm2m2S642 = _M0L12ieeeMantissaS644;
  } else {
    int32_t _M0L6_2atmpS1737 = *(int32_t*)&_M0L12ieeeExponentS643;
    int32_t _M0L6_2atmpS1736 = _M0L6_2atmpS1737 - 1023;
    int32_t _M0L6_2atmpS1735 = _M0L6_2atmpS1736 - 52;
    _M0Lm2e2S641 = _M0L6_2atmpS1735 - 2;
    _M0Lm2m2S642 = 4503599627370496ull | _M0L12ieeeMantissaS644;
  }
  _M0L6_2atmpS1838 = _M0Lm2m2S642;
  _M0L6_2atmpS1837 = _M0L6_2atmpS1838 & 1ull;
  _M0L4evenS645 = _M0L6_2atmpS1837 == 0ull;
  _M0L6_2atmpS1836 = _M0Lm2m2S642;
  _M0L2mvS646 = 4ull * _M0L6_2atmpS1836;
  _M0L7mmShiftS647
  = _M0L12ieeeMantissaS644 != 0ull || _M0L12ieeeExponentS643 <= 1u;
  _M0Lm2vrS648 = 0ull;
  _M0Lm2vpS649 = 0ull;
  _M0Lm2vmS650 = 0ull;
  _M0Lm3e10S651 = 0;
  _M0Lm17vmIsTrailingZerosS652 = 0;
  _M0Lm17vrIsTrailingZerosS653 = 0;
  _M0L6_2atmpS1738 = _M0Lm2e2S641;
  if (_M0L6_2atmpS1738 >= 0) {
    int32_t _M0L6_2atmpS1760 = _M0Lm2e2S641;
    int32_t _M0L6_2atmpS1756;
    int32_t _M0L6_2atmpS1759;
    int32_t _M0L6_2atmpS1758;
    int32_t _M0L6_2atmpS1757;
    int32_t _M0L1qS654;
    int32_t _M0L6_2atmpS1755;
    int32_t _M0L6_2atmpS1754;
    int32_t _M0L1kS655;
    int32_t _M0L6_2atmpS1753;
    int32_t _M0L6_2atmpS1752;
    int32_t _M0L6_2atmpS1751;
    int32_t _M0L1iS656;
    struct _M0TPB8Pow5Pair _M0L4pow5S657;
    uint64_t _M0L6_2atmpS1750;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS658;
    uint64_t _M0L8_2avrOutS659;
    uint64_t _M0L8_2avpOutS660;
    uint64_t _M0L8_2avmOutS661;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1756 = _M0FPB9log10Pow2(_M0L6_2atmpS1760);
    _M0L6_2atmpS1759 = _M0Lm2e2S641;
    _M0L6_2atmpS1758 = _M0L6_2atmpS1759 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1757 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1758);
    _M0L1qS654 = _M0L6_2atmpS1756 - _M0L6_2atmpS1757;
    _M0Lm3e10S651 = _M0L1qS654;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1755 = _M0FPB8pow5bits(_M0L1qS654);
    _M0L6_2atmpS1754 = 125 + _M0L6_2atmpS1755;
    _M0L1kS655 = _M0L6_2atmpS1754 - 1;
    _M0L6_2atmpS1753 = _M0Lm2e2S641;
    _M0L6_2atmpS1752 = -_M0L6_2atmpS1753;
    _M0L6_2atmpS1751 = _M0L6_2atmpS1752 + _M0L1qS654;
    _M0L1iS656 = _M0L6_2atmpS1751 + _M0L1kS655;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S657 = _M0FPB22double__computeInvPow5(_M0L1qS654);
    _M0L6_2atmpS1750 = _M0Lm2m2S642;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS658
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1750, _M0L4pow5S657, _M0L1iS656, _M0L7mmShiftS647);
    _M0L8_2avrOutS659 = _M0L7_2abindS658.$0;
    _M0L8_2avpOutS660 = _M0L7_2abindS658.$1;
    _M0L8_2avmOutS661 = _M0L7_2abindS658.$2;
    _M0Lm2vrS648 = _M0L8_2avrOutS659;
    _M0Lm2vpS649 = _M0L8_2avpOutS660;
    _M0Lm2vmS650 = _M0L8_2avmOutS661;
    if (_M0L1qS654 <= 21) {
      int32_t _M0L6_2atmpS1746 = (int32_t)_M0L2mvS646;
      uint64_t _M0L6_2atmpS1749 = _M0L2mvS646 / 5ull;
      int32_t _M0L6_2atmpS1748 = (int32_t)_M0L6_2atmpS1749;
      int32_t _M0L6_2atmpS1747 = 5 * _M0L6_2atmpS1748;
      int32_t _M0L6mvMod5S662 = _M0L6_2atmpS1746 - _M0L6_2atmpS1747;
      if (_M0L6mvMod5S662 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS653
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS646, _M0L1qS654);
      } else if (_M0L4evenS645) {
        uint64_t _M0L6_2atmpS1740 = _M0L2mvS646 - 1ull;
        uint64_t _M0L6_2atmpS1741;
        uint64_t _M0L6_2atmpS1739;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1741 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS647);
        _M0L6_2atmpS1739 = _M0L6_2atmpS1740 - _M0L6_2atmpS1741;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS652
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1739, _M0L1qS654);
      } else {
        uint64_t _M0L6_2atmpS1742 = _M0Lm2vpS649;
        uint64_t _M0L6_2atmpS1745 = _M0L2mvS646 + 2ull;
        int32_t _M0L6_2atmpS1744;
        uint64_t _M0L6_2atmpS1743;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1744
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1745, _M0L1qS654);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1743 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1744);
        _M0Lm2vpS649 = _M0L6_2atmpS1742 - _M0L6_2atmpS1743;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1774 = _M0Lm2e2S641;
    int32_t _M0L6_2atmpS1773 = -_M0L6_2atmpS1774;
    int32_t _M0L6_2atmpS1768;
    int32_t _M0L6_2atmpS1772;
    int32_t _M0L6_2atmpS1771;
    int32_t _M0L6_2atmpS1770;
    int32_t _M0L6_2atmpS1769;
    int32_t _M0L1qS663;
    int32_t _M0L6_2atmpS1761;
    int32_t _M0L6_2atmpS1767;
    int32_t _M0L6_2atmpS1766;
    int32_t _M0L1iS664;
    int32_t _M0L6_2atmpS1765;
    int32_t _M0L1kS665;
    int32_t _M0L1jS666;
    struct _M0TPB8Pow5Pair _M0L4pow5S667;
    uint64_t _M0L6_2atmpS1764;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS668;
    uint64_t _M0L8_2avrOutS669;
    uint64_t _M0L8_2avpOutS670;
    uint64_t _M0L8_2avmOutS671;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1768 = _M0FPB9log10Pow5(_M0L6_2atmpS1773);
    _M0L6_2atmpS1772 = _M0Lm2e2S641;
    _M0L6_2atmpS1771 = -_M0L6_2atmpS1772;
    _M0L6_2atmpS1770 = _M0L6_2atmpS1771 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1769 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1770);
    _M0L1qS663 = _M0L6_2atmpS1768 - _M0L6_2atmpS1769;
    _M0L6_2atmpS1761 = _M0Lm2e2S641;
    _M0Lm3e10S651 = _M0L1qS663 + _M0L6_2atmpS1761;
    _M0L6_2atmpS1767 = _M0Lm2e2S641;
    _M0L6_2atmpS1766 = -_M0L6_2atmpS1767;
    _M0L1iS664 = _M0L6_2atmpS1766 - _M0L1qS663;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1765 = _M0FPB8pow5bits(_M0L1iS664);
    _M0L1kS665 = _M0L6_2atmpS1765 - 125;
    _M0L1jS666 = _M0L1qS663 - _M0L1kS665;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S667 = _M0FPB19double__computePow5(_M0L1iS664);
    _M0L6_2atmpS1764 = _M0Lm2m2S642;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS668
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1764, _M0L4pow5S667, _M0L1jS666, _M0L7mmShiftS647);
    _M0L8_2avrOutS669 = _M0L7_2abindS668.$0;
    _M0L8_2avpOutS670 = _M0L7_2abindS668.$1;
    _M0L8_2avmOutS671 = _M0L7_2abindS668.$2;
    _M0Lm2vrS648 = _M0L8_2avrOutS669;
    _M0Lm2vpS649 = _M0L8_2avpOutS670;
    _M0Lm2vmS650 = _M0L8_2avmOutS671;
    if (_M0L1qS663 <= 1) {
      _M0Lm17vrIsTrailingZerosS653 = 1;
      if (_M0L4evenS645) {
        int32_t _M0L6_2atmpS1762;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1762 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS647);
        _M0Lm17vmIsTrailingZerosS652 = _M0L6_2atmpS1762 == 1;
      } else {
        uint64_t _M0L6_2atmpS1763 = _M0Lm2vpS649;
        _M0Lm2vpS649 = _M0L6_2atmpS1763 - 1ull;
      }
    } else if (_M0L1qS663 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS653
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS646, _M0L1qS663);
    }
  }
  _M0Lm7removedS672 = 0;
  _M0Lm16lastRemovedDigitS673 = 0;
  _M0Lm6outputS674 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS652 || _M0Lm17vrIsTrailingZerosS653) {
    int32_t _if__result_2334;
    uint64_t _M0L6_2atmpS1804;
    uint64_t _M0L6_2atmpS1810;
    uint64_t _M0L6_2atmpS1811;
    int32_t _if__result_2335;
    int32_t _M0L6_2atmpS1807;
    int64_t _M0L6_2atmpS1806;
    uint64_t _M0L6_2atmpS1805;
    while (1) {
      uint64_t _M0L6_2atmpS1787 = _M0Lm2vpS649;
      uint64_t _M0L7vpDiv10S675 = _M0L6_2atmpS1787 / 10ull;
      uint64_t _M0L6_2atmpS1786 = _M0Lm2vmS650;
      uint64_t _M0L7vmDiv10S676 = _M0L6_2atmpS1786 / 10ull;
      uint64_t _M0L6_2atmpS1785;
      int32_t _M0L6_2atmpS1782;
      int32_t _M0L6_2atmpS1784;
      int32_t _M0L6_2atmpS1783;
      int32_t _M0L7vmMod10S678;
      uint64_t _M0L6_2atmpS1781;
      uint64_t _M0L7vrDiv10S679;
      uint64_t _M0L6_2atmpS1780;
      int32_t _M0L6_2atmpS1777;
      int32_t _M0L6_2atmpS1779;
      int32_t _M0L6_2atmpS1778;
      int32_t _M0L7vrMod10S680;
      int32_t _M0L6_2atmpS1776;
      if (_M0L7vpDiv10S675 <= _M0L7vmDiv10S676) {
        break;
      }
      _M0L6_2atmpS1785 = _M0Lm2vmS650;
      _M0L6_2atmpS1782 = (int32_t)_M0L6_2atmpS1785;
      _M0L6_2atmpS1784 = (int32_t)_M0L7vmDiv10S676;
      _M0L6_2atmpS1783 = 10 * _M0L6_2atmpS1784;
      _M0L7vmMod10S678 = _M0L6_2atmpS1782 - _M0L6_2atmpS1783;
      _M0L6_2atmpS1781 = _M0Lm2vrS648;
      _M0L7vrDiv10S679 = _M0L6_2atmpS1781 / 10ull;
      _M0L6_2atmpS1780 = _M0Lm2vrS648;
      _M0L6_2atmpS1777 = (int32_t)_M0L6_2atmpS1780;
      _M0L6_2atmpS1779 = (int32_t)_M0L7vrDiv10S679;
      _M0L6_2atmpS1778 = 10 * _M0L6_2atmpS1779;
      _M0L7vrMod10S680 = _M0L6_2atmpS1777 - _M0L6_2atmpS1778;
      _M0Lm17vmIsTrailingZerosS652
      = _M0Lm17vmIsTrailingZerosS652 && _M0L7vmMod10S678 == 0;
      if (_M0Lm17vrIsTrailingZerosS653) {
        int32_t _M0L6_2atmpS1775 = _M0Lm16lastRemovedDigitS673;
        _M0Lm17vrIsTrailingZerosS653 = _M0L6_2atmpS1775 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS653 = 0;
      }
      _M0Lm16lastRemovedDigitS673 = _M0L7vrMod10S680;
      _M0Lm2vrS648 = _M0L7vrDiv10S679;
      _M0Lm2vpS649 = _M0L7vpDiv10S675;
      _M0Lm2vmS650 = _M0L7vmDiv10S676;
      _M0L6_2atmpS1776 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1776 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS652) {
      while (1) {
        uint64_t _M0L6_2atmpS1800 = _M0Lm2vmS650;
        uint64_t _M0L7vmDiv10S681 = _M0L6_2atmpS1800 / 10ull;
        uint64_t _M0L6_2atmpS1799 = _M0Lm2vmS650;
        int32_t _M0L6_2atmpS1796 = (int32_t)_M0L6_2atmpS1799;
        int32_t _M0L6_2atmpS1798 = (int32_t)_M0L7vmDiv10S681;
        int32_t _M0L6_2atmpS1797 = 10 * _M0L6_2atmpS1798;
        int32_t _M0L7vmMod10S682 = _M0L6_2atmpS1796 - _M0L6_2atmpS1797;
        uint64_t _M0L6_2atmpS1795;
        uint64_t _M0L7vpDiv10S684;
        uint64_t _M0L6_2atmpS1794;
        uint64_t _M0L7vrDiv10S685;
        uint64_t _M0L6_2atmpS1793;
        int32_t _M0L6_2atmpS1790;
        int32_t _M0L6_2atmpS1792;
        int32_t _M0L6_2atmpS1791;
        int32_t _M0L7vrMod10S686;
        int32_t _M0L6_2atmpS1789;
        if (_M0L7vmMod10S682 != 0) {
          break;
        }
        _M0L6_2atmpS1795 = _M0Lm2vpS649;
        _M0L7vpDiv10S684 = _M0L6_2atmpS1795 / 10ull;
        _M0L6_2atmpS1794 = _M0Lm2vrS648;
        _M0L7vrDiv10S685 = _M0L6_2atmpS1794 / 10ull;
        _M0L6_2atmpS1793 = _M0Lm2vrS648;
        _M0L6_2atmpS1790 = (int32_t)_M0L6_2atmpS1793;
        _M0L6_2atmpS1792 = (int32_t)_M0L7vrDiv10S685;
        _M0L6_2atmpS1791 = 10 * _M0L6_2atmpS1792;
        _M0L7vrMod10S686 = _M0L6_2atmpS1790 - _M0L6_2atmpS1791;
        if (_M0Lm17vrIsTrailingZerosS653) {
          int32_t _M0L6_2atmpS1788 = _M0Lm16lastRemovedDigitS673;
          _M0Lm17vrIsTrailingZerosS653 = _M0L6_2atmpS1788 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS653 = 0;
        }
        _M0Lm16lastRemovedDigitS673 = _M0L7vrMod10S686;
        _M0Lm2vrS648 = _M0L7vrDiv10S685;
        _M0Lm2vpS649 = _M0L7vpDiv10S684;
        _M0Lm2vmS650 = _M0L7vmDiv10S681;
        _M0L6_2atmpS1789 = _M0Lm7removedS672;
        _M0Lm7removedS672 = _M0L6_2atmpS1789 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS653) {
      int32_t _M0L6_2atmpS1803 = _M0Lm16lastRemovedDigitS673;
      if (_M0L6_2atmpS1803 == 5) {
        uint64_t _M0L6_2atmpS1802 = _M0Lm2vrS648;
        uint64_t _M0L6_2atmpS1801 = _M0L6_2atmpS1802 % 2ull;
        _if__result_2334 = _M0L6_2atmpS1801 == 0ull;
      } else {
        _if__result_2334 = 0;
      }
    } else {
      _if__result_2334 = 0;
    }
    if (_if__result_2334) {
      _M0Lm16lastRemovedDigitS673 = 4;
    }
    _M0L6_2atmpS1804 = _M0Lm2vrS648;
    _M0L6_2atmpS1810 = _M0Lm2vrS648;
    _M0L6_2atmpS1811 = _M0Lm2vmS650;
    if (_M0L6_2atmpS1810 == _M0L6_2atmpS1811) {
      if (!_M0L4evenS645) {
        _if__result_2335 = 1;
      } else {
        int32_t _M0L6_2atmpS1809 = _M0Lm17vmIsTrailingZerosS652;
        _if__result_2335 = !_M0L6_2atmpS1809;
      }
    } else {
      _if__result_2335 = 0;
    }
    if (_if__result_2335) {
      _M0L6_2atmpS1807 = 1;
    } else {
      int32_t _M0L6_2atmpS1808 = _M0Lm16lastRemovedDigitS673;
      _M0L6_2atmpS1807 = _M0L6_2atmpS1808 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1806 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1807);
    _M0L6_2atmpS1805 = *(uint64_t*)&_M0L6_2atmpS1806;
    _M0Lm6outputS674 = _M0L6_2atmpS1804 + _M0L6_2atmpS1805;
  } else {
    int32_t _M0Lm7roundUpS687 = 0;
    uint64_t _M0L6_2atmpS1832 = _M0Lm2vpS649;
    uint64_t _M0L8vpDiv100S688 = _M0L6_2atmpS1832 / 100ull;
    uint64_t _M0L6_2atmpS1831 = _M0Lm2vmS650;
    uint64_t _M0L8vmDiv100S689 = _M0L6_2atmpS1831 / 100ull;
    uint64_t _M0L6_2atmpS1826;
    uint64_t _M0L6_2atmpS1829;
    uint64_t _M0L6_2atmpS1830;
    int32_t _M0L6_2atmpS1828;
    uint64_t _M0L6_2atmpS1827;
    if (_M0L8vpDiv100S688 > _M0L8vmDiv100S689) {
      uint64_t _M0L6_2atmpS1817 = _M0Lm2vrS648;
      uint64_t _M0L8vrDiv100S690 = _M0L6_2atmpS1817 / 100ull;
      uint64_t _M0L6_2atmpS1816 = _M0Lm2vrS648;
      int32_t _M0L6_2atmpS1813 = (int32_t)_M0L6_2atmpS1816;
      int32_t _M0L6_2atmpS1815 = (int32_t)_M0L8vrDiv100S690;
      int32_t _M0L6_2atmpS1814 = 100 * _M0L6_2atmpS1815;
      int32_t _M0L8vrMod100S691 = _M0L6_2atmpS1813 - _M0L6_2atmpS1814;
      int32_t _M0L6_2atmpS1812;
      _M0Lm7roundUpS687 = _M0L8vrMod100S691 >= 50;
      _M0Lm2vrS648 = _M0L8vrDiv100S690;
      _M0Lm2vpS649 = _M0L8vpDiv100S688;
      _M0Lm2vmS650 = _M0L8vmDiv100S689;
      _M0L6_2atmpS1812 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1812 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1825 = _M0Lm2vpS649;
      uint64_t _M0L7vpDiv10S692 = _M0L6_2atmpS1825 / 10ull;
      uint64_t _M0L6_2atmpS1824 = _M0Lm2vmS650;
      uint64_t _M0L7vmDiv10S693 = _M0L6_2atmpS1824 / 10ull;
      uint64_t _M0L6_2atmpS1823;
      uint64_t _M0L7vrDiv10S695;
      uint64_t _M0L6_2atmpS1822;
      int32_t _M0L6_2atmpS1819;
      int32_t _M0L6_2atmpS1821;
      int32_t _M0L6_2atmpS1820;
      int32_t _M0L7vrMod10S696;
      int32_t _M0L6_2atmpS1818;
      if (_M0L7vpDiv10S692 <= _M0L7vmDiv10S693) {
        break;
      }
      _M0L6_2atmpS1823 = _M0Lm2vrS648;
      _M0L7vrDiv10S695 = _M0L6_2atmpS1823 / 10ull;
      _M0L6_2atmpS1822 = _M0Lm2vrS648;
      _M0L6_2atmpS1819 = (int32_t)_M0L6_2atmpS1822;
      _M0L6_2atmpS1821 = (int32_t)_M0L7vrDiv10S695;
      _M0L6_2atmpS1820 = 10 * _M0L6_2atmpS1821;
      _M0L7vrMod10S696 = _M0L6_2atmpS1819 - _M0L6_2atmpS1820;
      _M0Lm7roundUpS687 = _M0L7vrMod10S696 >= 5;
      _M0Lm2vrS648 = _M0L7vrDiv10S695;
      _M0Lm2vpS649 = _M0L7vpDiv10S692;
      _M0Lm2vmS650 = _M0L7vmDiv10S693;
      _M0L6_2atmpS1818 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1818 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1826 = _M0Lm2vrS648;
    _M0L6_2atmpS1829 = _M0Lm2vrS648;
    _M0L6_2atmpS1830 = _M0Lm2vmS650;
    _M0L6_2atmpS1828
    = _M0L6_2atmpS1829 == _M0L6_2atmpS1830 || _M0Lm7roundUpS687;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1827 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1828);
    _M0Lm6outputS674 = _M0L6_2atmpS1826 + _M0L6_2atmpS1827;
  }
  _M0L6_2atmpS1834 = _M0Lm3e10S651;
  _M0L6_2atmpS1835 = _M0Lm7removedS672;
  _M0L3expS697 = _M0L6_2atmpS1834 + _M0L6_2atmpS1835;
  _M0L6_2atmpS1833 = _M0Lm6outputS674;
  _block_2337
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2337)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2337->$0 = _M0L6_2atmpS1833;
  _block_2337->$1 = _M0L3expS697;
  return _block_2337;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS640) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS640) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS639) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS639) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS638) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS638) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS637) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS637 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS637 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS637 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS637 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS637 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS637 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS637 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS637 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS637 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS637 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS637 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS637 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS637 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS637 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS637 >= 100ull) {
    return 3;
  }
  if (_M0L1vS637 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS620) {
  int32_t _M0L6_2atmpS1734;
  int32_t _M0L6_2atmpS1733;
  int32_t _M0L4baseS619;
  int32_t _M0L5base2S621;
  int32_t _M0L6offsetS622;
  int32_t _M0L6_2atmpS1732;
  uint64_t _M0L4mul0S623;
  int32_t _M0L6_2atmpS1731;
  int32_t _M0L6_2atmpS1730;
  uint64_t _M0L4mul1S624;
  uint64_t _M0L1mS625;
  struct _M0TPB7Umul128 _M0L7_2abindS626;
  uint64_t _M0L7_2alow1S627;
  uint64_t _M0L8_2ahigh1S628;
  struct _M0TPB7Umul128 _M0L7_2abindS629;
  uint64_t _M0L7_2alow0S630;
  uint64_t _M0L8_2ahigh0S631;
  uint64_t _M0L3sumS632;
  uint64_t _M0Lm5high1S633;
  int32_t _M0L6_2atmpS1728;
  int32_t _M0L6_2atmpS1729;
  int32_t _M0L5deltaS634;
  uint64_t _M0L6_2atmpS1727;
  uint64_t _M0L6_2atmpS1719;
  int32_t _M0L6_2atmpS1726;
  uint32_t _M0L6_2atmpS1723;
  int32_t _M0L6_2atmpS1725;
  int32_t _M0L6_2atmpS1724;
  uint32_t _M0L6_2atmpS1722;
  uint32_t _M0L6_2atmpS1721;
  uint64_t _M0L6_2atmpS1720;
  uint64_t _M0L1aS635;
  uint64_t _M0L6_2atmpS1718;
  uint64_t _M0L1bS636;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1734 = _M0L1iS620 + 26;
  _M0L6_2atmpS1733 = _M0L6_2atmpS1734 - 1;
  _M0L4baseS619 = _M0L6_2atmpS1733 / 26;
  _M0L5base2S621 = _M0L4baseS619 * 26;
  _M0L6offsetS622 = _M0L5base2S621 - _M0L1iS620;
  _M0L6_2atmpS1732 = _M0L4baseS619 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1732);
  _M0L6_2atmpS1731 = _M0L4baseS619 * 2;
  _M0L6_2atmpS1730 = _M0L6_2atmpS1731 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S624
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1730);
  if (_M0L6offsetS622 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S623, .$1 = _M0L4mul1S624};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS622);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS626 = _M0FPB7umul128(_M0L1mS625, _M0L4mul1S624);
  _M0L7_2alow1S627 = _M0L7_2abindS626.$0;
  _M0L8_2ahigh1S628 = _M0L7_2abindS626.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS629 = _M0FPB7umul128(_M0L1mS625, _M0L4mul0S623);
  _M0L7_2alow0S630 = _M0L7_2abindS629.$0;
  _M0L8_2ahigh0S631 = _M0L7_2abindS629.$1;
  _M0L3sumS632 = _M0L8_2ahigh0S631 + _M0L7_2alow1S627;
  _M0Lm5high1S633 = _M0L8_2ahigh1S628;
  if (_M0L3sumS632 < _M0L8_2ahigh0S631) {
    uint64_t _M0L6_2atmpS1717 = _M0Lm5high1S633;
    _M0Lm5high1S633 = _M0L6_2atmpS1717 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1728 = _M0FPB8pow5bits(_M0L5base2S621);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1729 = _M0FPB8pow5bits(_M0L1iS620);
  _M0L5deltaS634 = _M0L6_2atmpS1728 - _M0L6_2atmpS1729;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1727
  = _M0FPB13shiftright128(_M0L7_2alow0S630, _M0L3sumS632, _M0L5deltaS634);
  _M0L6_2atmpS1719 = _M0L6_2atmpS1727 + 1ull;
  _M0L6_2atmpS1726 = _M0L1iS620 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1723
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1726);
  _M0L6_2atmpS1725 = _M0L1iS620 % 16;
  _M0L6_2atmpS1724 = _M0L6_2atmpS1725 << 1;
  _M0L6_2atmpS1722 = _M0L6_2atmpS1723 >> (_M0L6_2atmpS1724 & 31);
  _M0L6_2atmpS1721 = _M0L6_2atmpS1722 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1720 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1721);
  _M0L1aS635 = _M0L6_2atmpS1719 + _M0L6_2atmpS1720;
  _M0L6_2atmpS1718 = _M0Lm5high1S633;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS636
  = _M0FPB13shiftright128(_M0L3sumS632, _M0L6_2atmpS1718, _M0L5deltaS634);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS635, .$1 = _M0L1bS636};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS602) {
  int32_t _M0L4baseS601;
  int32_t _M0L5base2S603;
  int32_t _M0L6offsetS604;
  int32_t _M0L6_2atmpS1716;
  uint64_t _M0L4mul0S605;
  int32_t _M0L6_2atmpS1715;
  int32_t _M0L6_2atmpS1714;
  uint64_t _M0L4mul1S606;
  uint64_t _M0L1mS607;
  struct _M0TPB7Umul128 _M0L7_2abindS608;
  uint64_t _M0L7_2alow1S609;
  uint64_t _M0L8_2ahigh1S610;
  struct _M0TPB7Umul128 _M0L7_2abindS611;
  uint64_t _M0L7_2alow0S612;
  uint64_t _M0L8_2ahigh0S613;
  uint64_t _M0L3sumS614;
  uint64_t _M0Lm5high1S615;
  int32_t _M0L6_2atmpS1712;
  int32_t _M0L6_2atmpS1713;
  int32_t _M0L5deltaS616;
  uint64_t _M0L6_2atmpS1704;
  int32_t _M0L6_2atmpS1711;
  uint32_t _M0L6_2atmpS1708;
  int32_t _M0L6_2atmpS1710;
  int32_t _M0L6_2atmpS1709;
  uint32_t _M0L6_2atmpS1707;
  uint32_t _M0L6_2atmpS1706;
  uint64_t _M0L6_2atmpS1705;
  uint64_t _M0L1aS617;
  uint64_t _M0L6_2atmpS1703;
  uint64_t _M0L1bS618;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS601 = _M0L1iS602 / 26;
  _M0L5base2S603 = _M0L4baseS601 * 26;
  _M0L6offsetS604 = _M0L1iS602 - _M0L5base2S603;
  _M0L6_2atmpS1716 = _M0L4baseS601 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1716);
  _M0L6_2atmpS1715 = _M0L4baseS601 * 2;
  _M0L6_2atmpS1714 = _M0L6_2atmpS1715 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S606
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1714);
  if (_M0L6offsetS604 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S605, .$1 = _M0L4mul1S606};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS604);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS608 = _M0FPB7umul128(_M0L1mS607, _M0L4mul1S606);
  _M0L7_2alow1S609 = _M0L7_2abindS608.$0;
  _M0L8_2ahigh1S610 = _M0L7_2abindS608.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS611 = _M0FPB7umul128(_M0L1mS607, _M0L4mul0S605);
  _M0L7_2alow0S612 = _M0L7_2abindS611.$0;
  _M0L8_2ahigh0S613 = _M0L7_2abindS611.$1;
  _M0L3sumS614 = _M0L8_2ahigh0S613 + _M0L7_2alow1S609;
  _M0Lm5high1S615 = _M0L8_2ahigh1S610;
  if (_M0L3sumS614 < _M0L8_2ahigh0S613) {
    uint64_t _M0L6_2atmpS1702 = _M0Lm5high1S615;
    _M0Lm5high1S615 = _M0L6_2atmpS1702 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1712 = _M0FPB8pow5bits(_M0L1iS602);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1713 = _M0FPB8pow5bits(_M0L5base2S603);
  _M0L5deltaS616 = _M0L6_2atmpS1712 - _M0L6_2atmpS1713;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1704
  = _M0FPB13shiftright128(_M0L7_2alow0S612, _M0L3sumS614, _M0L5deltaS616);
  _M0L6_2atmpS1711 = _M0L1iS602 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1708
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1711);
  _M0L6_2atmpS1710 = _M0L1iS602 % 16;
  _M0L6_2atmpS1709 = _M0L6_2atmpS1710 << 1;
  _M0L6_2atmpS1707 = _M0L6_2atmpS1708 >> (_M0L6_2atmpS1709 & 31);
  _M0L6_2atmpS1706 = _M0L6_2atmpS1707 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1705 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1706);
  _M0L1aS617 = _M0L6_2atmpS1704 + _M0L6_2atmpS1705;
  _M0L6_2atmpS1703 = _M0Lm5high1S615;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS618
  = _M0FPB13shiftright128(_M0L3sumS614, _M0L6_2atmpS1703, _M0L5deltaS616);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS617, .$1 = _M0L1bS618};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS575,
  struct _M0TPB8Pow5Pair _M0L3mulS572,
  int32_t _M0L1jS588,
  int32_t _M0L7mmShiftS590
) {
  uint64_t _M0L7_2amul0S571;
  uint64_t _M0L7_2amul1S573;
  uint64_t _M0L1mS574;
  struct _M0TPB7Umul128 _M0L7_2abindS576;
  uint64_t _M0L5_2aloS577;
  uint64_t _M0L6_2atmpS578;
  struct _M0TPB7Umul128 _M0L7_2abindS579;
  uint64_t _M0L6_2alo2S580;
  uint64_t _M0L6_2ahi2S581;
  uint64_t _M0L3midS582;
  uint64_t _M0L6_2atmpS1701;
  uint64_t _M0L2hiS583;
  uint64_t _M0L3lo2S584;
  uint64_t _M0L6_2atmpS1699;
  uint64_t _M0L6_2atmpS1700;
  uint64_t _M0L4mid2S585;
  uint64_t _M0L6_2atmpS1698;
  uint64_t _M0L3hi2S586;
  int32_t _M0L6_2atmpS1697;
  int32_t _M0L6_2atmpS1696;
  uint64_t _M0L2vpS587;
  uint64_t _M0Lm2vmS589;
  int32_t _M0L6_2atmpS1695;
  int32_t _M0L6_2atmpS1694;
  uint64_t _M0L2vrS600;
  uint64_t _M0L6_2atmpS1693;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S571 = _M0L3mulS572.$0;
  _M0L7_2amul1S573 = _M0L3mulS572.$1;
  _M0L1mS574 = _M0L1mS575 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS576 = _M0FPB7umul128(_M0L1mS574, _M0L7_2amul0S571);
  _M0L5_2aloS577 = _M0L7_2abindS576.$0;
  _M0L6_2atmpS578 = _M0L7_2abindS576.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS579 = _M0FPB7umul128(_M0L1mS574, _M0L7_2amul1S573);
  _M0L6_2alo2S580 = _M0L7_2abindS579.$0;
  _M0L6_2ahi2S581 = _M0L7_2abindS579.$1;
  _M0L3midS582 = _M0L6_2atmpS578 + _M0L6_2alo2S580;
  if (_M0L3midS582 < _M0L6_2atmpS578) {
    _M0L6_2atmpS1701 = 1ull;
  } else {
    _M0L6_2atmpS1701 = 0ull;
  }
  _M0L2hiS583 = _M0L6_2ahi2S581 + _M0L6_2atmpS1701;
  _M0L3lo2S584 = _M0L5_2aloS577 + _M0L7_2amul0S571;
  _M0L6_2atmpS1699 = _M0L3midS582 + _M0L7_2amul1S573;
  if (_M0L3lo2S584 < _M0L5_2aloS577) {
    _M0L6_2atmpS1700 = 1ull;
  } else {
    _M0L6_2atmpS1700 = 0ull;
  }
  _M0L4mid2S585 = _M0L6_2atmpS1699 + _M0L6_2atmpS1700;
  if (_M0L4mid2S585 < _M0L3midS582) {
    _M0L6_2atmpS1698 = 1ull;
  } else {
    _M0L6_2atmpS1698 = 0ull;
  }
  _M0L3hi2S586 = _M0L2hiS583 + _M0L6_2atmpS1698;
  _M0L6_2atmpS1697 = _M0L1jS588 - 64;
  _M0L6_2atmpS1696 = _M0L6_2atmpS1697 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS587
  = _M0FPB13shiftright128(_M0L4mid2S585, _M0L3hi2S586, _M0L6_2atmpS1696);
  _M0Lm2vmS589 = 0ull;
  if (_M0L7mmShiftS590) {
    uint64_t _M0L3lo3S591 = _M0L5_2aloS577 - _M0L7_2amul0S571;
    uint64_t _M0L6_2atmpS1683 = _M0L3midS582 - _M0L7_2amul1S573;
    uint64_t _M0L6_2atmpS1684;
    uint64_t _M0L4mid3S592;
    uint64_t _M0L6_2atmpS1682;
    uint64_t _M0L3hi3S593;
    int32_t _M0L6_2atmpS1681;
    int32_t _M0L6_2atmpS1680;
    if (_M0L5_2aloS577 < _M0L3lo3S591) {
      _M0L6_2atmpS1684 = 1ull;
    } else {
      _M0L6_2atmpS1684 = 0ull;
    }
    _M0L4mid3S592 = _M0L6_2atmpS1683 - _M0L6_2atmpS1684;
    if (_M0L3midS582 < _M0L4mid3S592) {
      _M0L6_2atmpS1682 = 1ull;
    } else {
      _M0L6_2atmpS1682 = 0ull;
    }
    _M0L3hi3S593 = _M0L2hiS583 - _M0L6_2atmpS1682;
    _M0L6_2atmpS1681 = _M0L1jS588 - 64;
    _M0L6_2atmpS1680 = _M0L6_2atmpS1681 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS589
    = _M0FPB13shiftright128(_M0L4mid3S592, _M0L3hi3S593, _M0L6_2atmpS1680);
  } else {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS577 + _M0L5_2aloS577;
    uint64_t _M0L6_2atmpS1691 = _M0L3midS582 + _M0L3midS582;
    uint64_t _M0L6_2atmpS1692;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1689;
    uint64_t _M0L6_2atmpS1690;
    uint64_t _M0L3hi3S596;
    uint64_t _M0L3lo4S597;
    uint64_t _M0L6_2atmpS1687;
    uint64_t _M0L6_2atmpS1688;
    uint64_t _M0L4mid4S598;
    uint64_t _M0L6_2atmpS1686;
    uint64_t _M0L3hi4S599;
    int32_t _M0L6_2atmpS1685;
    if (_M0L3lo3S594 < _M0L5_2aloS577) {
      _M0L6_2atmpS1692 = 1ull;
    } else {
      _M0L6_2atmpS1692 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1691 + _M0L6_2atmpS1692;
    _M0L6_2atmpS1689 = _M0L2hiS583 + _M0L2hiS583;
    if (_M0L4mid3S595 < _M0L3midS582) {
      _M0L6_2atmpS1690 = 1ull;
    } else {
      _M0L6_2atmpS1690 = 0ull;
    }
    _M0L3hi3S596 = _M0L6_2atmpS1689 + _M0L6_2atmpS1690;
    _M0L3lo4S597 = _M0L3lo3S594 - _M0L7_2amul0S571;
    _M0L6_2atmpS1687 = _M0L4mid3S595 - _M0L7_2amul1S573;
    if (_M0L3lo3S594 < _M0L3lo4S597) {
      _M0L6_2atmpS1688 = 1ull;
    } else {
      _M0L6_2atmpS1688 = 0ull;
    }
    _M0L4mid4S598 = _M0L6_2atmpS1687 - _M0L6_2atmpS1688;
    if (_M0L4mid3S595 < _M0L4mid4S598) {
      _M0L6_2atmpS1686 = 1ull;
    } else {
      _M0L6_2atmpS1686 = 0ull;
    }
    _M0L3hi4S599 = _M0L3hi3S596 - _M0L6_2atmpS1686;
    _M0L6_2atmpS1685 = _M0L1jS588 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS589
    = _M0FPB13shiftright128(_M0L4mid4S598, _M0L3hi4S599, _M0L6_2atmpS1685);
  }
  _M0L6_2atmpS1695 = _M0L1jS588 - 64;
  _M0L6_2atmpS1694 = _M0L6_2atmpS1695 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS600
  = _M0FPB13shiftright128(_M0L3midS582, _M0L2hiS583, _M0L6_2atmpS1694);
  _M0L6_2atmpS1693 = _M0Lm2vmS589;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS600,
                                                .$1 = _M0L2vpS587,
                                                .$2 = _M0L6_2atmpS1693};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  uint64_t _M0L6_2atmpS1679;
  uint64_t _M0L6_2atmpS1678;
  uint64_t _M0L6_2atmpS1677;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1679 = 1ull << (_M0L1pS570 & 63);
  _M0L6_2atmpS1678 = _M0L6_2atmpS1679 - 1ull;
  _M0L6_2atmpS1677 = _M0L5valueS569 & _M0L6_2atmpS1678;
  return _M0L6_2atmpS1677 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS567,
  int32_t _M0L1pS568
) {
  int32_t _M0L6_2atmpS1676;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1676 = _M0FPB10pow5Factor(_M0L5valueS567);
  return _M0L6_2atmpS1676 >= _M0L1pS568;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS562) {
  uint64_t _M0L6_2atmpS1667;
  uint64_t _M0L6_2atmpS1668;
  uint64_t _M0L6_2atmpS1669;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L6_2atmpS1675;
  int32_t _M0L5countS563;
  uint64_t _M0L1vS564;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1667 = _M0L5valueS562 % 5ull;
  if (_M0L6_2atmpS1667 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1668 = _M0L5valueS562 % 25ull;
  if (_M0L6_2atmpS1668 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1669 = _M0L5valueS562 % 125ull;
  if (_M0L6_2atmpS1669 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1670 = _M0L5valueS562 % 625ull;
  if (_M0L6_2atmpS1670 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1675 = _M0L5valueS562 / 625ull;
  _M0L5countS563 = 4;
  _M0L1vS564 = _M0L6_2atmpS1675;
  while (1) {
    if (_M0L1vS564 > 0ull) {
      uint64_t _M0L6_2atmpS1671 = _M0L1vS564 % 5ull;
      int32_t _M0L6_2atmpS1672;
      uint64_t _M0L6_2atmpS1673;
      if (_M0L6_2atmpS1671 != 0ull) {
        return _M0L5countS563;
      }
      _M0L6_2atmpS1672 = _M0L5countS563 + 1;
      _M0L6_2atmpS1673 = _M0L1vS564 / 5ull;
      _M0L5countS563 = _M0L6_2atmpS1672;
      _M0L1vS564 = _M0L6_2atmpS1673;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS566;
      moonbit_string_t _M0L6_2atmpS1674;
      int32_t _result_2339;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS566
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS566, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS566, _M0L5valueS562);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1674
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS566);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS566);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2339 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1674);
      moonbit_decref_cycle_free(_M0L6_2atmpS1674);
      return _result_2339;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS561,
  uint64_t _M0L2hiS559,
  int32_t _M0L4distS560
) {
  int32_t _M0L6_2atmpS1666;
  uint64_t _M0L6_2atmpS1664;
  uint64_t _M0L6_2atmpS1665;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1666 = 64 - _M0L4distS560;
  _M0L6_2atmpS1664 = _M0L2hiS559 << (_M0L6_2atmpS1666 & 63);
  _M0L6_2atmpS1665 = _M0L2loS561 >> (_M0L4distS560 & 63);
  return _M0L6_2atmpS1664 | _M0L6_2atmpS1665;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS549,
  uint64_t _M0L1bS552
) {
  uint64_t _M0L3aLoS548;
  uint64_t _M0L3aHiS550;
  uint64_t _M0L3bLoS551;
  uint64_t _M0L3bHiS553;
  uint64_t _M0L1xS554;
  uint64_t _M0L6_2atmpS1662;
  uint64_t _M0L6_2atmpS1663;
  uint64_t _M0L1yS555;
  uint64_t _M0L6_2atmpS1660;
  uint64_t _M0L6_2atmpS1661;
  uint64_t _M0L1zS556;
  uint64_t _M0L6_2atmpS1658;
  uint64_t _M0L6_2atmpS1659;
  uint64_t _M0L6_2atmpS1656;
  uint64_t _M0L6_2atmpS1657;
  uint64_t _M0L1wS557;
  uint64_t _M0L2loS558;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS548 = _M0L1aS549 & 4294967295ull;
  _M0L3aHiS550 = _M0L1aS549 >> 32;
  _M0L3bLoS551 = _M0L1bS552 & 4294967295ull;
  _M0L3bHiS553 = _M0L1bS552 >> 32;
  _M0L1xS554 = _M0L3aLoS548 * _M0L3bLoS551;
  _M0L6_2atmpS1662 = _M0L3aHiS550 * _M0L3bLoS551;
  _M0L6_2atmpS1663 = _M0L1xS554 >> 32;
  _M0L1yS555 = _M0L6_2atmpS1662 + _M0L6_2atmpS1663;
  _M0L6_2atmpS1660 = _M0L3aLoS548 * _M0L3bHiS553;
  _M0L6_2atmpS1661 = _M0L1yS555 & 4294967295ull;
  _M0L1zS556 = _M0L6_2atmpS1660 + _M0L6_2atmpS1661;
  _M0L6_2atmpS1658 = _M0L3aHiS550 * _M0L3bHiS553;
  _M0L6_2atmpS1659 = _M0L1yS555 >> 32;
  _M0L6_2atmpS1656 = _M0L6_2atmpS1658 + _M0L6_2atmpS1659;
  _M0L6_2atmpS1657 = _M0L1zS556 >> 32;
  _M0L1wS557 = _M0L6_2atmpS1656 + _M0L6_2atmpS1657;
  _M0L2loS558 = _M0L1aS549 * _M0L1bS552;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS558, .$1 = _M0L1wS557};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS546,
  int32_t _M0L4fromS543,
  int32_t _M0L2toS542
) {
  int32_t _M0L3lenS541;
  int32_t _M0L6_2atmpS1655;
  uint16_t* _M0L6bufferS544;
  int32_t _M0L1iS545;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS541 = _M0L2toS542 - _M0L4fromS543;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1655 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS544
  = (uint16_t*)moonbit_make_string(_M0L3lenS541, _M0L6_2atmpS1655);
  _M0L1iS545 = 0;
  while (1) {
    if (_M0L1iS545 < _M0L3lenS541) {
      int32_t _M0L6_2atmpS1653 = _M0L4fromS543 + _M0L1iS545;
      int32_t _M0L6_2atmpS1652;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L6_2atmpS1654;
      if (
        _M0L6_2atmpS1653 < 0
        || _M0L6_2atmpS1653 >= Moonbit_array_length(_M0L5bytesS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1652 = (int32_t)_M0L5bytesS546[_M0L6_2atmpS1653];
      _M0L6_2atmpS1651 = (uint16_t)_M0L6_2atmpS1652;
      if (
        _M0L1iS545 < 0 || _M0L1iS545 >= Moonbit_array_length(_M0L6bufferS544)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS544[_M0L1iS545] = _M0L6_2atmpS1651;
      _M0L6_2atmpS1654 = _M0L1iS545 + 1;
      _M0L1iS545 = _M0L6_2atmpS1654;
      continue;
    }
    break;
  }
  return _M0L6bufferS544;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS540) {
  int32_t _M0L6_2atmpS1650;
  uint32_t _M0L6_2atmpS1649;
  uint32_t _M0L6_2atmpS1648;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1650 = _M0L1eS540 * 78913;
  _M0L6_2atmpS1649 = *(uint32_t*)&_M0L6_2atmpS1650;
  _M0L6_2atmpS1648 = _M0L6_2atmpS1649 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1648;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS539) {
  int32_t _M0L6_2atmpS1647;
  uint32_t _M0L6_2atmpS1646;
  uint32_t _M0L6_2atmpS1645;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1647 = _M0L1eS539 * 732923;
  _M0L6_2atmpS1646 = *(uint32_t*)&_M0L6_2atmpS1647;
  _M0L6_2atmpS1645 = _M0L6_2atmpS1646 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1645;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS537,
  int32_t _M0L8exponentS538,
  int32_t _M0L8mantissaS535
) {
  moonbit_string_t _M0L1sS536;
  moonbit_string_t _result_2342;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS535) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS537) {
    _M0L1sS536 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS536 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS538) {
    moonbit_string_t _result_2341;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2341
    = moonbit_add_string(_M0L1sS536, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS536);
    return _result_2341;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2342
  = moonbit_add_string(_M0L1sS536, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS536);
  return _result_2342;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS534) {
  int32_t _M0L6_2atmpS1644;
  uint32_t _M0L6_2atmpS1643;
  uint32_t _M0L6_2atmpS1642;
  int32_t _M0L6_2atmpS1641;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1644 = _M0L1eS534 * 1217359;
  _M0L6_2atmpS1643 = *(uint32_t*)&_M0L6_2atmpS1644;
  _M0L6_2atmpS1642 = _M0L6_2atmpS1643 >> 19;
  _M0L6_2atmpS1641 = *(int32_t*)&_M0L6_2atmpS1642;
  return _M0L6_2atmpS1641 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS533) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS533 != _M0L4selfS533) {
    return 0;
  } else if (_M0L4selfS533 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS533 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS533;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS532) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS532 != _M0L4selfS532) {
    return 0ll;
  } else if (_M0L4selfS532 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS532 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS532;
  }
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS529
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1638;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2343;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1638
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS529, 0);
  _block_2343
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2343)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _block_2343->$0 = _M0L6_2atmpS1638;
  _block_2343->$1 = _M0L3lenS529;
  return _block_2343;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS530
) {
  float* _M0L6_2atmpS1639;
  struct _M0TPB5ArrayGfE* _block_2344;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1639 = (float*)moonbit_make_float_array_raw(_M0L3lenS530);
  _block_2344
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2344)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_2344->$0 = _M0L6_2atmpS1639;
  _block_2344->$1 = _M0L3lenS530;
  return _block_2344;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS531
) {
  int32_t* _M0L6_2atmpS1640;
  struct _M0TPB5ArrayGiE* _block_2345;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1640 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS531);
  _block_2345
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2345)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _block_2345->$0 = _M0L6_2atmpS1640;
  _block_2345->$1 = _M0L3lenS531;
  return _block_2345;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS525,
  int32_t _M0L5indexS526
) {
  uint64_t* _M0L6_2atmpS1636;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1636 = _M0L4selfS525;
  if (
    _M0L5indexS526 < 0
    || _M0L5indexS526 >= Moonbit_array_length(_M0L6_2atmpS1636)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1636[_M0L5indexS526];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint32_t* _M0L6_2atmpS1637;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1637 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1637)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1637[_M0L5indexS528];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS524
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS524, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS523) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS523, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS522) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS522;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS510,
  float _M0L5valueS512
) {
  int32_t _M0L3lenS1608;
  float* _M0L6_2atmpS1610;
  int32_t _M0L6_2atmpS1609;
  int32_t _M0L6lengthS511;
  float* _M0L3bufS1613;
  int32_t _M0L6_2atmpS1614;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1608 = _M0L4selfS510->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1610 = _M0MPC15array5Array6bufferGfE(_M0L4selfS510);
  _M0L6_2atmpS1609 = Moonbit_array_length(_M0L6_2atmpS1610);
  moonbit_decref_cycle_free(_M0L6_2atmpS1610);
  if (_M0L3lenS1608 == _M0L6_2atmpS1609) {
    int32_t _M0L3lenS1612 = _M0L4selfS510->$1;
    int32_t _M0L6_2atmpS1611 = _M0L3lenS1612 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS510, _M0L6_2atmpS1611);
  }
  _M0L6lengthS511 = _M0L4selfS510->$1;
  _M0L3bufS1613 = _M0L4selfS510->$0;
  _M0L3bufS1613[_M0L6lengthS511] = _M0L5valueS512;
  _M0L6_2atmpS1614 = _M0L6lengthS511 + 1;
  _M0L4selfS510->$1 = _M0L6_2atmpS1614;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS513,
  moonbit_string_t _M0L5valueS515
) {
  int32_t _M0L3lenS1615;
  moonbit_string_t* _M0L6_2atmpS1617;
  int32_t _M0L6_2atmpS1616;
  int32_t _M0L6lengthS514;
  moonbit_string_t* _M0L3bufS1620;
  moonbit_string_t _M0L6_2aoldS2231;
  int32_t _M0L6_2atmpS1621;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1615 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1617 = _M0MPC15array5Array6bufferGsE(_M0L4selfS513);
  _M0L6_2atmpS1616 = Moonbit_array_length(_M0L6_2atmpS1617);
  moonbit_decref_cycle_free(_M0L6_2atmpS1617);
  if (_M0L3lenS1615 == _M0L6_2atmpS1616) {
    int32_t _M0L3lenS1619 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS1618 = _M0L3lenS1619 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS513, _M0L6_2atmpS1618);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS1620 = _M0L4selfS513->$0;
  _M0L6_2aoldS2231 = (moonbit_string_t)_M0L3bufS1620[_M0L6lengthS514];
  moonbit_decref_cycle_free(_M0L6_2aoldS2231);
  _M0L3bufS1620[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS1621 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS1621;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS516,
  struct _M0TUsiE* _M0L5valueS518
) {
  int32_t _M0L3lenS1622;
  struct _M0TUsiE** _M0L6_2atmpS1624;
  int32_t _M0L6_2atmpS1623;
  int32_t _M0L6lengthS517;
  struct _M0TUsiE** _M0L3bufS1627;
  struct _M0TUsiE* _M0L6_2aoldS2232;
  int32_t _M0L6_2atmpS1628;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1622 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1624 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS516);
  _M0L6_2atmpS1623 = Moonbit_array_length(_M0L6_2atmpS1624);
  moonbit_decref_cycle_free(_M0L6_2atmpS1624);
  if (_M0L3lenS1622 == _M0L6_2atmpS1623) {
    int32_t _M0L3lenS1626 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS1625 = _M0L3lenS1626 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS516, _M0L6_2atmpS1625);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS1627 = _M0L4selfS516->$0;
  _M0L6_2aoldS2232 = (struct _M0TUsiE*)_M0L3bufS1627[_M0L6lengthS517];
  if (_M0L6_2aoldS2232) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2232);
  }
  _M0L3bufS1627[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS1628 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS1628;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS519,
  int32_t _M0L5valueS521
) {
  int32_t _M0L3lenS1629;
  int32_t* _M0L6_2atmpS1631;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L6lengthS520;
  int32_t* _M0L3bufS1634;
  int32_t _M0L6_2atmpS1635;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1629 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1631 = _M0MPC15array5Array6bufferGiE(_M0L4selfS519);
  _M0L6_2atmpS1630 = Moonbit_array_length(_M0L6_2atmpS1631);
  moonbit_decref_cycle_free(_M0L6_2atmpS1631);
  if (_M0L3lenS1629 == _M0L6_2atmpS1630) {
    int32_t _M0L3lenS1633 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS1632 = _M0L3lenS1633 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS519, _M0L6_2atmpS1632);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS1634 = _M0L4selfS519->$0;
  _M0L3bufS1634[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS1635 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS1635;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS495,
  int32_t _M0L8requiredS497
) {
  int32_t _M0L8old__capS494;
  int32_t _M0L3lenS1604;
  int32_t _M0L8new__capS496;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS494 = _M0MPC15array5Array8capacityGfE(_M0L4selfS495);
  _M0L3lenS1604 = _M0L4selfS495->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS496
  = _M0FPB23array__growth__capacity(_M0L8old__capS494, _M0L3lenS1604, _M0L8requiredS497);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS495, _M0L8new__capS496);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS499,
  int32_t _M0L8requiredS501
) {
  int32_t _M0L8old__capS498;
  int32_t _M0L3lenS1605;
  int32_t _M0L8new__capS500;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS498 = _M0MPC15array5Array8capacityGsE(_M0L4selfS499);
  _M0L3lenS1605 = _M0L4selfS499->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS500
  = _M0FPB23array__growth__capacity(_M0L8old__capS498, _M0L3lenS1605, _M0L8requiredS501);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS499, _M0L8new__capS500);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS503,
  int32_t _M0L8requiredS505
) {
  int32_t _M0L8old__capS502;
  int32_t _M0L3lenS1606;
  int32_t _M0L8new__capS504;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS502 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS503);
  _M0L3lenS1606 = _M0L4selfS503->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS504
  = _M0FPB23array__growth__capacity(_M0L8old__capS502, _M0L3lenS1606, _M0L8requiredS505);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS503, _M0L8new__capS504);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS507,
  int32_t _M0L8requiredS509
) {
  int32_t _M0L8old__capS506;
  int32_t _M0L3lenS1607;
  int32_t _M0L8new__capS508;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS506 = _M0MPC15array5Array8capacityGiE(_M0L4selfS507);
  _M0L3lenS1607 = _M0L4selfS507->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS508
  = _M0FPB23array__growth__capacity(_M0L8old__capS506, _M0L3lenS1607, _M0L8requiredS509);
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
  float* _M0L6_2aoldS2233;
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
  _M0L6_2aoldS2233 = _M0L4selfS471->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2233);
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
  moonbit_string_t* _M0L6_2aoldS2234;
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
  _M0L6_2aoldS2234 = _M0L4selfS477->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2234);
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
  struct _M0TUsiE** _M0L6_2aoldS2235;
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
  _M0L6_2aoldS2235 = _M0L4selfS483->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2235);
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
  int32_t* _M0L6_2aoldS2236;
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
  _M0L6_2aoldS2236 = _M0L4selfS489->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2236);
  _M0L4selfS489->$0 = _M0L8new__bufS493;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS466
) {
  float* _M0L6_2atmpS1600;
  int32_t _result_2346;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1600 = _M0MPC15array5Array6bufferGfE(_M0L4selfS466);
  _result_2346 = Moonbit_array_length(_M0L6_2atmpS1600);
  moonbit_decref_cycle_free(_M0L6_2atmpS1600);
  return _result_2346;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1601;
  int32_t _result_2347;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1601 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2347 = Moonbit_array_length(_M0L6_2atmpS1601);
  moonbit_decref_cycle_free(_M0L6_2atmpS1601);
  return _result_2347;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1602;
  int32_t _result_2348;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1602 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2348 = Moonbit_array_length(_M0L6_2atmpS1602);
  moonbit_decref_cycle_free(_M0L6_2atmpS1602);
  return _result_2348;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS1603;
  int32_t _result_2349;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1603 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_2349 = Moonbit_array_length(_M0L6_2atmpS1603);
  moonbit_decref_cycle_free(_M0L6_2atmpS1603);
  return _result_2349;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS457) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS457->$1;
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2237;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2237 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2237);
  return _M0L8_2afieldS2237;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS453
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2238;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2238 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2238);
  return _M0L8_2afieldS2238;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2239;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2239 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2239);
  return _M0L8_2afieldS2239;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2240;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2240 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2240);
  return _M0L8_2afieldS2240;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2241;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2241 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2241);
  return _M0L8_2afieldS2241;
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
  int32_t _M0L3endS1598;
  int32_t _M0L5startS1599;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1597;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1590;
  int32_t _M0L6_2atmpS1589;
  int32_t _if__result_2351;
  uint16_t* _M0L4dataS1591;
  int32_t _M0L3lenS1592;
  moonbit_string_t _M0L6_2atmpS1593;
  int32_t _M0L6_2atmpS1594;
  int32_t _M0L3lenS1596;
  int32_t _M0L6_2atmpS1595;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1598 = _M0L3strS448.$2;
  _M0L5startS1599 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1598 - _M0L5startS1599;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1597 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1597 + _M0L8str__lenS447;
  _M0L4dataS1590 = _M0L4selfS450->$0;
  _M0L6_2atmpS1589 = Moonbit_array_length(_M0L4dataS1590);
  if (_M0L8requiredS449 > _M0L6_2atmpS1589) {
    _if__result_2351 = 1;
  } else {
    int32_t _M0L3lenS1588 = _M0L4selfS450->$1;
    _if__result_2351 = _M0L8requiredS449 < _M0L3lenS1588;
  }
  if (_if__result_2351) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1591 = _M0L4selfS450->$0;
  _M0L3lenS1592 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1591);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1593 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1594 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1591, _M0L3lenS1592, _M0L6_2atmpS1593, _M0L6_2atmpS1594, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1591);
  moonbit_decref_cycle_free(_M0L6_2atmpS1593);
  _M0L3lenS1596 = _M0L4selfS450->$1;
  _M0L6_2atmpS1595 = _M0L3lenS1596 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1595;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2352;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1587;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1586;
  moonbit_string_t _result_2353;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1585 = Moonbit_array_length(_M0L3strS444);
    _if__result_2352 = _M0L3endS443 == _M0L6_2atmpS1585;
  } else {
    _if__result_2352 = 0;
  }
  if (_if__result_2352) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1587 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1587, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1586 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2353
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1586, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1586);
  return _result_2353;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2354;
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
      int32_t _M0L6_2atmpS1584 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2354 = _M0L6_2atmpS1584 <= _M0L3lenS436;
    } else {
      _if__result_2354 = 0;
    }
  } else {
    _if__result_2354 = 0;
  }
  if (_if__result_2354) {
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
  int32_t _M0L6_2atmpS1583;
  int32_t _M0L6_2atmpS1582;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1581;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1583 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1582 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1583;
  _M0L2e1S422 = _M0L6_2atmpS1582 - 1;
  _M0L6_2atmpS1581 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1581 - 1;
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
        int32_t _M0L6_2atmpS1578 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1577 = (int32_t)_M0L6_2atmpS1578;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1577;
        uint32_t _M0L6_2atmpS1573 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1572;
        int32_t _M0L6_2atmpS1574;
        uint32_t _M0L6_2atmpS1576;
        int32_t _M0L6_2atmpS1575;
        int32_t _M0L6_2atmpS1579;
        int32_t _M0L6_2atmpS1580;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1572 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1573);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1572;
        _M0L6_2atmpS1574 = _M0L1jS433 + 1;
        _M0L6_2atmpS1576 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1575 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1576);
        if (
          _M0L6_2atmpS1574 < 0
          || _M0L6_2atmpS1574 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1574] = _M0L6_2atmpS1575;
        _M0L6_2atmpS1579 = _M0L1iS432 + 1;
        _M0L6_2atmpS1580 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1579;
        _M0L1jS433 = _M0L6_2atmpS1580;
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
  int32_t _M0L6_2atmpS1571;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1571 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1571 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1570 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1570;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1567 = 1;
      } else {
        _M0L6_2atmpS1567 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1567;
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
      int32_t _M0L6_2atmpS1568;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1568 = 1;
      } else {
        _M0L6_2atmpS1568 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1568;
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
      int32_t _M0L6_2atmpS1569;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1569 = 1;
      } else {
        _M0L6_2atmpS1569 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1569;
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
  int32_t _M0L6_2atmpS1566;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1566 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1566;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1543 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1543;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1542 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1541 = 48 + _M0L6_2atmpS1542;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1540 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1539 = 48 + _M0L6_2atmpS1540;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1538 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1537 = 48 + _M0L6_2atmpS1538;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1536 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1535 = 48 + _M0L6_2atmpS1536;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1527 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1526 = _M0L6_2atmpS1527 - 4;
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1528;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1534;
      _M0L6bufferS381[_M0L6_2atmpS1526] = _M0L6d1__hiS377;
      _M0L6_2atmpS1529 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1528 = _M0L6_2atmpS1529 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1528] = _M0L6d1__loS378;
      _M0L6_2atmpS1531 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1530 = _M0L6_2atmpS1531 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1530] = _M0L6d2__hiS379;
      _M0L6_2atmpS1533 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1532 = _M0L6_2atmpS1533 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1532] = _M0L6d2__loS380;
      _M0L6_2atmpS1534 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1534;
      continue;
    } else {
      int32_t _M0L6_2atmpS1565 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1565;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1552 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1551 = 48 + _M0L6_2atmpS1552;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1551;
          int32_t _M0L6_2atmpS1550 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1549 = 48 + _M0L6_2atmpS1550;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1549;
          int32_t _M0L6_2atmpS1545 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - 2;
          int32_t _M0L6_2atmpS1547;
          int32_t _M0L6_2atmpS1546;
          int32_t _M0L6_2atmpS1548;
          _M0L6bufferS381[_M0L6_2atmpS1544] = _M0L5d__hiS388;
          _M0L6_2atmpS1547 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1546 = _M0L6_2atmpS1547 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1546] = _M0L5d__loS389;
          _M0L6_2atmpS1548 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1548;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1560 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1559 = 48 + _M0L6_2atmpS1560;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1559;
          int32_t _M0L6_2atmpS1558 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1557 = 48 + _M0L6_2atmpS1558;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1557;
          int32_t _M0L6_2atmpS1554 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1553 = _M0L6_2atmpS1554 - 2;
          int32_t _M0L6_2atmpS1556;
          int32_t _M0L6_2atmpS1555;
          _M0L6bufferS381[_M0L6_2atmpS1553] = _M0L5d__hiS391;
          _M0L6_2atmpS1556 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1555 = _M0L6_2atmpS1556 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1555] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1564 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1561 = _M0L6_2atmpS1564 - 1;
          int32_t _M0L6_2atmpS1563 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1562 = (uint16_t)_M0L6_2atmpS1563;
          _M0L6bufferS381[_M0L6_2atmpS1561] = _M0L6_2atmpS1562;
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
  int32_t _M0L6_2atmpS1511;
  int32_t _M0L6_2atmpS1510;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1511 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1510 = _M0L5radixS355 & _M0L6_2atmpS1511;
  if (_M0L6_2atmpS1510 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1518;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1518 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1518;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1517 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1517;
        int32_t _M0L6_2atmpS1514 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1512 = _M0L6_2atmpS1514 - 1;
        int32_t _M0L6_2atmpS1513 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1515;
        uint64_t _M0L6_2atmpS1516;
        _M0L6bufferS361[_M0L6_2atmpS1512] = _M0L6_2atmpS1513;
        _M0L6_2atmpS1515 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1516 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1515;
        _M0L1nS359 = _M0L6_2atmpS1516;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1525 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1525;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1524 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1523 = _M0L1nS367 - _M0L6_2atmpS1524;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1523;
        int32_t _M0L6_2atmpS1521 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1519 = _M0L6_2atmpS1521 - 1;
        int32_t _M0L6_2atmpS1520 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1522;
        _M0L6bufferS361[_M0L6_2atmpS1519] = _M0L6_2atmpS1520;
        _M0L6_2atmpS1522 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1522;
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
  int32_t _M0L6_2atmpS1509;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1509 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1509;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1506 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1506;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1500 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1498 = _M0L6_2atmpS1500 - 2;
      int32_t _M0L6_2atmpS1499 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1503;
      int32_t _M0L6_2atmpS1501;
      int32_t _M0L6_2atmpS1502;
      int32_t _M0L6_2atmpS1504;
      uint64_t _M0L6_2atmpS1505;
      _M0L6bufferS348[_M0L6_2atmpS1498] = _M0L6_2atmpS1499;
      _M0L6_2atmpS1503 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1501 = _M0L6_2atmpS1503 - 1;
      _M0L6_2atmpS1502
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1501] = _M0L6_2atmpS1502;
      _M0L6_2atmpS1504 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1505 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1504;
      _M0L1nS344 = _M0L6_2atmpS1505;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1508 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1508;
      int32_t _M0L6_2atmpS1507 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1507;
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
      uint64_t _M0L6_2atmpS1496 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1497 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1496;
      _M0L5countS341 = _M0L6_2atmpS1497;
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
    int32_t _M0L6_2atmpS1495;
    int32_t _M0L6_2atmpS1494;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1495 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1494 = _M0L6_2atmpS1495 / 4;
    return _M0L6_2atmpS1494 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1493 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1493;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1490;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1490 = 1;
      } else {
        _M0L6_2atmpS1490 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1490;
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
      int32_t _M0L6_2atmpS1491;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1491 = 1;
      } else {
        _M0L6_2atmpS1491 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1491;
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
      int32_t _M0L6_2atmpS1492;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1492 = 1;
      } else {
        _M0L6_2atmpS1492 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1492;
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
      uint32_t _M0L6_2atmpS1488 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1489 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1488;
      _M0L5countS315 = _M0L6_2atmpS1489;
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
    int32_t _M0L6_2atmpS1487;
    int32_t _M0L6_2atmpS1486;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1487 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1486 = _M0L6_2atmpS1487 / 4;
    return _M0L6_2atmpS1486 + 1;
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
  int32_t _M0L6_2atmpS1485;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1485 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1485;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1462 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1462;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1461 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1460 = 48 + _M0L6_2atmpS1461;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1460;
      int32_t _M0L6_2atmpS1459 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1458 = 48 + _M0L6_2atmpS1459;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1458;
      int32_t _M0L6_2atmpS1457 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1456 = 48 + _M0L6_2atmpS1457;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1456;
      int32_t _M0L6_2atmpS1455 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1454 = 48 + _M0L6_2atmpS1455;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1446 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1445 = _M0L6_2atmpS1446 - 4;
      int32_t _M0L6_2atmpS1448;
      int32_t _M0L6_2atmpS1447;
      int32_t _M0L6_2atmpS1450;
      int32_t _M0L6_2atmpS1449;
      int32_t _M0L6_2atmpS1452;
      int32_t _M0L6_2atmpS1451;
      int32_t _M0L6_2atmpS1453;
      _M0L6bufferS294[_M0L6_2atmpS1445] = _M0L6d1__hiS290;
      _M0L6_2atmpS1448 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1447 = _M0L6_2atmpS1448 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1447] = _M0L6d1__loS291;
      _M0L6_2atmpS1450 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1449 = _M0L6_2atmpS1450 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1449] = _M0L6d2__hiS292;
      _M0L6_2atmpS1452 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1451 = _M0L6_2atmpS1452 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1451] = _M0L6d2__loS293;
      _M0L6_2atmpS1453 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1453;
      continue;
    } else {
      int32_t _M0L6_2atmpS1484 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1484;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1471 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1470 = 48 + _M0L6_2atmpS1471;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1470;
          int32_t _M0L6_2atmpS1469 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1468 = 48 + _M0L6_2atmpS1469;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1468;
          int32_t _M0L6_2atmpS1464 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1463 = _M0L6_2atmpS1464 - 2;
          int32_t _M0L6_2atmpS1466;
          int32_t _M0L6_2atmpS1465;
          int32_t _M0L6_2atmpS1467;
          _M0L6bufferS294[_M0L6_2atmpS1463] = _M0L5d__hiS301;
          _M0L6_2atmpS1466 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1465 = _M0L6_2atmpS1466 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1465] = _M0L5d__loS302;
          _M0L6_2atmpS1467 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1467;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1479 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1478 = 48 + _M0L6_2atmpS1479;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1478;
          int32_t _M0L6_2atmpS1477 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1476 = 48 + _M0L6_2atmpS1477;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1476;
          int32_t _M0L6_2atmpS1473 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1472 = _M0L6_2atmpS1473 - 2;
          int32_t _M0L6_2atmpS1475;
          int32_t _M0L6_2atmpS1474;
          _M0L6bufferS294[_M0L6_2atmpS1472] = _M0L5d__hiS304;
          _M0L6_2atmpS1475 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1474 = _M0L6_2atmpS1475 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1474] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1483 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1480 = _M0L6_2atmpS1483 - 1;
          int32_t _M0L6_2atmpS1482 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1481 = (uint16_t)_M0L6_2atmpS1482;
          _M0L6bufferS294[_M0L6_2atmpS1480] = _M0L6_2atmpS1481;
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
  int32_t _M0L6_2atmpS1430;
  int32_t _M0L6_2atmpS1429;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1430 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1429 = _M0L5radixS268 & _M0L6_2atmpS1430;
  if (_M0L6_2atmpS1429 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1437;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1437 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1437;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1436 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1436;
        int32_t _M0L6_2atmpS1433 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1431 = _M0L6_2atmpS1433 - 1;
        int32_t _M0L6_2atmpS1432 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1434;
        uint32_t _M0L6_2atmpS1435;
        _M0L6bufferS274[_M0L6_2atmpS1431] = _M0L6_2atmpS1432;
        _M0L6_2atmpS1434 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1435 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1434;
        _M0L1nS272 = _M0L6_2atmpS1435;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1444 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1444;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1443 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1442 = _M0L1nS280 - _M0L6_2atmpS1443;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1442;
        int32_t _M0L6_2atmpS1440 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1438 = _M0L6_2atmpS1440 - 1;
        int32_t _M0L6_2atmpS1439 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1441;
        _M0L6bufferS274[_M0L6_2atmpS1438] = _M0L6_2atmpS1439;
        _M0L6_2atmpS1441 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1441;
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
  int32_t _M0L6_2atmpS1428;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1428 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1428;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1425 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1425;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1419 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1417 = _M0L6_2atmpS1419 - 2;
      int32_t _M0L6_2atmpS1418 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1422;
      int32_t _M0L6_2atmpS1420;
      int32_t _M0L6_2atmpS1421;
      int32_t _M0L6_2atmpS1423;
      uint32_t _M0L6_2atmpS1424;
      _M0L6bufferS261[_M0L6_2atmpS1417] = _M0L6_2atmpS1418;
      _M0L6_2atmpS1422 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1420 = _M0L6_2atmpS1422 - 1;
      _M0L6_2atmpS1421
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1420] = _M0L6_2atmpS1421;
      _M0L6_2atmpS1423 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1424 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1423;
      _M0L1nS257 = _M0L6_2atmpS1424;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1427 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1427;
      int32_t _M0L6_2atmpS1426 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1426;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1416;
  moonbit_string_t _result_2368;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1416
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1416);
  if (_M0L6_2atmpS1416.$1) {
    moonbit_decref(_M0L6_2atmpS1416.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2368 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2368;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1413;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1413 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1413);
  moonbit_decref_cycle_free(_M0L6_2atmpS1413);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1414;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1414 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1414);
  moonbit_decref_cycle_free(_M0L6_2atmpS1414);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1415;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1415 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1415);
  moonbit_decref_cycle_free(_M0L6_2atmpS1415);
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
  moonbit_string_t _M0L8_2afieldS2242;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2242 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2242);
  return _M0L8_2afieldS2242;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1412;
  int64_t _M0L6_2atmpS1411;
  struct _M0TPC16string10StringView _M0L6_2atmpS1410;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1412 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1411 = (int64_t)_M0L6_2atmpS1412;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1410
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1411);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1410);
  moonbit_decref_cycle_free(_M0L6_2atmpS1410.$0);
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
  int32_t _M0L6_2atmpS1394;
  int32_t _if__result_2369;
  int32_t _M0L6_2atmpS1402;
  int32_t _if__result_2370;
  int32_t _M0L6_2atmpS1404;
  int32_t _M0L6_2atmpS1405;
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
  _M0L6_2atmpS1394 = _M0Lm2loS236;
  if (_M0L6_2atmpS1394 > 0) {
    int32_t _M0L6_2atmpS1393 = _M0Lm2loS236;
    if (_M0L6_2atmpS1393 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1392 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1391 = _M0L4selfS235[_M0L6_2atmpS1392];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1391)) {
        int32_t _M0L6_2atmpS1390 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1389 = _M0L6_2atmpS1390 - 1;
        int32_t _M0L6_2atmpS1388 = _M0L4selfS235[_M0L6_2atmpS1389];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2369
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1388);
      } else {
        _if__result_2369 = 0;
      }
    } else {
      _if__result_2369 = 0;
    }
  } else {
    _if__result_2369 = 0;
  }
  if (_if__result_2369) {
    int32_t _M0L6_2atmpS1395 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1395 + 1;
  }
  _M0L6_2atmpS1402 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1402 > 0) {
    int32_t _M0L6_2atmpS1401 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1401 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1400 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1399 = _M0L4selfS235[_M0L6_2atmpS1400];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1399)) {
        int32_t _M0L6_2atmpS1398 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1397 = _M0L6_2atmpS1398 - 1;
        int32_t _M0L6_2atmpS1396 = _M0L4selfS235[_M0L6_2atmpS1397];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2370
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1396);
      } else {
        _if__result_2370 = 0;
      }
    } else {
      _if__result_2370 = 0;
    }
  } else {
    _if__result_2370 = 0;
  }
  if (_if__result_2370) {
    int32_t _M0L6_2atmpS1403 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1403 - 1;
  }
  _M0L6_2atmpS1404 = _M0Lm2loS236;
  _M0L6_2atmpS1405 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1404 >= _M0L6_2atmpS1405) {
    int32_t _M0L6_2atmpS1406 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1407 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1406,
                                                 .$2 = _M0L6_2atmpS1407};
  } else {
    int32_t _M0L6_2atmpS1408 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1409 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1408,
                                                 .$2 = _M0L6_2atmpS1409};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1387;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1387
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1387);
  if (_M0L6_2atmpS1387.$1) {
    moonbit_decref(_M0L6_2atmpS1387.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1386;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1386
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1386);
  if (_M0L6_2atmpS1386.$1) {
    moonbit_decref(_M0L6_2atmpS1386.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1385;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1385 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1385;
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
  int32_t _M0L6_2atmpS1384;
  struct _M0TPC16string10StringView _M0L6_2atmpS1382;
  struct _M0TPB6Logger _M0L6_2atmpS1383;
  moonbit_string_t _result_2371;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1384 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1382
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1384
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1383
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1382, _M0L6_2atmpS1383, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1382.$0);
  if (_M0L6_2atmpS1383.$1) {
    moonbit_decref(_M0L6_2atmpS1383.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2371 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2371;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1380;
  int32_t _M0L5startS1381;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1380 = _M0L4selfS218.$2;
  _M0L5startS1381 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1380 - _M0L5startS1381;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1377;
    int32_t _M0L5startS1379;
    int32_t _M0L6_2atmpS1378;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1361;
    int32_t _M0L6_2atmpS1362;
    int32_t _M0L6_2atmpS1363;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1377 = _M0L4selfS218.$0;
    _M0L5startS1379 = _M0L4selfS218.$1;
    _M0L6_2atmpS1378 = _M0L5startS1379 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1377[_M0L6_2atmpS1378];
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
        int32_t _M0L6_2atmpS1364;
        int32_t _M0L6_2atmpS1365;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1364 = _M0L1iS220 + 1;
        _M0L6_2atmpS1365 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1364;
        _M0L3segS221 = _M0L6_2atmpS1365;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1366;
        int32_t _M0L6_2atmpS1367;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1366 = _M0L1iS220 + 1;
        _M0L6_2atmpS1367 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1366;
        _M0L3segS221 = _M0L6_2atmpS1367;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1368;
        int32_t _M0L6_2atmpS1369;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1368 = _M0L1iS220 + 1;
        _M0L6_2atmpS1369 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1368;
        _M0L3segS221 = _M0L6_2atmpS1369;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1370;
        int32_t _M0L6_2atmpS1371;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1370 = _M0L1iS220 + 1;
        _M0L6_2atmpS1371 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1370;
        _M0L3segS221 = _M0L6_2atmpS1371;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1373;
          moonbit_string_t _M0L6_2atmpS1372;
          int32_t _M0L6_2atmpS1374;
          int32_t _M0L6_2atmpS1375;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1373 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1372 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1373);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1372);
          moonbit_decref_cycle_free(_M0L6_2atmpS1372);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1374 = _M0L1iS220 + 1;
          _M0L6_2atmpS1375 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1374;
          _M0L3segS221 = _M0L6_2atmpS1375;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1376 = _M0L1iS220 + 1;
          int32_t _tmp_2374 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1376;
          _M0L3segS221 = _tmp_2374;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2373;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1361 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1361);
    _M0L6_2atmpS1362 = _M0L1iS220 + 1;
    _M0L6_2atmpS1363 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1362;
    _M0L3segS221 = _M0L6_2atmpS1363;
    continue;
    joinlet_2373:;
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
    int64_t _M0L6_2atmpS1360 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1359;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1359
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1360);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1359);
    moonbit_decref_cycle_free(_M0L6_2atmpS1359.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1357;
  int32_t _M0L5startS1358;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1335;
  int32_t _if__result_2375;
  int32_t _M0L6_2atmpS1345;
  int32_t _if__result_2376;
  int32_t _M0L6_2atmpS1347;
  int32_t _M0L6_2atmpS1348;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1357 = _M0L4selfS201.$2;
  _M0L5startS1358 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1357 - _M0L5startS1358;
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
  _M0L6_2atmpS1335 = _M0Lm2loS202;
  if (_M0L6_2atmpS1335 > 0) {
    int32_t _M0L6_2atmpS1334 = _M0Lm2loS202;
    if (_M0L6_2atmpS1334 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1333 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1332 = _M0L4baseS209 + _M0L6_2atmpS1333;
      int32_t _M0L6_2atmpS1331 = _M0L3strS208[_M0L6_2atmpS1332];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1331)) {
        int32_t _M0L6_2atmpS1330 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1329 = _M0L4baseS209 + _M0L6_2atmpS1330;
        int32_t _M0L6_2atmpS1328 = _M0L6_2atmpS1329 - 1;
        int32_t _M0L6_2atmpS1327 = _M0L3strS208[_M0L6_2atmpS1328];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2375
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1327);
      } else {
        _if__result_2375 = 0;
      }
    } else {
      _if__result_2375 = 0;
    }
  } else {
    _if__result_2375 = 0;
  }
  if (_if__result_2375) {
    int32_t _M0L6_2atmpS1336 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1336 + 1;
  }
  _M0L6_2atmpS1345 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1345 > 0) {
    int32_t _M0L6_2atmpS1344 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1344 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1343 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1342 = _M0L4baseS209 + _M0L6_2atmpS1343;
      int32_t _M0L6_2atmpS1341 = _M0L3strS208[_M0L6_2atmpS1342];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1341)) {
        int32_t _M0L6_2atmpS1340 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1339 = _M0L4baseS209 + _M0L6_2atmpS1340;
        int32_t _M0L6_2atmpS1338 = _M0L6_2atmpS1339 - 1;
        int32_t _M0L6_2atmpS1337 = _M0L3strS208[_M0L6_2atmpS1338];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2376
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1337);
      } else {
        _if__result_2376 = 0;
      }
    } else {
      _if__result_2376 = 0;
    }
  } else {
    _if__result_2376 = 0;
  }
  if (_if__result_2376) {
    int32_t _M0L6_2atmpS1346 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1346 - 1;
  }
  _M0L6_2atmpS1347 = _M0Lm2loS202;
  _M0L6_2atmpS1348 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1347 >= _M0L6_2atmpS1348) {
    int32_t _M0L6_2atmpS1352 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1349 = _M0L4baseS209 + _M0L6_2atmpS1352;
    int32_t _M0L6_2atmpS1351 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1350 = _M0L4baseS209 + _M0L6_2atmpS1351;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1349,
                                                 .$2 = _M0L6_2atmpS1350};
  } else {
    int32_t _M0L6_2atmpS1356 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1353 = _M0L4baseS209 + _M0L6_2atmpS1356;
    int32_t _M0L6_2atmpS1355 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1354 = _M0L4baseS209 + _M0L6_2atmpS1355;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1353,
                                                 .$2 = _M0L6_2atmpS1354};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1324;
  int32_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1326;
  int32_t _M0L6_2atmpS1325;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1322;
  moonbit_string_t _result_2377;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1324 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1323
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1324);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1323);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1326 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1325
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1326);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1325);
  _M0L6_2atmpS1322 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2377 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1322);
  moonbit_decref_cycle_free(_M0L6_2atmpS1322);
  return _result_2377;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1319;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1319 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1319);
  } else {
    int32_t _M0L6_2atmpS1321;
    int32_t _M0L6_2atmpS1320;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1321 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1320 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1321, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1320);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1317;
  int32_t _M0L6_2atmpS1318;
  int32_t _M0L6_2atmpS1316;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1317 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1318 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1316 = _M0L6_2atmpS1317 - _M0L6_2atmpS1318;
  return _M0L6_2atmpS1316 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1314;
  int32_t _M0L6_2atmpS1315;
  int32_t _M0L6_2atmpS1313;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1314 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1315 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1313 = _M0L6_2atmpS1314 % _M0L6_2atmpS1315;
  return _M0L6_2atmpS1313 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1311;
  int32_t _M0L6_2atmpS1312;
  int32_t _M0L6_2atmpS1310;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1311 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1312 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1310 = _M0L6_2atmpS1311 / _M0L6_2atmpS1312;
  return _M0L6_2atmpS1310 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1308;
  int32_t _M0L6_2atmpS1309;
  int32_t _M0L6_2atmpS1307;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1308 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1309 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1307 = _M0L6_2atmpS1308 + _M0L6_2atmpS1309;
  return _M0L6_2atmpS1307 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1306;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1306 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1306;
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
  int32_t _M0L3lenS1305;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1300;
  int32_t _M0L6_2atmpS1299;
  int32_t _if__result_2378;
  uint16_t* _M0L4dataS1301;
  int32_t _M0L3lenS1302;
  int32_t _M0L3lenS1304;
  int32_t _M0L6_2atmpS1303;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1305 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1305 + _M0L8str__lenS182;
  _M0L4dataS1300 = _M0L4selfS185->$0;
  _M0L6_2atmpS1299 = Moonbit_array_length(_M0L4dataS1300);
  if (_M0L8requiredS184 > _M0L6_2atmpS1299) {
    _if__result_2378 = 1;
  } else {
    int32_t _M0L3lenS1298 = _M0L4selfS185->$1;
    _if__result_2378 = _M0L8requiredS184 < _M0L3lenS1298;
  }
  if (_if__result_2378) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1301 = _M0L4selfS185->$0;
  _M0L3lenS1302 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1301);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1301, _M0L3lenS1302, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1301);
  _M0L3lenS1304 = _M0L4selfS185->$1;
  _M0L6_2atmpS1303 = _M0L3lenS1304 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1303;
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
      int32_t _M0L6_2atmpS1295 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1296;
      int32_t _M0L6_2atmpS1297;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1295;
      _M0L6_2atmpS1296 = _M0L1iS176 + 1;
      _M0L6_2atmpS1297 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1296;
      _M0L1jS177 = _M0L6_2atmpS1297;
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
    int32_t _M0L3lenS1266 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1268 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1267 = Moonbit_array_length(_M0L4dataS1268);
    uint16_t* _M0L4dataS1271;
    int32_t _M0L3lenS1272;
    int32_t _M0L6_2atmpS1273;
    int32_t _M0L3lenS1275;
    int32_t _M0L6_2atmpS1274;
    if (_M0L3lenS1266 >= _M0L6_2atmpS1267) {
      int32_t _M0L3lenS1270 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1269 = _M0L3lenS1270 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1269);
    }
    _M0L4dataS1271 = _M0L4selfS171->$0;
    _M0L3lenS1272 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1271);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1273 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1272 < 0
      || _M0L3lenS1272 >= Moonbit_array_length(_M0L4dataS1271)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1271[_M0L3lenS1272] = _M0L6_2atmpS1273;
    moonbit_decref_cycle_free(_M0L4dataS1271);
    _M0L3lenS1275 = _M0L4selfS171->$1;
    _M0L6_2atmpS1274 = _M0L3lenS1275 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1274;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1279 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1277 = Moonbit_array_length(_M0L4dataS1279);
    int32_t _M0L3lenS1278 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1277 - _M0L3lenS1278;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1282;
    int32_t _M0L3lenS1283;
    uint32_t _M0L6_2atmpS1286;
    uint32_t _M0L6_2atmpS1285;
    int32_t _M0L6_2atmpS1284;
    uint16_t* _M0L4dataS1287;
    int32_t _M0L3lenS1292;
    int32_t _M0L6_2atmpS1288;
    uint32_t _M0L6_2atmpS1291;
    uint32_t _M0L6_2atmpS1290;
    int32_t _M0L6_2atmpS1289;
    int32_t _M0L3lenS1294;
    int32_t _M0L6_2atmpS1293;
    if (_M0L6_2atmpS1276 < 2) {
      int32_t _M0L3lenS1281 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1280 = _M0L3lenS1281 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1280);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1282 = _M0L4selfS171->$0;
    _M0L3lenS1283 = _M0L4selfS171->$1;
    _M0L6_2atmpS1286 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1285 = 55296u + _M0L6_2atmpS1286;
    moonbit_incref_cycle_free(_M0L4dataS1282);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1284 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1285);
    if (
      _M0L3lenS1283 < 0
      || _M0L3lenS1283 >= Moonbit_array_length(_M0L4dataS1282)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1282[_M0L3lenS1283] = _M0L6_2atmpS1284;
    moonbit_decref_cycle_free(_M0L4dataS1282);
    _M0L4dataS1287 = _M0L4selfS171->$0;
    _M0L3lenS1292 = _M0L4selfS171->$1;
    _M0L6_2atmpS1288 = _M0L3lenS1292 + 1;
    _M0L6_2atmpS1291 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1290 = 56320u + _M0L6_2atmpS1291;
    moonbit_incref_cycle_free(_M0L4dataS1287);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1289 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1290);
    if (
      _M0L6_2atmpS1288 < 0
      || _M0L6_2atmpS1288 >= Moonbit_array_length(_M0L4dataS1287)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1287[_M0L6_2atmpS1288] = _M0L6_2atmpS1289;
    moonbit_decref_cycle_free(_M0L4dataS1287);
    _M0L3lenS1294 = _M0L4selfS171->$1;
    _M0L6_2atmpS1293 = _M0L3lenS1294 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1293;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1265;
  int32_t _M0L6_2atmpS1263;
  int32_t _M0L3lenS1264;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1260;
  int32_t _M0L6_2atmpS1261;
  int32_t _M0L3lenS1262;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2243;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1265 = _M0L4selfS166->$0;
  _M0L6_2atmpS1263 = Moonbit_array_length(_M0L4dataS1265);
  _M0L3lenS1264 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1263, _M0L3lenS1264, _M0L8requiredS167);
  _M0L4dataS1260 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1260);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1261 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1262 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1260, _M0L13new__capacityS165, _M0L6_2atmpS1261, _M0L3lenS1262, 0, 0);
  _M0L6_2aoldS2243 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2243);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
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
  int32_t _M0L6_2atmpS1259;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1259 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1259;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1258;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1258 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1258;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1249;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1249 = _M0L4selfS155->$1;
  if (_M0L3lenS1249 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1250 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1252 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1251 = Moonbit_array_length(_M0L4dataS1252);
    if (_M0L3lenS1250 == _M0L6_2atmpS1251) {
      uint16_t* _M0L4dataS1253 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1253);
      return _M0L4dataS1253;
    } else {
      uint16_t* _M0L4dataS1254 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1255 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1256;
      int32_t _M0L3lenS1257;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1254);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1256 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1257 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1254, _M0L3lenS1255, _M0L6_2atmpS1256, _M0L3lenS1257, 0, 0);
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
  int32_t _if__result_2381;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1245 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1246 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1245 <= _M0L6_2atmpS1246) {
            int32_t _M0L6_2atmpS1244 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2381 = _M0L6_2atmpS1244 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2381 = 0;
          }
        } else {
          _if__result_2381 = 0;
        }
      } else {
        _if__result_2381 = 0;
      }
    } else {
      _if__result_2381 = 0;
    }
  } else {
    _if__result_2381 = 0;
  }
  if (_if__result_2381) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1248;
    moonbit_string_t _M0L6_2atmpS1247;
    uint16_t* _result_2382;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS1248 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1248);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1247
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2382 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1247);
    moonbit_decref_cycle_free(_M0L6_2atmpS1247);
    return _result_2382;
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
  struct _M0TPB13StringBuilder* _block_2383;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1243 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1243 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2383
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2383)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 56, 0);
  _block_2383->$0 = _M0L4dataS140;
  _block_2383->$1 = 0;
  return _block_2383;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1242;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1242 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1242;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2384;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1223 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1224;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1224
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
          if (_M0L6_2atmpS1223 <= _M0L6_2atmpS1224) {
            int32_t _M0L6_2atmpS1222 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2384 = _M0L6_2atmpS1222 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2384 = 0;
          }
        } else {
          _if__result_2384 = 0;
        }
      } else {
        _if__result_2384 = 0;
      }
    } else {
      _if__result_2384 = 0;
    }
  } else {
    _if__result_2384 = 0;
  }
  if (_if__result_2384) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1226;
    moonbit_string_t _M0L6_2atmpS1225;
    float* _result_2385;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1226 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1226);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1225
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2385
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1225);
    moonbit_decref_cycle_free(_M0L6_2atmpS1225);
    return _result_2385;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2386;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1228 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1229;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1229
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
          if (_M0L6_2atmpS1228 <= _M0L6_2atmpS1229) {
            int32_t _M0L6_2atmpS1227 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2386 = _M0L6_2atmpS1227 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2386 = 0;
          }
        } else {
          _if__result_2386 = 0;
        }
      } else {
        _if__result_2386 = 0;
      }
    } else {
      _if__result_2386 = 0;
    }
  } else {
    _if__result_2386 = 0;
  }
  if (_if__result_2386) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1231;
    moonbit_string_t _M0L6_2atmpS1230;
    moonbit_string_t* _result_2387;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1231 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1231);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1230
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2387
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1230);
    moonbit_decref_cycle_free(_M0L6_2atmpS1230);
    return _result_2387;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2388;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1233 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1234;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1234
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
          if (_M0L6_2atmpS1233 <= _M0L6_2atmpS1234) {
            int32_t _M0L6_2atmpS1232 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2388 = _M0L6_2atmpS1232 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2388 = 0;
          }
        } else {
          _if__result_2388 = 0;
        }
      } else {
        _if__result_2388 = 0;
      }
    } else {
      _if__result_2388 = 0;
    }
  } else {
    _if__result_2388 = 0;
  }
  if (_if__result_2388) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, 0, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1236;
    moonbit_string_t _M0L6_2atmpS1235;
    struct _M0TUsiE** _result_2389;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1236 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1236);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1235
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2389
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1235);
    moonbit_decref_cycle_free(_M0L6_2atmpS1235);
    return _result_2389;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2390;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1238 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1239;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1239
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
          if (_M0L6_2atmpS1238 <= _M0L6_2atmpS1239) {
            int32_t _M0L6_2atmpS1237 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2390 = _M0L6_2atmpS1237 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2390 = 0;
          }
        } else {
          _if__result_2390 = 0;
        }
      } else {
        _if__result_2390 = 0;
      }
    } else {
      _if__result_2390 = 0;
    }
  } else {
    _if__result_2390 = 0;
  }
  if (_if__result_2390) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1241;
    moonbit_string_t _M0L6_2atmpS1240;
    int32_t* _result_2391;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1241 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1241);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1240
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2391
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1240);
    moonbit_decref_cycle_free(_M0L6_2atmpS1240);
    return _result_2391;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1219;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1219
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1219);
  if (_M0L6_2atmpS1219.$1) {
    moonbit_decref(_M0L6_2atmpS1219.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1220;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1220
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1220);
  if (_M0L6_2atmpS1220.$1) {
    moonbit_decref(_M0L6_2atmpS1220.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1221;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1221
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1221);
  if (_M0L6_2atmpS1221.$1) {
    moonbit_decref(_M0L6_2atmpS1221.$1);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  moonbit_string_t* _M0L3srcS70,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  struct _M0TUsiE** _M0L3srcS75,
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
        int32_t _M0L6_2atmpS1174 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1176 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1175;
        int32_t _M0L6_2atmpS1177;
        if (
          _M0L6_2atmpS1176 < 0
          || _M0L6_2atmpS1176 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1175 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1176];
        if (
          _M0L6_2atmpS1174 < 0
          || _M0L6_2atmpS1174 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1174] = _M0L6_2atmpS1175;
        _M0L6_2atmpS1177 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1177;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1182 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1182;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1178 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1180 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1179;
        int32_t _M0L6_2atmpS1181;
        if (
          _M0L6_2atmpS1180 < 0
          || _M0L6_2atmpS1180 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1179 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1180];
        if (
          _M0L6_2atmpS1178 < 0
          || _M0L6_2atmpS1178 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1178] = _M0L6_2atmpS1179;
        _M0L6_2atmpS1181 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1181;
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
        int32_t _M0L6_2atmpS1183 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1185 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1184;
        int32_t _M0L6_2atmpS1186;
        if (
          _M0L6_2atmpS1185 < 0
          || _M0L6_2atmpS1185 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1184 = (float)_M0L3srcS28[_M0L6_2atmpS1185];
        if (
          _M0L6_2atmpS1183 < 0
          || _M0L6_2atmpS1183 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1183] = _M0L6_2atmpS1184;
        _M0L6_2atmpS1186 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1186;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1191 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1191;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1187 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1189 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1188;
        int32_t _M0L6_2atmpS1190;
        if (
          _M0L6_2atmpS1189 < 0
          || _M0L6_2atmpS1189 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1188 = (float)_M0L3srcS28[_M0L6_2atmpS1189];
        if (
          _M0L6_2atmpS1187 < 0
          || _M0L6_2atmpS1187 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1187] = _M0L6_2atmpS1188;
        _M0L6_2atmpS1190 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1190;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  moonbit_string_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1192 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1194 = _M0L11src__offsetS39 + _M0L1iS40;
        moonbit_string_t _M0L6_2atmpS1193;
        moonbit_string_t _M0L6_2aoldS2244;
        int32_t _M0L6_2atmpS1195;
        if (
          _M0L6_2atmpS1194 < 0
          || _M0L6_2atmpS1194 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1193 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS1194];
        if (
          _M0L6_2atmpS1192 < 0
          || _M0L6_2atmpS1192 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2244 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS1192];
        moonbit_incref_cycle_free(_M0L6_2atmpS1193);
        moonbit_decref_cycle_free(_M0L6_2aoldS2244);
        _M0L3dstS36[_M0L6_2atmpS1192] = _M0L6_2atmpS1193;
        _M0L6_2atmpS1195 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1195;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1200 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1200;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1196 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1198 = _M0L11src__offsetS39 + _M0L1iS43;
        moonbit_string_t _M0L6_2atmpS1197;
        moonbit_string_t _M0L6_2aoldS2245;
        int32_t _M0L6_2atmpS1199;
        if (
          _M0L6_2atmpS1198 < 0
          || _M0L6_2atmpS1198 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1197 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS1198];
        if (
          _M0L6_2atmpS1196 < 0
          || _M0L6_2atmpS1196 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2245 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS1196];
        moonbit_incref_cycle_free(_M0L6_2atmpS1197);
        moonbit_decref_cycle_free(_M0L6_2aoldS2245);
        _M0L3dstS36[_M0L6_2atmpS1196] = _M0L6_2atmpS1197;
        _M0L6_2atmpS1199 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1199;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  struct _M0TUsiE** _M0L3srcS46,
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
        int32_t _M0L6_2atmpS1201 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1203 = _M0L11src__offsetS48 + _M0L1iS49;
        struct _M0TUsiE* _M0L6_2atmpS1202;
        struct _M0TUsiE* _M0L6_2aoldS2246;
        int32_t _M0L6_2atmpS1204;
        if (
          _M0L6_2atmpS1203 < 0
          || _M0L6_2atmpS1203 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1202 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS1203];
        if (
          _M0L6_2atmpS1201 < 0
          || _M0L6_2atmpS1201 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2246 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS1201];
        if (_M0L6_2atmpS1202) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1202);
        }
        if (_M0L6_2aoldS2246) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2246);
        }
        _M0L3dstS45[_M0L6_2atmpS1201] = _M0L6_2atmpS1202;
        _M0L6_2atmpS1204 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1204;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1209 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1209;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1205 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1207 = _M0L11src__offsetS48 + _M0L1iS52;
        struct _M0TUsiE* _M0L6_2atmpS1206;
        struct _M0TUsiE* _M0L6_2aoldS2247;
        int32_t _M0L6_2atmpS1208;
        if (
          _M0L6_2atmpS1207 < 0
          || _M0L6_2atmpS1207 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1206 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS1207];
        if (
          _M0L6_2atmpS1205 < 0
          || _M0L6_2atmpS1205 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2247 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS1205];
        if (_M0L6_2atmpS1206) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1206);
        }
        if (_M0L6_2aoldS2247) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2247);
        }
        _M0L3dstS45[_M0L6_2atmpS1205] = _M0L6_2atmpS1206;
        _M0L6_2atmpS1208 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1208;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  int32_t* _M0L3srcS55,
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
        int32_t _M0L6_2atmpS1210 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1212 = _M0L11src__offsetS57 + _M0L1iS58;
        int32_t _M0L6_2atmpS1211;
        int32_t _M0L6_2atmpS1213;
        if (
          _M0L6_2atmpS1212 < 0
          || _M0L6_2atmpS1212 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1211 = (int32_t)_M0L3srcS55[_M0L6_2atmpS1212];
        if (
          _M0L6_2atmpS1210 < 0
          || _M0L6_2atmpS1210 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1210] = _M0L6_2atmpS1211;
        _M0L6_2atmpS1213 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1213;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1218 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1218;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1214 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1216 = _M0L11src__offsetS57 + _M0L1iS61;
        int32_t _M0L6_2atmpS1215;
        int32_t _M0L6_2atmpS1217;
        if (
          _M0L6_2atmpS1216 < 0
          || _M0L6_2atmpS1216 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1215 = (int32_t)_M0L3srcS55[_M0L6_2atmpS1216];
        if (
          _M0L6_2atmpS1214 < 0
          || _M0L6_2atmpS1214 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1214] = _M0L6_2atmpS1215;
        _M0L6_2atmpS1217 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1217;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_30.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_31.data);
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS6) {
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1143) {
  switch (Moonbit_object_tag(_M0L4_2aeS1143)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1143);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1169,
  struct _M0TPB4Show _M0L8_2aparamS1168
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1167 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1169;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1167, _M0L8_2aparamS1168);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1166,
  struct _M0TPB4Show _M0L8_2aparamS1165
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1164 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1166;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1164, _M0L8_2aparamS1165);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1163,
  int32_t _M0L8_2aparamS1162
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1161 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1163;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1161, _M0L8_2aparamS1162);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1160,
  struct _M0TPC16string10StringView _M0L8_2aparamS1159
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1158 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1160;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1158, _M0L8_2aparamS1159);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1157,
  moonbit_string_t _M0L8_2aparamS1154,
  int32_t _M0L8_2aparamS1155,
  int32_t _M0L8_2aparamS1156
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1153 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1157;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1153, _M0L8_2aparamS1154, _M0L8_2aparamS1155, _M0L8_2aparamS1156);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1152,
  moonbit_string_t _M0L8_2aparamS1151
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1150 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1152;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1150, _M0L8_2aparamS1151);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2402 = 9218868437227405311ll;
  int64_t _tmp_2403;
  int64_t _tmp_2404;
  int64_t _tmp_2405;
  int64_t _tmp_2406;
  _M0FPB18double__max__value = *(double*)&_tmp_2402;
  _tmp_2403 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2403;
  _tmp_2404 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2404;
  _tmp_2405 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2405;
  _tmp_2406 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2406;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1173;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1136;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1137;
  int32_t _M0L7_2abindS1138;
  struct _M0TUsiE** _M0L7_2abindS1139;
  int32_t _M0L6_2acntS2252;
  int32_t _M0L2__S1140;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1173
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1136
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1136)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _M0L12async__testsS1136->$0 = _M0L6_2atmpS1173;
  _M0L12async__testsS1136->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1137
  = _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1138 = _M0L7_2abindS1137->$1;
  _M0L7_2abindS1139 = _M0L7_2abindS1137->$0;
  _M0L6_2acntS2252
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1137));
  if (_M0L6_2acntS2252 > 1) {
    int32_t _M0L11_2anew__cntS2253 = _M0L6_2acntS2252 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1137), _M0L11_2anew__cntS2253);
    moonbit_incref_cycle_free(_M0L7_2abindS1139);
  } else if (_M0L6_2acntS2252 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1137);
  }
  _M0L2__S1140 = 0;
  while (1) {
    if (_M0L2__S1140 < _M0L7_2abindS1138) {
      struct _M0TUsiE* _M0L3argS1141 =
        (struct _M0TUsiE*)_M0L7_2abindS1139[_M0L2__S1140];
      moonbit_string_t _M0L6_2atmpS1170 = _M0L3argS1141->$0;
      int32_t _M0L6_2atmpS1171 = _M0L3argS1141->$1;
      int32_t _M0L6_2atmpS1172;
      moonbit_incref_cycle_free(_M0L6_2atmpS1170);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1136, _M0L6_2atmpS1170, _M0L6_2atmpS1171);
      moonbit_decref_cycle_free(_M0L6_2atmpS1170);
      _M0L6_2atmpS1172 = _M0L2__S1140 + 1;
      _M0L2__S1140 = _M0L6_2atmpS1172;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1139);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\wilson_cowan\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29wilson__cowan__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1136);
  moonbit_decref_cycle_free(_M0L12async__testsS1136);
  moonbit_flush_cycles();
  return 0;
}