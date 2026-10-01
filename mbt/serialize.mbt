// serialize.mbt — model + optimizer checkpoint serialization (v0.56.0).
//
// Round-trip-stable serialization for DQN / DDPG / TD3 model parameters
// and Adam optimizer state. Output is a lightweight key=value text
// format that can be persisted to disk by the caller (no file-I/O
// coupling).
//
// Design choices:
//   * Pure-functional: serialize_* returns a String; deserialize_*
//     takes a String. Caller handles file I/O via MoonBit's @fs or
//     by piping to `moon run`.
//   * Float values use `Float::to_string` (sufficient for same-process
//     round-trip; MoonBit's Float32 printer is stable enough for the
//     project's tolerance).
//   * 1D arrays encoded as `[v0;v1;v2;...]` (semicolon-separated).
//     2D arrays use the same semicolon split, with rows separated by
//     `|` inside the brackets: `[r0c0;r0c1|r1c0;r1c1]`.
//     Both semicolon and pipe are non-numeric characters that never
//     appear in `Float::to_string` output, so the format is safe to
//     embed inside our `key=value\n` line-oriented header.
//   * Header is parsed line-by-line (split on '\n'). Each key=value
//     pair occupies exactly one line. There is no nested structure
//     in the header; the matrix body is fully self-contained.
//
// API:
//   serialize_linear_qnet(q)              -> String
//   deserialize_linear_qnet(s)            -> LinearQNet
//   serialize_qnetwork_continuous(q)      -> String
//   deserialize_qnetwork_continuous(s)    -> QNetworkContinuous
//   serialize_deterministic_policy(p)     -> String
//   deserialize_deterministic_policy(s)   -> DeterministicPolicy
//   serialize_adam_state(opt)             -> String
//   deserialize_adam_state(s)             -> AdamState
//   serialize_qnet_checkpoint(q, opt, k)  -> String
//   deserialize_qnet_checkpoint(s)        -> (LinearQNet, AdamState, Int)

// ───────────────────────────────────────────────────────────────────
// Low-level string helpers
// ───────────────────────────────────────────────────────────────────

///|
/// Separator used inside 1D arrays: ';' (never appears in
/// Float::to_string output).
const SEP_1D : Char = ';'

///|
/// Separator used inside 2D arrays for row boundaries: '|'.
const SEP_2D_ROW : Char = '|'

///|
/// Encode an Array[Float] as `[v0;v1;...]` using bit-exact hex
/// representation via `Float::reinterpret_as_int`. This guarantees
/// that the round-trip `decode_float_list(encode_float_list(x))` is
/// bit-identical to `x` even for Float32 values that do not have a
/// short decimal representation (e.g. 0.1F).
fn encode_float_list(xs : Array[Float]) -> String {
  let buf = StringBuilder::new()
  buf.write_char('[')
  for i in 0..<xs.length() {
    if i > 0 {
      buf.write_char(SEP_1D)
    }
    let bits = Float::reinterpret_as_int(xs[i])
    buf.write_string(bits.to_string())
  }
  buf.write_char(']')
  buf.to_string()
}

///|
/// Decode a `[v0;v1;...]` string back into Array[Float] by stripping
/// brackets, splitting on ';', and reinterpreting each Int as a Float
/// via `Float::reinterpret_from_int`. The Int representation is the
/// bit-exact Float32 bits (output of `Float::reinterpret_as_int`).
fn decode_float_list(s : String) -> Array[Float] {
  let inner = strip_brackets(s)
  if inner.length() == 0 {
    return []
  }
  let strs = split_on(inner, SEP_1D)
  let out : Array[Float] = Array::make(strs.length(), 0.0F)
  for i in 0..<strs.length() {
    let bits = parse_int(strs[i])
    out[i] = Float::reinterpret_from_int(bits)
  }
  out
}

///|
/// Encode a 2D float matrix as `[r0c0;r0c1|r1c0;r1c1|...]` using
/// bit-exact Int representation of each Float. Rows are joined by '|'
/// so the entire matrix fits on one line.
fn encode_float_matrix(xs : Array[Array[Float]]) -> String {
  let buf = StringBuilder::new()
  buf.write_char('[')
  for i in 0..<xs.length() {
    if i > 0 {
      buf.write_char(SEP_2D_ROW)
    }
    let row = xs[i]
    for j in 0..<row.length() {
      if j > 0 {
        buf.write_char(SEP_1D)
      }
      let bits = Float::reinterpret_as_int(row[j])
      buf.write_string(bits.to_string())
    }
  }
  buf.write_char(']')
  buf.to_string()
}

///|
/// Decode a 2D float matrix string. Rows separated by '|', values by
/// ';'. Empty matrix `[]` returns `[]`.
fn decode_float_matrix(s : String) -> Array[Array[Float]] {
  let inner = strip_brackets(s)
  if inner.length() == 0 {
    return []
  }
  let row_strs = split_on(inner, SEP_2D_ROW)
  let out : Array[Array[Float]] = Array::make(row_strs.length(), [])
  for i in 0..<row_strs.length() {
    let row_str = row_strs[i]
    if row_str.length() == 0 {
      out[i] = []
    } else {
      let val_strs = split_on(row_str, SEP_1D)
      let row : Array[Float] = Array::make(val_strs.length(), 0.0F)
      for j in 0..<val_strs.length() {
        row[j] = Float::reinterpret_from_int(parse_int(val_strs[j]))
      }
      out[i] = row
    }
  }
  out
}

///|
/// Parse a Float with stricter semantics than `parse_float` from
/// import_data.mbt: skip leading/trailing whitespace and reject
/// completely-empty strings (return 0.0F so the caller can detect
/// via shape mismatch). We deliberately use `parse_float` which
/// already handles leading sign, integer part, decimal part, and 'e'
/// exponent.
fn parse_float_strict(s : String) -> Float {
  // Trim leading and trailing whitespace.
  let n = s.length()
  let mut lo = 0
  let mut hi = n
  while lo < hi && (s[lo] == ' ' || s[lo] == '\t') {
    lo = lo + 1
  }
  while hi > lo && (s[hi - 1] == ' ' || s[hi - 1] == '\t') {
    hi = hi - 1
  }
  if hi == lo {
    return 0.0F
  }
  let buf = StringBuilder::new()
  for i in lo..<hi {
    buf.write_char(s[i].unsafe_to_char())
  }
  parse_float(buf.to_string())
}

///|
/// Split a string on a single character. Returns an Array of
/// non-empty pieces (empty pieces are dropped).
fn split_on(s : String, sep : Char) -> Array[String] {
  let out : Array[String] = []
  let buf = StringBuilder::new()
  let n = s.length()
  let mut i = 0
  while i < n {
    let c = s[i]
    let cc : Char = c.unsafe_to_char()
    if cc == sep {
      let piece = buf.to_string()
      buf.reset()
      if piece.length() > 0 {
        out.push(piece)
      }
    } else {
      buf.write_char(c.unsafe_to_char())
    }
    i = i + 1
  }
  let trailing = buf.to_string()
  if trailing.length() > 0 {
    out.push(trailing)
  }
  out
}

///|
/// Strip the leading '[' and trailing ']' if both present; trim
/// surrounding whitespace. Returns the inner content.
fn strip_brackets(s : String) -> String {
  let n = s.length()
  if n < 2 {
    return s
  }
  let mut lo = 0
  let mut hi = n
  // Trim leading whitespace.
  while lo < hi && (s[lo] == ' ' || s[lo] == '\t' || s[lo] == '\n' || s[lo] == '\r') {
    lo = lo + 1
  }
  // Trim trailing whitespace.
  while hi > lo && (s[hi - 1] == ' ' || s[hi - 1] == '\t' || s[hi - 1] == '\n' || s[hi - 1] == '\r') {
    hi = hi - 1
  }
  if hi - lo >= 2 && s[lo] == '[' && s[hi - 1] == ']' {
    lo = lo + 1
    hi = hi - 1
  }
  let buf = StringBuilder::new()
  for i in lo..<hi {
    buf.write_char(s[i].unsafe_to_char())
  }
  buf.to_string()
}

// ───────────────────────────────────────────────────────────────────
// Key/value header parsing
// ───────────────────────────────────────────────────────────────────

///|
/// Look up a key in the SNNCKPT v1 header. Returns the value (after
/// the first '=' up to end of line, trimmed) or "" if absent.
fn header_lookup(lines : Array[String], key : String) -> String {
  let prefix = key + "="
  for i in 0..<lines.length() {
    let line = lines[i]
    if line.length() >= prefix.length() {
      let mut match_ok = true
      for j in 0..<prefix.length() {
        if line[j] != prefix[j] {
          match_ok = false
          break
        }
      }
      if match_ok {
        let buf = StringBuilder::new()
        for j in prefix.length()..<line.length() {
          buf.write_char(line[j].unsafe_to_char())
        }
        return buf.to_string()
      }
    }
  }
  ""
}

///|
/// Split a SNNCKPT body into lines (strip leading "SNNCKPT v1" header
/// line if present). Lines are separated by '\n' (also handles '\r\n').
fn split_header_lines(s : String) -> Array[String] {
  let out : Array[String] = []
  let n = s.length()
  let buf = StringBuilder::new()
  for i in 0..<n {
    let c = s[i]
    if c == '\n' {
      let line = buf.to_string()
      buf.reset()
      // Skip the magic header line so consumers only see key=value lines.
      if line.length() >= 9 && line[0] == 'S' && line[1] == 'N' && line[2] == 'N' &&
         line[3] == 'C' && line[4] == 'K' && line[5] == 'P' && line[6] == 'T' {
        continue
      }
      if line.length() > 0 {
        out.push(line)
      }
    } else if c != '\r' {
      buf.write_char(c.unsafe_to_char())
    }
  }
  let trailing = buf.to_string()
  if trailing.length() > 0 {
    if !(trailing.length() >= 9 && trailing[0] == 'S' && trailing[1] == 'N' &&
         trailing[2] == 'N' && trailing[3] == 'C' && trailing[4] == 'K' &&
         trailing[5] == 'P' && trailing[6] == 'T') {
      out.push(trailing)
    }
  }
  out
}

// ───────────────────────────────────────────────────────────────────
// Public API: model + optimizer serialize/deserialize
// ───────────────────────────────────────────────────────────────────

///|
/// Serialize a `LinearQNet` (DQN-style Q-network: W[n_actions, n_states],
/// b[n_actions]) to a String. Header `kind=LinearQNet` plus the four
/// weight/bias fields. Optimizer state is NOT included; use
/// `serialize_qnet_checkpoint` for a full checkpoint.
pub fn serialize_linear_qnet(q : LinearQNet) -> String {
  let buf = StringBuilder::new()
  buf.write_string("SNNCKPT v1\n")
  buf.write_string("kind=LinearQNet\n")
  buf.write_string("n_states=")
  buf.write_string(q.n_states.to_string())
  buf.write_string("\nn_actions=")
  buf.write_string(q.n_actions.to_string())
  buf.write_string("\nw=")
  buf.write_string(encode_float_matrix(q.w))
  buf.write_string("\nb=")
  buf.write_string(encode_float_list(q.b))
  buf.write_string("\n")
  buf.to_string()
}

///|
/// Inverse of `serialize_linear_qnet`. Constructs a new LinearQNet
/// whose weight and bias arrays are deep-copied from the encoded
/// payload, so subsequent mutation of either side is independent.
pub fn deserialize_linear_qnet(s : String) -> LinearQNet {
  let lines = split_header_lines(s)
  let kind = header_lookup(lines, "kind")
  if kind != "LinearQNet" {
    abort("deserialize_linear_qnet: expected kind=LinearQNet, got '" + kind + "'")
  }
  let n_states = parse_int(header_lookup(lines, "n_states"))
  let n_actions = parse_int(header_lookup(lines, "n_actions"))
  let w_src = decode_float_matrix(header_lookup(lines, "w"))
  let b_src = decode_float_list(header_lookup(lines, "b"))
  // Deep-copy into freshly-allocated arrays.
  let w : Array[Array[Float]] = Array::make(n_actions, [])
  for i in 0..<n_actions {
    let row : Array[Float] = Array::make(n_states, 0.0F)
    for j in 0..<n_states {
      row[j] = w_src[i][j]
    }
    w[i] = row
  }
  let b : Array[Float] = Array::make(n_actions, 0.0F)
  for i in 0..<n_actions {
    b[i] = b_src[i]
  }
  { n_states, n_actions, w, b }
}

///|
/// Serialize a `QNetworkContinuous` (DDPG/TD3 critic head: state+action
/// -> scalar Q). Header `kind=QNetworkContinuous` plus shape metadata
/// (state_dim, action_dim, hidden) and the four weight/bias fields.
pub fn serialize_qnetwork_continuous(q : QNetworkContinuous) -> String {
  let buf = StringBuilder::new()
  buf.write_string("SNNCKPT v1\n")
  buf.write_string("kind=QNetworkContinuous\n")
  buf.write_string("state_dim=")
  buf.write_string(q.state_dim.to_string())
  buf.write_string("\naction_dim=")
  buf.write_string(q.action_dim.to_string())
  buf.write_string("\nhidden=")
  buf.write_string(q.hidden.to_string())
  buf.write_string("\nw1=")
  buf.write_string(encode_float_matrix(q.w1))
  buf.write_string("\nb1=")
  buf.write_string(encode_float_list(q.b1))
  buf.write_string("\nw2=")
  buf.write_string(encode_float_list(q.w2))
  buf.write_string("\nb2=")
  buf.write_string(Float::reinterpret_as_int(q.b2).to_string())
  buf.write_string("\n")
  buf.to_string()
}

///|
/// Inverse of `serialize_qnetwork_continuous`.
pub fn deserialize_qnetwork_continuous(s : String) -> QNetworkContinuous {
  let lines = split_header_lines(s)
  let kind = header_lookup(lines, "kind")
  if kind != "QNetworkContinuous" {
    abort("deserialize_qnetwork_continuous: expected kind=QNetworkContinuous, got '" + kind + "'")
  }
  let state_dim = parse_int(header_lookup(lines, "state_dim"))
  let action_dim = parse_int(header_lookup(lines, "action_dim"))
  let hidden = parse_int(header_lookup(lines, "hidden"))
  let w1_src = decode_float_matrix(header_lookup(lines, "w1"))
  let b1_src = decode_float_list(header_lookup(lines, "b1"))
  let w2_src = decode_float_list(header_lookup(lines, "w2"))
  let b2_val = Float::reinterpret_from_int(parse_int(header_lookup(lines, "b2")))
  let w1 : Array[Array[Float]] = Array::make(hidden, [])
  for i in 0..<hidden {
    let row : Array[Float] = Array::make(state_dim + action_dim, 0.0F)
    for j in 0..<(state_dim + action_dim) {
      row[j] = w1_src[i][j]
    }
    w1[i] = row
  }
  let b1 : Array[Float] = Array::make(hidden, 0.0F)
  for i in 0..<hidden {
    b1[i] = b1_src[i]
  }
  let w2 : Array[Float] = Array::make(hidden, 0.0F)
  for i in 0..<hidden {
    w2[i] = w2_src[i]
  }
  { state_dim, action_dim, hidden, w1, b1, w2, b2: b2_val }
}

///|
/// Serialize a `DeterministicPolicy` (DDPG/TD3 actor head). Header
/// `kind=DeterministicPolicy` plus shape and squash range.
pub fn serialize_deterministic_policy(p : DeterministicPolicy) -> String {
  let buf = StringBuilder::new()
  buf.write_string("SNNCKPT v1\n")
  buf.write_string("kind=DeterministicPolicy\n")
  buf.write_string("state_dim=")
  buf.write_string(p.state_dim.to_string())
  buf.write_string("\naction_dim=")
  buf.write_string(p.action_dim.to_string())
  buf.write_string("\nhidden=")
  buf.write_string(p.hidden.to_string())
  buf.write_string("\nw1=")
  buf.write_string(encode_float_matrix(p.w1))
  buf.write_string("\nb1=")
  buf.write_string(encode_float_list(p.b1))
  buf.write_string("\nw2=")
  buf.write_string(encode_float_matrix(p.w2))
  buf.write_string("\nb2=")
  buf.write_string(encode_float_list(p.b2))
  buf.write_string("\naction_low=")
  buf.write_string(Float::reinterpret_as_int(p.action_low).to_string())
  buf.write_string("\naction_high=")
  buf.write_string(Float::reinterpret_as_int(p.action_high).to_string())
  buf.write_string("\n")
  buf.to_string()
}

///|
/// Inverse of `serialize_deterministic_policy`.
pub fn deserialize_deterministic_policy(s : String) -> DeterministicPolicy {
  let lines = split_header_lines(s)
  let kind = header_lookup(lines, "kind")
  if kind != "DeterministicPolicy" {
    abort("deserialize_deterministic_policy: expected kind=DeterministicPolicy, got '" + kind + "'")
  }
  let state_dim = parse_int(header_lookup(lines, "state_dim"))
  let action_dim = parse_int(header_lookup(lines, "action_dim"))
  let hidden = parse_int(header_lookup(lines, "hidden"))
  let w1_src = decode_float_matrix(header_lookup(lines, "w1"))
  let b1_src = decode_float_list(header_lookup(lines, "b1"))
  let w2_src = decode_float_matrix(header_lookup(lines, "w2"))
  let b2_src = decode_float_list(header_lookup(lines, "b2"))
  let action_low = Float::reinterpret_from_int(parse_int(header_lookup(lines, "action_low")))
  let action_high = Float::reinterpret_from_int(parse_int(header_lookup(lines, "action_high")))
  let w1 : Array[Array[Float]] = Array::make(hidden, [])
  for i in 0..<hidden {
    let row : Array[Float] = Array::make(state_dim, 0.0F)
    for j in 0..<state_dim {
      row[j] = w1_src[i][j]
    }
    w1[i] = row
  }
  let b1 : Array[Float] = Array::make(hidden, 0.0F)
  for i in 0..<hidden {
    b1[i] = b1_src[i]
  }
  let w2 : Array[Array[Float]] = Array::make(action_dim, [])
  for i in 0..<action_dim {
    let row : Array[Float] = Array::make(hidden, 0.0F)
    for j in 0..<hidden {
      row[j] = w2_src[i][j]
    }
    w2[i] = row
  }
  let b2 : Array[Float] = Array::make(action_dim, 0.0F)
  for i in 0..<action_dim {
    b2[i] = b2_src[i]
  }
  { state_dim, action_dim, hidden, w1, b1, w2, b2, action_low, action_high }
}

///|
/// Serialize an `AdamState` (first/second moments + step counter)
/// keyed by `kind=AdamState`. Bias moments are length 1 for scalar
/// critics (QNetworkContinuous.b2) so the array is preserved as-is.
pub fn serialize_adam_state(opt : AdamState) -> String {
  let buf = StringBuilder::new()
  buf.write_string("SNNCKPT v1\n")
  buf.write_string("kind=AdamState\n")
  buf.write_string("step=")
  buf.write_string(opt.step.to_string())
  buf.write_string("\nm_w=")
  buf.write_string(encode_float_list(opt.m_w))
  buf.write_string("\nv_w=")
  buf.write_string(encode_float_list(opt.v_w))
  buf.write_string("\nm_b=")
  buf.write_string(encode_float_list(opt.m_b))
  buf.write_string("\nv_b=")
  buf.write_string(encode_float_list(opt.v_b))
  buf.write_string("\n")
  buf.to_string()
}

///|
/// Inverse of `serialize_adam_state`. Deep-copies the four moment
/// arrays so subsequent updates to either side don't alias.
pub fn deserialize_adam_state(s : String) -> AdamState {
  let lines = split_header_lines(s)
  let kind = header_lookup(lines, "kind")
  if kind != "AdamState" {
    abort("deserialize_adam_state: expected kind=AdamState, got '" + kind + "'")
  }
  let step = parse_int(header_lookup(lines, "step"))
  let mw_src = decode_float_list(header_lookup(lines, "m_w"))
  let vw_src = decode_float_list(header_lookup(lines, "v_w"))
  let mb_src = decode_float_list(header_lookup(lines, "m_b"))
  let vb_src = decode_float_list(header_lookup(lines, "v_b"))
  let mw : Array[Float] = Array::make(mw_src.length(), 0.0F)
  for i in 0..<mw_src.length() {
    mw[i] = mw_src[i]
  }
  let vw : Array[Float] = Array::make(vw_src.length(), 0.0F)
  for i in 0..<vw_src.length() {
    vw[i] = vw_src[i]
  }
  let mb : Array[Float] = Array::make(mb_src.length(), 0.0F)
  for i in 0..<mb_src.length() {
    mb[i] = mb_src[i]
  }
  let vb : Array[Float] = Array::make(vb_src.length(), 0.0F)
  for i in 0..<vb_src.length() {
    vb[i] = vb_src[i]
  }
  { m_w: mw, v_w: vw, m_b: mb, v_b: vb, step }
}

// ───────────────────────────────────────────────────────────────────
// Combined checkpoint helpers
// ───────────────────────────────────────────────────────────────────

///|
/// Serialize a LinearQNet + AdamState + step count into a single
/// checkpoint string. Uses an outer header line `kind=QNetCheckpoint`
/// so the deserializer can recover the full tuple without separate
/// payloads. Multi-section format: outer header refers to the inner
/// model + opt via inline-encoded sub-blocks whose '\n' characters
/// are escaped as the literal two-char sequence "\n".
pub fn serialize_qnet_checkpoint(
  q : LinearQNet,
  opt : AdamState,
  step : Int,
) -> String {
  let buf = StringBuilder::new()
  buf.write_string("SNNCKPT v1\n")
  buf.write_string("kind=QNetCheckpoint\n")
  buf.write_string("model=")
  buf.write_string(encode_inline_block(serialize_linear_qnet(q)))
  buf.write_string("\nopt=")
  buf.write_string(encode_inline_block(serialize_adam_state(opt)))
  buf.write_string("\nstep=")
  buf.write_string(step.to_string())
  buf.write_string("\n")
  buf.to_string()
}

///|
/// Inverse of `serialize_qnet_checkpoint`. Reconstructs the model,
/// optimizer state, and the global training step counter.
pub fn deserialize_qnet_checkpoint(s : String) -> (LinearQNet, AdamState, Int) {
  let lines = split_header_lines(s)
  let kind = header_lookup(lines, "kind")
  if kind != "QNetCheckpoint" {
    abort("deserialize_qnet_checkpoint: expected kind=QNetCheckpoint, got '" + kind + "'")
  }
  let model = deserialize_linear_qnet(decode_inline_block(header_lookup(lines, "model")))
  let opt = deserialize_adam_state(decode_inline_block(header_lookup(lines, "opt")))
  let step = parse_int(header_lookup(lines, "step"))
  (model, opt, step)
}

///|
/// Encode an inner SNNCKPT block so it can be embedded in a single
/// key=value line: each '\n' becomes "\\n" so the outer parser sees
/// it as one line.
fn encode_inline_block(s : String) -> String {
  let buf = StringBuilder::new()
  for i in 0..<s.length() {
    let c = s[i]
    if c == '\n' {
      buf.write_string("\\n")
    } else if c == '\\' {
      buf.write_string("\\\\")
    } else {
      buf.write_char(c.unsafe_to_char())
    }
  }
  buf.to_string()
}

///|
/// Inverse of `encode_inline_block`.
fn decode_inline_block(s : String) -> String {
  let buf = StringBuilder::new()
  let n = s.length()
  let mut i = 0
  while i < n {
    let c = s[i]
    if c == '\\' && i + 1 < n {
      let nx = s[i + 1]
      if nx == 'n' {
        buf.write_char('\n')
        i = i + 2
        continue
      } else if nx == '\\' {
        buf.write_char('\\')
        i = i + 2
        continue
      }
    }
    buf.write_char(c.unsafe_to_char())
    i = i + 1
  }
  buf.to_string()
}
