-- The step union with two plugins: a bad with: on one plugin's step must give
-- exactly one error, at the offending key, with nothing from the other branch.
local cbor = require("qwe.cbor")
local plugins = require("qwe.plugins")
local validate = require("qwe.validate")

local function fail(msg) error(msg, 0) end

local list = plugins.builtin()
list[#list + 1] = {
  name = "other.plugin",
  schema = plugins.decode_schema('{"type":"object","required":["x"],"additionalProperties":false,"properties":{"x":{"type":"integer"}}}'),
}

local function workflow(steps)
  return cbor.map({ jobs = cbor.map({ j = cbor.map({ target = "local", steps = cbor.array(steps) }) }) })
end

local function step(uses, with)
  return cbor.map({ uses = uses, with = cbor.map(with) })
end

-- valid for each plugin
local errors = validate.validate(workflow({
  step("file.ensure", { path = "/tmp/x" }),
  step("other.plugin", { x = 1 }),
}), list)
if #errors ~= 0 then fail("expected no errors, got: " .. errors[1].message) end

-- a bad key on file.ensure: one error, at that key
errors = validate.validate(workflow({
  step("other.plugin", { x = 1 }),
  step("file.ensure", { path = "/tmp/x", contnet = "oops" }),
}), list)
if #errors ~= 1 then
  local msgs = {}
  for _, e in ipairs(errors) do msgs[#msgs + 1] = e.pointer .. " " .. e.message end
  fail("expected exactly one error, got " .. #errors .. ": " .. table.concat(msgs, "; "))
end
if errors[1].pointer ~= "/jobs/j/steps/1/with/contnet" or errors[1].kind ~= "key" then
  fail("wrong location: " .. errors[1].pointer .. " (" .. errors[1].kind .. ")")
end

-- and a wrong type for the other plugin's input: also exactly one error, at the value
errors = validate.validate(workflow({
  step("file.ensure", { path = "/tmp/x" }),
  step("other.plugin", { x = "not a number" }),
}), list)
if #errors ~= 1 or errors[1].pointer ~= "/jobs/j/steps/1/with/x" or errors[1].kind ~= "value" then
  fail("expected one error at /jobs/j/steps/1/with/x, got " .. #errors)
end

-- a required with: key that is missing entirely is caught even with no with: block
errors = validate.validate(cbor.map({ jobs = cbor.map({ j = cbor.map({
  target = "local",
  steps = cbor.array({ cbor.map({ uses = "other.plugin" }) }),
}) }) }), list)
if #errors ~= 1 or not errors[1].message:find("with", 1, true) then
  fail("a plugin with required inputs needs a with: block")
end
print("validate_test: ok")
