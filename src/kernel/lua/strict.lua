-- Strict globals for plugin code: reading or writing a global that does not
-- exist is an error, so a typo cannot silently become a nil or a leaked global.
local M = {}

-- An environment for one plugin's chunk. Every standard global is readable;
-- anything else is an error naming the global and the plugin.
function M.env(plugin_name)
  local base = _G
  local env = setmetatable({}, {
    __index = function(_, key)
      local value = rawget(base, key)
      if value == nil then
        error(string.format('plugin %s: read of undeclared global "%s"', plugin_name, tostring(key)), 2)
      end
      return value
    end,
    __newindex = function(_, key)
      error(string.format('plugin %s: assignment to undeclared global "%s"', plugin_name, tostring(key)), 2)
    end,
  })
  -- _G is the plugin's own environment, so _G.x = 1 is no way around the rule.
  rawset(env, "_G", env)
  return env
end

-- Runs chunk (a loaded function) under strict globals and returns what it returns.
function M.run(plugin_name, chunk)
  setfenv(chunk, M.env(plugin_name))
  return chunk()
end

return M
