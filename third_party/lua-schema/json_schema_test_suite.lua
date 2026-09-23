-- Runs the upstream JSON-Schema-Test-Suite (draft-07) against lua-schema, as
-- qwe configures it (see src/kernel/lua/validate.lua). Arguments: the suite's
-- .json files. Exits non-zero if any test outside the excluded set fails.
local cbor = require("qwe.cbor")
local json = require("dkjson")
require("compat53")
local schema = require("schema")
require("schema.draft-07")

schema.default_schema = "http://json-schema.org/draft-07/schema"
schema.json.is_array = function(d) return getmetatable(d) == cbor.array_mt end
schema.json.is_object = function(d) return getmetatable(d) == cbor.map_mt end
schema.json.null = cbor.null

-- Whole files that test keywords qwe's strict metaschema rejects or that need
-- something we do not have (a regex engine, remote documents).
local excluded_files = {
  ["format.json"] = "format: rejected by the strict metaschema",
  ["refRemote.json"] = "remote $ref: no network, no remote documents",
}

-- Single tests excluded on purpose, by "<file>: <group>: <test>".
local excluded_tests = {
  ["multipleOf.json: by small number: 0.0075 is multiple of 0.0001"] =
    "double arithmetic: 0.0075 / 0.0001 is not exactly 75",
}

local function read(path)
  local f = assert(io.open(path, "rb"))
  local text = f:read("*a")
  f:close()
  return text
end

local function decode(text)
  return assert(json.decode(text, 1, cbor.null, cbor.map_mt, cbor.array_mt))
end

-- A group that mentions a keyword we exclude, or a remote document, is skipped.
local function skipped(group_text)
  return group_text:find('"format"', 1, true)
      or group_text:find("localhost:1234", 1, true)
      or group_text:find("http://json-schema.org/draft-07/schema#", 1, true) -- a remote $ref to the metaschema
end

local passed, failed, skipped_n = 0, 0, 0
local failures = {}

for _, path in ipairs({ ... }) do
  local name = path:match("([^/]+)$")
  if excluded_files[name] then
    print("skip file " .. name .. " (" .. excluded_files[name] .. ")")
  else
    local groups = decode(read(path))
    for _, group in ipairs(groups) do
      local text = json.encode(group, { exception = function() return "null" end })
      if skipped(text) then
        skipped_n = skipped_n + #group.tests
      else
        local ok, validator = pcall(schema.new, group.schema)
        for _, t in ipairs(group.tests) do
          local label = name .. ": " .. group.description .. ": " .. t.description
          if excluded_tests[label] then
            skipped_n = skipped_n + 1
          elseif not ok then
            failed = failed + 1
            failures[#failures + 1] = label .. " (schema rejected: " .. tostring(validator) .. ")"
          else
            local ran, result = pcall(validator.validate, validator, t.data)
            if ran and result.valid == t.valid then
              passed = passed + 1
            else
              failed = failed + 1
              failures[#failures + 1] = label .. (ran and " (wrong verdict)" or (" (error: " .. tostring(result) .. ")"))
            end
          end
        end
      end
    end
  end
end

for _, f in ipairs(failures) do print("FAIL " .. f) end
print(string.format("json-schema-test-suite draft-07: %d passed, %d failed, %d skipped", passed, failed, skipped_n))
if failed > 0 then os.exit(1) end
