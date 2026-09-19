-- The two Lua 5.3 functions lua-schema needs, for LuaJIT (Lua 5.1 semantics).
-- Installs them as globals, once; strict-globals code never sees them written.
if not utf8 then
  utf8 = {}
  -- Counts code points; returns nil and the byte position if s is not valid UTF-8.
  function utf8.len(s)
    local n, i, len = 0, 1, #s
    while i <= len do
      local c = s:byte(i)
      local extra
      if c < 0x80 then extra = 0
      elseif c >= 0xC2 and c < 0xE0 then extra = 1
      elseif c >= 0xE0 and c < 0xF0 then extra = 2
      elseif c >= 0xF0 and c < 0xF5 then extra = 3
      else return nil, i end
      for k = 1, extra do
        local cc = s:byte(i + k)
        if not cc or cc < 0x80 or cc > 0xBF then return nil, i end
      end
      i = i + extra + 1
      n = n + 1
    end
    return n
  end
end

if not math.tointeger then
  function math.tointeger(x)
    if type(x) == "number" and x % 1 == 0 and x >= -2^53 and x <= 2^53 then
      return x
    end
    return nil
  end
end

return true
