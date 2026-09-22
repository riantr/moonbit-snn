// analysis_populations_test.mbt — port of
// `refs/SNNModels.jl/test/analysis/populations_test.jl`.
//
// Three Julia test cases, mapped to MoonBit's flat-array container:
//   1. `population_indices` — non-overlapping 1-based index ranges
//   2. `filter_items` — default (drop noise*) + custom (N > 7)
//   3. `average_conn_strength` — block-mean of a dense weight matrix

///|
test "population_indices — 10 + 5 → E=1..10, I=11..15" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
  ]
  let idx = population_indices(pops)
  assert_eq(idx.length(), 2)
  // E: 1..10
  assert_eq(idx[0].name, "E")
  assert_eq(idx[0].start, 1)
  assert_eq(idx[0].end_, 10)
  // I: 11..15
  assert_eq(idx[1].name, "I")
  assert_eq(idx[1].start, 11)
  assert_eq(idx[1].end_, 15)
}

///|
test "population_indices — non-overlapping ranges cover 1..N_total" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
  ]
  let idx = population_indices(pops)
  // Ranges non-overlapping.
  let e_end = idx[0].end_
  let i_start = idx[1].start
  // E ends at 10, I starts at 11: non-overlapping.
  assert_eq(e_end + 1, i_start)
  // Union covers 1..15.
  let total = idx[0].length() + idx[1].length()
  assert_eq(total, 15)
}

///|
test "population_indices — three pops (E, I, PV) covers 1..20" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
    ("PV", 5),
  ]
  let idx = population_indices(pops)
  assert_eq(idx.length(), 3)
  assert_eq(idx[2].name, "PV")
  assert_eq(idx[2].start, 16)
  assert_eq(idx[2].end_, 20)
  let total = idx[0].length() + idx[1].length() + idx[2].length()
  assert_eq(total, 20)
}

///|
test "population_indices — single pop starts at 1" {
  let pops : Array[(String, Int)] = [
    ("E", 8),
  ]
  let idx = population_indices(pops)
  assert_eq(idx.length(), 1)
  assert_eq(idx[0].start, 1)
  assert_eq(idx[0].end_, 8)
  assert_eq(idx[0].length(), 8)
}

///|
test "filter_items — default drops 'noise_*' labels" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("noise_pop", 5),
    ("noise_input", 3),
    ("I", 7),
  ]
  let filtered = filter_items(pops)
  // E and I remain; both noise pops are dropped.
  assert_eq(filtered.length(), 2)
  assert_eq(filtered[0].0, "E")
  assert_eq(filtered[1].0, "I")
}

///|
test "filter_items_with — DropNoise equivalent to default" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("noise_pop", 5),
    ("I", 7),
  ]
  let filtered = filter_items_with(pops, DropNoise)
  assert_eq(filtered.length(), 2)
  assert_eq(filtered[0].0, "E")
  assert_eq(filtered[1].0, "I")
}

///|
test "filter_items_with — Greater(7) keeps only N > 7" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
  ]
  let filtered = filter_items_with(pops, Greater(7))
  // E (10) kept, I (5) dropped.
  assert_eq(filtered.length(), 1)
  assert_eq(filtered[0].0, "E")
}

///|
test "filter_items_with — Less(7) keeps only N < 7" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
  ]
  let filtered = filter_items_with(pops, Less(7))
  // I (5) kept, E (10) dropped.
  assert_eq(filtered.length(), 1)
  assert_eq(filtered[0].0, "I")
}

///|
test "filter_items_with — Equal(5) keeps only N == 5" {
  let pops : Array[(String, Int)] = [
    ("E", 10),
    ("I", 5),
    ("PV", 5),
  ]
  let filtered = filter_items_with(pops, Equal(5))
  // I (5) and PV (5) kept, E (10) dropped.
  assert_eq(filtered.length(), 2)
  assert_eq(filtered[0].0, "I")
  assert_eq(filtered[1].0, "PV")
}

///|
test "average_conn_strength — A→A block mean / μ" {
  // 3×3 dense weight matrix:
  //   [1 2 0]
  //   [3 4 0]
  //   [0 0 5]
  // pops = [[1, 2], [3]]  (A = rows 1-2, B = row 3)
  let m : Array[Array[Float]] = [
    [1.0F, 2.0F, 0.0F],
    [3.0F, 4.0F, 0.0F],
    [0.0F, 0.0F, 5.0F],
  ]
  let pops : Array[PopIndex] = [
    { name: "A", start: 1, end_: 2 },
    { name: "B", start: 3, end_: 3 },
  ]
  let result = average_conn_strength(m, pops, 1.0F)
  // A→A: mean of [[1,2],[3,4]] = (1+2+3+4)/4 = 2.5
  assert_eq(result.length(), 2)
  assert_eq(result[0].length(), 2)
  let diff_aa = (result[0][0] - 2.5F).abs()
  assert_true(diff_aa < 1.0e-4)
  // B→B: M[3,3] = 5.0
  let diff_bb = (result[1][1] - 5.0F).abs()
  assert_true(diff_bb < 1.0e-4)
  // A→B: M[[1,2], [3]] = [0, 0], mean = 0
  assert_eq(result[0][1], 0.0F)
  // B→A: M[[3], [1,2]] = [0, 0], mean = 0
  assert_eq(result[1][0], 0.0F)
}

///|
test "average_conn_strength — μ = 2.0 halves the means" {
  let m : Array[Array[Float]] = [
    [1.0F, 2.0F, 0.0F],
    [3.0F, 4.0F, 0.0F],
    [0.0F, 0.0F, 5.0F],
  ]
  let pops : Array[PopIndex] = [
    { name: "A", start: 1, end_: 2 },
    { name: "B", start: 3, end_: 3 },
  ]
  let result = average_conn_strength(m, pops, 2.0F)
  // A→A: 2.5 / 2.0 = 1.25
  let diff = (result[0][0] - 1.25F).abs()
  assert_true(diff < 1.0e-4)
  // B→B: 5.0 / 2.0 = 2.5
  let diff2 = (result[1][1] - 2.5F).abs()
  assert_true(diff2 < 1.0e-4)
}

///|
test "average_conn_strength — single pop, all-zeros block" {
  let m : Array[Array[Float]] = [
    [0.0F, 0.0F],
    [0.0F, 0.0F],
  ]
  let pops : Array[PopIndex] = [
    { name: "A", start: 1, end_: 2 },
  ]
  let result = average_conn_strength(m, pops, 1.0F)
  assert_eq(result.length(), 1)
  assert_eq(result[0].length(), 1)
  // Empty-block fallback: 0.0F (sum=0, count=4 → avg=0).
  assert_eq(result[0][0], 0.0F)
}

///|
test "PopIndex::length — end - start + 1" {
  let p1 : PopIndex = { name: "X", start: 1, end_: 5 }
  assert_eq(p1.length(), 5)
  let p2 : PopIndex = { name: "Y", start: 6, end_: 10 }
  assert_eq(p2.length(), 5)
  let p3 : PopIndex = { name: "Z", start: 11, end_: 11 }
  assert_eq(p3.length(), 1)
}