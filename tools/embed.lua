-- Turns Lua source files into a C table, so they can be compiled into the
-- binary. Runs on minilua at build time.
-- usage: minilua embed.lua <out.c> <module>=<file> ...
local arg = {...}
local out = arg[1]
local f = assert(io.open(out, "w"))
f:write('#include "plugins/builtin/embedded.h"\n\n')
local entries = {}
for i = 2, #arg do
  local name, path = arg[i]:match("^([^=]+)=(.+)$")
  assert(name, "bad argument: " .. arg[i])
  local src = assert(io.open(path, "rb")):read("*a")
  f:write(string.format("static const unsigned char data%d[] = {", i))
  for j = 1, #src do
    f:write(string.byte(src, j), ",")
    if j % 20 == 0 then f:write("\n") end
  end
  f:write("0};\n")
  entries[#entries + 1] = string.format('\t{"%s", data%d, %d},\n', name, i, #src)
end
f:write("\nconst struct qwe_embedded qwe_embedded_modules[] = {\n")
for _, e in ipairs(entries) do f:write(e) end
f:write("\t{0, 0, 0},\n};\n")
f:close()
