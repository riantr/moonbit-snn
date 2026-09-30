// import_data.mbt — data import helpers (v0.42.1).
//
// Provides string → numeric parsers and CSV loaders. Complements
// `image.mbt` (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF via
// riantr/moonbit_image) by handling the structured-data formats.
//
// API:
//   parse_int(s)            → Int    (returns 0 on parse failure)
//   parse_float(s)          → Float  (returns 0.0F on parse failure;
//                                 supports leading sign + decimal +
//                                 optional 'e'/'E' exponent)
//   parse_csv_row(line)     → Array[Float] (single CSV row)
//   parse_csv(text)         → Array[Array[Float]] (multi-row CSV)
//   parse_int_list(text)    → Array[Int]    (single line, comma-separated)
//
// CSV format: rows separated by '\n' (also handles '\r\n'), columns
// by ','. Whitespace within fields is stripped. No header support —
// caller strips headers if needed.

///|
/// Parse a decimal string to Int. Returns 0 on parse failure.
pub fn parse_int(s : String) -> Int {
  let n = s.length()
  if n == 0 {
    return 0
  }
  let mut i = 0
  let mut sign = 1
  if s[0] == '-' {
    sign = -1
    i = 1
  } else if s[0] == '+' {
    i = 1
  }
  let mut acc : Int = 0
  let mut found_digit = false
  while i < n {
    let c = s[i]
    if c >= '0' && c <= '9' {
      acc = acc * 10 + (c.to_int() - '0'.to_int())
      found_digit = true
      i = i + 1
    } else {
      break
    }
  }
  if !found_digit {
    return 0
  }
  sign * acc
}

///|
/// Parse a decimal string to Float. Handles leading sign, integer part,
/// decimal part, and optional 'e'/'E' exponent. Returns 0.0F on
/// parse failure or empty input.
pub fn parse_float(s : String) -> Float {
  let n = s.length()
  if n == 0 {
    return 0.0F
  }
  let mut i = 0
  let mut sign : Float = 1.0F
  if s[0] == '-' {
    sign = -1.0F
    i = 1
  } else if s[0] == '+' {
    i = 1
  }
  let mut acc : Float = 0.0F
  let mut found_digit = false
  // Integer part.
  while i < n {
    let c = s[i]
    if c >= '0' && c <= '9' {
      acc = acc * 10.0F + Float::from_int(c.to_int() - '0'.to_int())
      found_digit = true
      i = i + 1
    } else {
      break
    }
  }
  // Decimal part.
  if i < n && s[i] == '.' {
    i = i + 1
    let mut frac : Float = 0.1F
    while i < n {
      let c = s[i]
      if c >= '0' && c <= '9' {
        acc = acc + Float::from_int(c.to_int() - '0'.to_int()) * frac
        frac = frac * 0.1F
        found_digit = true
        i = i + 1
      } else {
        break
      }
    }
  }
  if !found_digit {
    return 0.0F
  }
  // Exponent.
  if i < n && (s[i] == 'e' || s[i] == 'E') {
    i = i + 1
    let mut exp_sign : Int = 1
    if i < n && s[i] == '-' {
      exp_sign = -1
      i = i + 1
    } else if i < n && s[i] == '+' {
      i = i + 1
    }
    let mut exp : Int = 0
    while i < n {
      let c = s[i]
      if c >= '0' && c <= '9' {
        exp = exp * 10 + (c.to_int() - '0'.to_int())
        i = i + 1
      } else {
        break
      }
    }
    let factor : Float = if exp_sign == 1 {
      pow10_int(exp)
    } else {
      1.0F / pow10_int(exp)
    }
    acc = acc * factor
  }
  sign * acc
}

///|
/// Helper: 10^n as Float. Only handles small n.
fn pow10_int(n : Int) -> Float {
  let mut result = 1.0F
  let mut k = 0
  while k < n {
    result = result * 10.0F
    k = k + 1
  }
  result
}

///|
/// Parse a single CSV row into Array[Float]. Splits on ',', trims
/// whitespace, parses each field via `parse_float`.
pub fn parse_csv_row(line : String) -> Array[Float] {
  let fields = split_csv_line(line)
  let out : Array[Float] = Array::make(fields.length(), 0.0F)
  for i in 0..<fields.length() {
    out[i] = parse_float(fields[i])
  }
  out
}

///|
/// Split a CSV line on ',' into String fields. Iterates manually to
/// avoid String::split (which returns Iter, not Array).
fn split_csv_line(line : String) -> Array[String] {
  let out : Array[String] = []
  let buf = StringBuilder::new()
  let n = line.length()
  let mut i = 0
  while i < n {
    let c = line[i]
    if c == ',' {
      out.push(buf.to_string())
      buf.reset()
    } else if c != ' ' && c != '\t' {
      // Skip whitespace; only append non-whitespace.
      buf.write_char(c.unsafe_to_char())
    }
    i = i + 1
  }
  out.push(buf.to_string())
  out
}

///|
/// Parse multi-line CSV text into 2D Float array. Splits on '\n',
/// then each line via `parse_csv_row`. Handles '\r\n' (Windows).
pub fn parse_csv(text : String) -> Array[Array[Float]] {
  let out : Array[Array[Float]] = []
  let n = text.length()
  let buf = StringBuilder::new()
  let mut i = 0
  while i < n {
    let c = text[i]
    if c == '\n' {
      let line = buf.to_string()
      buf.reset()
      if line.length() > 0 {
        out.push(parse_csv_row(line))
      }
    } else if c != '\r' {
      buf.write_char(c.unsafe_to_char())
    }
    i = i + 1
  }
  // Trailing line (no newline at end).
  let trailing = buf.to_string()
  if trailing.length() > 0 {
    out.push(parse_csv_row(trailing))
  }
  out
}

///|
/// Parse a comma-separated Int list (single line).
pub fn parse_int_list(text : String) -> Array[Int] {
  let fields = split_csv_line(text)
  let out : Array[Int] = Array::make(fields.length(), 0)
  for i in 0..<fields.length() {
    out[i] = parse_int(fields[i])
  }
  out
}