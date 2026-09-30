// import_data_test.mbt — tests for v0.42.1 import_data.mbt.

///|
test "parse_int_basic" {
  if parse_int("0") != 0 {
    fail("parse_int('0') = \{parse_int("0")}")
  }
  if parse_int("123") != 123 {
    fail("parse_int('123') = \{parse_int("123")}")
  }
  if parse_int("-456") != -456 {
    fail("parse_int('-456') = \{parse_int("-456")}")
  }
  if parse_int("+789") != 789 {
    fail("parse_int('+789') = \{parse_int("+789")}")
  }
}

///|
test "parse_int_trailing_garbage_stops_at_first_non_digit" {
  // "42abc" parses as 42.
  if parse_int("42abc") != 42 {
    fail("parse_int('42abc') = \{parse_int("42abc")}")
  }
  // "abc" parses as 0 (no leading digit).
  if parse_int("abc") != 0 {
    fail("parse_int('abc') = \{parse_int("abc")}")
  }
  // "" parses as 0.
  if parse_int("") != 0 {
    fail("parse_int('') = \{parse_int("")}")
  }
}

///|
test "parse_float_basic" {
  if (parse_float("0.0") - 0.0F).abs() > 1.0e-6F {
    fail("parse_float('0.0') = \{parse_float("0.0")}")
  }
  if (parse_float("1.5") - 1.5F).abs() > 1.0e-6F {
    fail("parse_float('1.5') = \{parse_float("1.5")}")
  }
  if (parse_float("-2.75") - -2.75F).abs() > 1.0e-6F {
    fail("parse_float('-2.75') = \{parse_float("-2.75")}")
  }
  if (parse_float("3.14159") - 3.14159F).abs() > 1.0e-5F {
    fail("parse_float('3.14159') = \{parse_float("3.14159")}")
  }
}

///|
test "parse_float_with_exponent" {
  // "1e3" → 1000.0
  if (parse_float("1e3") - 1000.0F).abs() > 1.0e-3F {
    fail("parse_float('1e3') = \{parse_float("1e3")}")
  }
  // "1.5e-2" → 0.015
  if (parse_float("1.5e-2") - 0.015F).abs() > 1.0e-5F {
    fail("parse_float('1.5e-2') = \{parse_float("1.5e-2")}")
  }
  // "2E5" → 200000.0
  if (parse_float("2E5") - 200000.0F).abs() > 1.0F {
    fail("parse_float('2E5') = \{parse_float("2E5")}")
  }
}

///|
test "parse_float_no_decimal" {
  // "5" parses as 5.0
  if (parse_float("5") - 5.0F).abs() > 1.0e-6F {
    fail("parse_float('5') = \{parse_float("5")}")
  }
}

///|
test "parse_float_invalid_returns_zero" {
  // "" → 0
  if parse_float("") != 0.0F {
    fail("parse_float('') should be 0")
  }
  // "abc" → 0 (no leading digit)
  if parse_float("abc") != 0.0F {
    fail("parse_float('abc') should be 0")
  }
  // ".5" → 0.5 (Julia/Python accept leading-dot decimal).
  if (parse_float(".5") - 0.5F).abs() > 1.0e-6F {
    fail("parse_float for .5 should be 0.5")
  }
  // "-.5" → -0.5 (sign + leading-dot).
  if (parse_float("-.5") - -0.5F).abs() > 1.0e-6F {
    fail("parse_float for -.5 should be -0.5")
  }
}

///|
test "parse_csv_row_simple" {
  let row = parse_csv_row("1.5,2.5,3.5,4.5")
  if row.length() != 4 {
    fail("row.length() = \{row.length()}, expected 4")
  }
  let expected : Array[Float] = [1.5F, 2.5F, 3.5F, 4.5F]
  for i in 0..<row.length() {
    if (row[i] - expected[i]).abs() > 1.0e-5F {
      fail("row[\{i}] = \{row[i]}, expected \{expected[i]}")
    }
  }
}

///|
test "parse_csv_row_handles_whitespace" {
  let row = parse_csv_row(" 1.5 , 2.5 , 3.5 ")
  if row.length() != 3 {
    fail("row.length() = \{row.length()}, expected 3")
  }
  let expected : Array[Float] = [1.5F, 2.5F, 3.5F]
  for i in 0..<row.length() {
    if (row[i] - expected[i]).abs() > 1.0e-5F {
      fail("row[\{i}] = \{row[i]}, expected \{expected[i]}")
    }
  }
}

///|
test "parse_csv_row_handles_negative_and_exponents" {
  let row = parse_csv_row("-1.5,2e2,-3E-1")
  if row.length() != 3 {
    fail("row.length() = \{row.length()}, expected 3")
  }
  if (row[0] - -1.5F).abs() > 1.0e-5F {
    fail("row[0] = \{row[0]}, expected -1.5")
  }
  if (row[1] - 200.0F).abs() > 1.0e-3F {
    fail("row[1] = \{row[1]}, expected 200.0")
  }
  if (row[2] - -0.3F).abs() > 1.0e-5F {
    fail("row[2] = \{row[2]}, expected -0.3")
  }
}

///|
test "parse_csv_multi_row" {
  let text = "1.0,2.0,3.0\n4.0,5.0,6.0\n7.0,8.0,9.0"
  let grid = parse_csv(text)
  if grid.length() != 3 {
    fail("grid.length() = \{grid.length()}, expected 3")
  }
  if grid[0].length() != 3 || grid[1].length() != 3 || grid[2].length() != 3 {
    fail("row lengths not 3")
  }
  if (grid[0][0] - 1.0F).abs() > 1.0e-6F {
    fail("grid[0][0] = \{grid[0][0]}, expected 1.0")
  }
  if (grid[1][1] - 5.0F).abs() > 1.0e-6F {
    fail("grid[1][1] = \{grid[1][1]}, expected 5.0")
  }
  if (grid[2][2] - 9.0F).abs() > 1.0e-6F {
    fail("grid[2][2] = \{grid[2][2]}, expected 9.0")
  }
}

///|
test "parse_csv_handles_windows_line_endings" {
  let text = "1.0,2.0\r\n3.0,4.0\r\n5.0,6.0"
  let grid = parse_csv(text)
  if grid.length() != 3 {
    fail("grid.length() = \{grid.length()}, expected 3 (CRLF handled)")
  }
  if (grid[1][0] - 3.0F).abs() > 1.0e-6F {
    fail("grid[1][0] = \{grid[1][0]}, expected 3.0")
  }
}

///|
test "parse_csv_handles_blank_lines" {
  let text = "1.0,2.0\n\n3.0,4.0\n"
  let grid = parse_csv(text)
  if grid.length() != 2 {
    fail("grid.length() = \{grid.length()}, expected 2 (blank lines skipped)")
  }
}

///|
test "parse_csv_uneven_rows" {
  // Row 1 has 2 cols, row 2 has 3 cols.
  let text = "1.0,2.0\n3.0,4.0,5.0"
  let grid = parse_csv(text)
  if grid.length() != 2 {
    fail("grid.length() = \{grid.length()}, expected 2")
  }
  if grid[0].length() != 2 {
    fail("row 0 length = \{grid[0].length()}, expected 2")
  }
  if grid[1].length() != 3 {
    fail("row 1 length = \{grid[1].length()}, expected 3")
  }
}

///|
test "parse_int_list_basic" {
  let ints = parse_int_list("1,2,3,4,5")
  if ints.length() != 5 {
    fail("ints.length() = \{ints.length()}, expected 5")
  }
  let expected : Array[Int] = [1, 2, 3, 4, 5]
  for i in 0..<ints.length() {
    if ints[i] != expected[i] {
      fail("ints[\{i}] = \{ints[i]}, expected \{expected[i]}")
    }
  }
}

///|
test "parse_int_list_handles_negatives" {
  let ints = parse_int_list("-1,-2,-3")
  if ints.length() != 3 {
    fail("ints.length() = \{ints.length()}, expected 3")
  }
  let expected : Array[Int] = [-1, -2, -3]
  for i in 0..<ints.length() {
    if ints[i] != expected[i] {
      fail("ints[\{i}] = \{ints[i]}, expected \{expected[i]}")
    }
  }
}