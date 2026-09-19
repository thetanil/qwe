-- The built-in step plugins that `uses:` can name, with their with: schemas.
-- (`run:` is a step kind, not a `uses:` plugin.)
local cbor = require("qwe.cbor")
local json = require("dkjson")

local names = { "file.ensure" }

local M = {}

-- Decodes a JSON schema so that objects and arrays carry qwe.cbor's metatables,
-- which is how lua-schema tells them apart.
function M.decode_schema(text)
  return assert(json.decode(text, 1, cbor.null, cbor.map_mt, cbor.array_mt))
end

-- Returns a list of { name = "file.ensure", schema = <decoded with: schema> }.
function M.builtin()
  local list = {}
  for _, name in ipairs(names) do
    list[#list + 1] = {
      name = name,
      schema = M.decode_schema(require("plugin_schema." .. name)),
    }
  end
  return list
end

return M
