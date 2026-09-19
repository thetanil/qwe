-- The qwe strict metaschema: draft-07's, with typos rejected, pattern removed
-- and secret added (ADR-0009).
local plugins = require("qwe.plugins")
local plugincheck = require("qwe.plugincheck")

local failures = 0

local function test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    print("ok   " .. name)
  else
    failures = failures + 1
    print("FAIL " .. name .. ": " .. tostring(err))
  end
end

local function errors_of(json)
  return plugincheck.metaschema_errors(plugins.decode_schema(json))
end

local function expect(list, pointer, kind)
  if #list ~= 1 then
    local seen = {}
    for _, e in ipairs(list) do seen[#seen + 1] = e.pointer .. " " .. e.message end
    error("expected one error at " .. pointer .. ", got " .. #list .. ": " .. table.concat(seen, "; "), 0)
  end
  if list[1].pointer ~= pointer or list[1].kind ~= kind then
    error("expected " .. kind .. " error at " .. pointer .. ", got " .. list[1].kind .. " at " .. list[1].pointer, 0)
  end
end

test("metaschema_rejects_typo", function()
  expect(errors_of('{"type":"object","requried":["x"]}'), "/requried", "key")
end)

test("rejects_nested_typo", function()
  expect(errors_of('{"properties":{"x":{"minLenght":1}}}'), "/properties/x/minLenght", "key")
end)

test("property_named_like_keyword_ok", function()
  local list = errors_of('{"type":"object","properties":{"requried":{"type":"string"}}}')
  if #list ~= 0 then error("unexpected: " .. list[1].pointer .. " " .. list[1].message, 0) end
end)

test("pattern_rejected", function()
  expect(errors_of('{"type":"string","pattern":"^a"}'), "/pattern", "key")
  expect(errors_of('{"patternProperties":{"^a":{}}}'), "/patternProperties", "key")
  expect(errors_of('{"type":"string","format":"uri"}'), "/format", "key")
end)

test("secret_allowed_and_typed", function()
  if #errors_of('{"type":"string","secret":true}') ~= 0 then error("secret: true was rejected", 0) end
  if #errors_of('{"type":"string","secret":"yes"}') == 0 then error("secret: \"yes\" was accepted", 0) end
end)

test("schema_pinned_to_draft_07", function()
  if #errors_of('{"$schema":"http://json-schema.org/draft-07/schema#","type":"string"}') ~= 0 then
    error("draft-07 was rejected", 0)
  end
  if #errors_of('{"$schema":"http://json-schema.org/draft-04/schema#","type":"string"}') == 0 then
    error("draft-04 was accepted", 0)
  end
end)

if failures > 0 then error(failures .. " test(s) failed", 0) end
