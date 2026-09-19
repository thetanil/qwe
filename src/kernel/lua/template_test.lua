-- qwe.template: the $QWE_OUTPUT format, evaluation and the checks.
local cbor = require("qwe.cbor")
local template = require("qwe.template")

local function eq(want, got, what)
  if want ~= got then error(string.format("%s: want %q, got %q", what, tostring(want), tostring(got)), 2) end
end

-- GitHub's format: key=value, and key<<DELIM ... DELIM for several lines.
local out = template.parse_output("a=1\nb=two words\nc<<EOT\nl1\n\nl3\nEOT\nd=x=y\nnot a line\nunclosed<<X\nlost\n")
eq("1", out.a, "a")
eq("two words", out.b, "b")
eq("l1\n\nl3", out.c, "multi-line value with a blank line")
eq("x=y", out.d, "value containing =")
eq(nil, out.unclosed, "an unterminated block is dropped")
eq("v", template.parse_output("k=v\r\n").k, "CRLF")
eq("", template.parse_output("k=\n").k, "empty value")

-- resolve: the innermost env wins, and templates are evaluated.
local wf = cbor.map({ env = cbor.map({ A = "wf", B = "wf", N = 3 }) })
local job = cbor.map({
  env = cbor.map({ B = "job" }),
  steps = cbor.array({
    cbor.map({ id = "one", run = "echo" }),
    cbor.map({
      run = "echo ${{ steps.one.outputs.k }} ${{ env.B }} ${{ steps.one.outputs.missing }}.",
      env = cbor.map({ B = "step", FROM = "${{ steps.one.outputs.k }}" }),
    }),
  }),
})
local step = template.resolve(wf, job, 2, { one = { k = "val" } }, nil)
eq("echo val step .", step.run, "run text")
eq("wf", step.env.A, "workflow env")
eq("step", step.env.B, "step env overrides job and workflow")
eq("3", step.env.N, "numbers become strings")
eq("val", step.env.FROM, "a steps reference in an env value")
eq(nil, step.env.QWE_OUTPUT, "QWE_OUTPUT only with an output path")
eq("/o", template.resolve(wf, job, 1, {}, "/o").env.QWE_OUTPUT, "QWE_OUTPUT for a run: step")

-- check: what a workflow may reference.
local function problems(steps_a, steps_b, env)
  local doc = cbor.map({
    env = env,
    jobs = cbor.map({
      a = cbor.map({ target = "local", steps = cbor.array(steps_a) }),
      b = cbor.map({ target = "local", steps = cbor.array(steps_b or { cbor.map({ run = "x" }) }) }),
    }),
  })
  return template.check(doc, {})
end
eq(0, #problems({ cbor.map({ id = "s", run = "x" }), cbor.map({ run = "${{ steps.s.outputs.k }}" }) }), "a later step reads")
eq(1, #problems({ cbor.map({ run = "${{ steps.s.outputs.k }}", id = "s" }) }), "a step cannot read its own outputs")
eq(1, #problems({ cbor.map({ id = "s", run = "x" }) }, { cbor.map({ run = "${{ steps.s.outputs.k }}" }) }), "another job")
eq(1, #problems({ cbor.map({ run = "${{ secrets.X }}" }) }), "secrets are not available yet")
eq(1, #problems({ cbor.map({ run = "${{ env.A.b }}" }) }), "a malformed env reference")
eq(1, #problems({ cbor.map({ run = "x", env = cbor.map({ ["BAD NAME"] = "v" }) }) }), "env name")
eq(1, #problems({ cbor.map({ run = "x", env = cbor.map({ A = "${{ env.B }}" }) }) }), "env value reading env")
print("ok   template")
