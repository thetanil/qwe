# The YAML→CBOR transcoder keeps source positions from the start

The kernel works only on CBOR. Once YAML is transcoded, line and column numbers are gone, unless the transcoder keeps them. `qwe serve` will later include an LSP server for workflow YAML, and every diagnostic it shows needs a position. Adding positions after the fact would mean changing every validation error path in the kernel and every plugin. So the transcoder keeps a side table from each CBOR node to its `line:column` from M1 onwards, and every validation error must report `file:line:col`.
