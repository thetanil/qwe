-- The built-in file.line step plugin: edits one line of a file on the target,
-- idempotently. The whole file travels through the backend, like file.ensure: read with
-- stat+cat, written back over stdin, so it works over ssh too and the content is never on
-- a command line. The file's mode is untouched (a shell redirection to an existing file
-- keeps its mode), so unlike file.ensure this plugin never chmods.
local M = {}

local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- The OS reason from a failed command's stderr, with shell noise (like "sh: 1: cannot
-- open x: ") stripped: the text after the last ": ", trimmed. Falls back to the whole
-- trimmed stderr if there is no ": ".
local function clean_reason(stderr)
  local trimmed = stderr:gsub("^%s+", ""):gsub("%s+$", "")
  return trimmed:match(".*: (.*)$") or trimmed
end

-- content -> lines, had_trailing_newline. An empty file is zero lines.
local function split_lines(content)
  if content == "" then return {}, true end
  local trailing = content:sub(-1) == "\n"
  local body = trailing and content:sub(1, -2) or content
  local lines = {}
  for line in (body .. "\n"):gmatch("(.-)\n") do
    lines[#lines + 1] = line
  end
  return lines, trailing
end

local function join_lines(lines, trailing)
  local content = table.concat(lines, "\n")
  if #lines > 0 and trailing then content = content .. "\n" end
  return content
end

local function line_matches(line, with)
  if with.regexp then return line:match(with.regexp) ~= nil end
  return line == with.line
end

-- The index of the last line that matches with (by regexp or by equality), or nil.
local function last_match(lines, with)
  local last
  for i, line in ipairs(lines) do
    if line_matches(line, with) then last = i end
  end
  return last
end

-- The file's content with with.line applied: new_content, changed.
local function edited(content, with)
  local lines, trailing = split_lines(content)
  if with.state == "absent" then
    local kept, removed = {}, false
    for _, line in ipairs(lines) do
      if line_matches(line, with) then
        removed = true
      else
        kept[#kept + 1] = line
      end
    end
    if not removed then return content, false end
    return join_lines(kept, trailing), true
  end
  local last = last_match(lines, with)
  if not last then
    lines[#lines + 1] = with.line
    return join_lines(lines, true), true
  end
  if lines[last] == with.line then return content, false end
  lines[last] = with.line
  return join_lines(lines, trailing), true
end

-- The file's content, or nil if it does not exist.
local function read_file(with, ctx, path)
  local stat = ctx.backend:run("stat -c %a -- " .. path)
  if stat.code ~= 0 then return nil end
  local cat = ctx.backend:run("cat -- " .. path)
  if cat.code ~= 0 then error("file.line: cannot read " .. with.path .. ": " .. clean_reason(cat.stderr), 0) end
  return cat.stdout
end

function M.check(with, ctx)
  local content = read_file(with, ctx, quote(with.path))
  if content == nil then return with.state ~= "absent" end
  local _, changed = edited(content, with)
  return changed
end

function M.apply(with, ctx)
  local path = quote(with.path)
  local content = read_file(with, ctx, path)
  local new_content = content == nil and (with.line .. "\n") or (edited(content, with))
  local write = ctx.backend:run("cat > " .. path, new_content)
  if write.code ~= 0 then error("file.line: cannot write " .. with.path .. ": " .. clean_reason(write.stderr), 0) end
end

return M
