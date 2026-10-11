// Tests for get_synapse_symbol.

///|
test "get_synapse_symbol — glu/gaba pass-through" {
  assert_eq(@snn_mbt.get_synapse_symbol("glu"), "glu")
  assert_eq(@snn_mbt.get_synapse_symbol("gaba"), "gaba")
}

///|
test "get_synapse_symbol — he/ge → glu" {
  assert_eq(@snn_mbt.get_synapse_symbol("he"), "glu")
  assert_eq(@snn_mbt.get_synapse_symbol("ge"), "glu")
}

///|
test "get_synapse_symbol — hi/gi → gaba" {
  assert_eq(@snn_mbt.get_synapse_symbol("hi"), "gaba")
  assert_eq(@snn_mbt.get_synapse_symbol("gi"), "gaba")
}

///|
test "get_synapse_symbol — unknown symbol passthrough" {
  assert_eq(@snn_mbt.get_synapse_symbol("foo"), "foo")
  assert_eq(@snn_mbt.get_synapse_symbol("xyz"), "xyz")
  assert_eq(@snn_mbt.get_synapse_symbol(""), "")
}