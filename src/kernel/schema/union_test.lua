-- The step union is an allOf of if/then discriminated by uses: (ADR-0009), so
-- each plugin's with: is checked only on the steps that use that plugin.
local cbor = require("qwe.cbor")
local plugins = require("qwe.plugins")
local validate = require("qwe.validate")

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

local function fail(msg) error(msg, 0) end

local function if_then_discriminator()
  local errors = validate.validate(workflow({
    step("file.ensure", { path = "/tmp/x" }),
    step("other.plugin", { x = 1 }),
  }), list)
  if #errors ~= 0 then fail("valid with: rejected: " .. errors[1].message) end

  -- x: is valid for other.plugin, and not a key file.ensure knows
  errors = validate.validate(workflow({ step("file.ensure", { x = 1, path = "/tmp/x" }) }), list)
  if #errors ~= 1 or errors[1].pointer ~= "/jobs/j/steps/0/with/x" then
    fail("file.ensure with an unknown key: expected one error at .../with/x, got " .. #errors)
  end

  errors = validate.validate(workflow({ step("other.plugin", { x = "text" }) }), list)
  if #errors ~= 1 or errors[1].pointer ~= "/jobs/j/steps/0/with/x" then
    fail("other.plugin with a wrong type: expected one error at .../with/x, got " .. #errors)
  end
end

local ok, err = pcall(if_then_discriminator)
if not ok then fail("if_then_discriminator: " .. tostring(err)) end
print("ok   if_then_discriminator")
