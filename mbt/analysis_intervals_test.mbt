// analysis_intervals_test.mbt — tests for analysis_intervals.mbt.

///|
test "time_in_interval — empty intervals returns false" {
  let intervals : Array[Array[Float]] = []
  if time_in_interval(5.0F, intervals) { fail("empty intervals should return false") }
}

///|
test "time_in_interval — x at start of interval (inclusive)" {
  let intervals : Array[Array[Float]] = [[0.0F, 10.0F], [20.0F, 30.0F]]
  if !time_in_interval(0.0F, intervals) { fail("x=0 in [0,10] should be true") }
}

///|
test "time_in_interval — x at end of interval (inclusive)" {
  let intervals : Array[Array[Float]] = [[0.0F, 10.0F]]
  if !time_in_interval(10.0F, intervals) { fail("x=10 in [0,10] should be true") }
}

///|
test "time_in_interval — x in middle of second interval" {
  let intervals : Array[Array[Float]] = [[0.0F, 10.0F], [20.0F, 30.0F]]
  if !time_in_interval(25.0F, intervals) { fail("x=25 in [20,30] should be true") }
}

///|
test "time_in_interval — x outside all intervals" {
  let intervals : Array[Array[Float]] = [[0.0F, 10.0F], [20.0F, 30.0F]]
  if time_in_interval(15.0F, intervals) { fail("x=15 not in any should be false") }
  if time_in_interval(35.0F, intervals) { fail("x=35 not in any should be false") }
  if time_in_interval(-1.0F, intervals) { fail("x=-1 not in any should be false") }
}

///|
test "start_interval — returns start of containing interval" {
  let intervals : Array[Array[Float]] = [[5.0F, 15.0F], [20.0F, 30.0F]]
  let s = start_interval(7.0F, intervals)
  if s != 5.0F { fail("expected 5.0F, got \{s}") }
  let s2 = start_interval(25.0F, intervals)
  if s2 != 20.0F { fail("expected 20.0F, got \{s2}") }
}

///|
test "start_interval — returns -1.0F if no interval contains x" {
  let intervals : Array[Array[Float]] = [[0.0F, 10.0F]]
  let s = start_interval(15.0F, intervals)
  if s != -1.0F { fail("expected -1.0F, got \{s}") }
}

///|
test "end_interval — returns end of containing interval" {
  let intervals : Array[Array[Float]] = [[5.0F, 15.0F], [20.0F, 30.0F]]
  let e = end_interval(7.0F, intervals)
  if e != 15.0F { fail("expected 15.0F, got \{e}") }
  let e2 = end_interval(25.0F, intervals)
  if e2 != 30.0F { fail("expected 30.0F, got \{e2}") }
}