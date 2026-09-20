-- The `ssh` execution backend: commands run on a remote host through the host
-- OpenSSH client, multiplexed over one ControlMaster per host (qwe-ssh-sec I).
--
-- Two halves:
--   * translation, a pure function: a script and its stdin become the local
--     `ssh -S <sock> ...` argv and the stdin that carries the env preamble. It
--     runs in the step's forked child.
--   * the connection lifecycle, in the parent only: ensure() starts a master
--     (in its own session, outside every step's process group), close_all()
--     ends them, kill() stops a step's processes on the remote host. A forked
--     step only inherits the socket's path, never a connection.
local become = require("qwe.become")
local exec = require("qwe.exec")
local fs = require("qwe.fs")

local M = {}

-- Quotes s as one word for a POSIX shell.
local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- A word that needs no quoting stays as it is, so the remote command reads well.
local function word(s)
  if s:match("^[A-Za-z0-9_./=+-]+$") then return s end
  return quote(s)
end

-- Stops what a step started on the remote host. sshd gives a command run
-- without a tty no way to learn that its channel was closed, so killing the
-- local ssh client leaves the remote processes running. Every command
-- qwe sends has QWE_STEP=<token> in its environment (see qwe.template.resolve),
-- which its children inherit: this finds them by that and signals them.
local KILLER = [[
tok=$1; sig=$2
for d in /proc/[0-9]*; do
  tr '\0' '\n' < "$d/environ" 2>/dev/null | grep -qx "QWE_STEP=$tok" && kill -"$sig" "${d#/proc/}" 2>/dev/null
done
exit 0
]]

-- The directory for control sockets and master logs: $XDG_RUNTIME_DIR/qwe when
-- that is set (the system made it per-user and 0700), else /tmp/qwe-<uid>.
-- A unix socket path is limited to about 108 bytes, so a runtime dir too long
-- for that falls back to /tmp. ensure() verifies the directory before using it.
-- ssh appends "." and 16 characters while it binds; %C expands to 40.
local MAX_SOCK = 107
local SOCK_TAIL = #"/4194304." + 40 + 17

function M.socket_dir(uid, xdg)
  if xdg and xdg ~= "" then
    local dir = xdg:gsub("/+$", "") .. "/qwe"
    if #dir + SOCK_TAIL <= MAX_SOCK then return dir end
  end
  return string.format("/tmp/qwe-%d", uid)
end

-- The ControlPath. One socket per qwe run and host: a run never shares (or
-- closes) another run's master. ssh expands %C to a hash of the connection.
function M.sock_path(dir, pid)
  return string.format("%s/%d.%%C", dir, pid)
end

-- ssh options every client of a master takes.
local function client(sock)
  return { "ssh", "-S", sock, "-o", "ControlMaster=no", "-o", "BatchMode=yes" }
end

local function append(list, ...)
  for _, v in ipairs({ ... }) do list[#list + 1] = v end
  return list
end

-- The argv that runs a command remotely: sh -c <bootstrap> sh <script>, as one
-- string for the remote shell to parse.
function M.command_argv(host, sock, script, become_value)
  local words = {}
  for _, w in ipairs(become.prefix(become_value)) do words[#words + 1] = word(w) end
  -- sudo (if any) wraps the shell that reads the preamble
  words[#words + 1] = "sh -c " .. quote(exec.bootstrap) .. " sh " .. quote(script)
  local remote = table.concat(words, " ")
  -- ProxyCommand=false: a step never opens a connection of its own. Without it, a
  -- client whose master died mid-session falls back to a new direct connection
  -- (ControlMaster=no allows that), and the step runs on, unowned, instead of
  -- failing with connection-lost.
  return append(client(sock), "-o", "ProxyCommand=false", host, "--", remote)
end

function M.master_argv(host, sock, log)
  return {
    "ssh", "-M", "-N", "-f", "-S", sock, "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
    "-o", "LogLevel=ERROR", "-E", log, host,
  }
end

function M.check_argv(host, sock)
  return { "ssh", "-S", sock, "-O", "check", host }
end

function M.exit_argv(host, sock)
  return { "ssh", "-S", sock, "-O", "exit", host }
end

function M.kill_argv(host, sock, token, signal)
  local argv = client(sock)
  append(argv, "-o", "ConnectTimeout=5", host, "--",
    "sh -c " .. quote(KILLER) .. " sh " .. quote(token) .. " " .. quote(signal))
  return argv
end

local Backend = {}
Backend.__index = Backend

-- Translates a shell script into what starts it on the target: the argv, and
-- the stdin (the env preamble, then the command's own stdin, unchanged).
function Backend:command(script, stdin)
  return M.command_argv(self.host, self.sock, script, self.become), exec.preamble(self.env) .. (stdin or "")
end

-- Runs a shell script, with stdin (a string, or nil for none) as its input.
-- Returns { code, stdout, stderr }; code is -N if the script died of signal N.
function Backend:run(script, stdin)
  local argv, input = self:command(script, stdin)
  local code, out, err = exec.run(argv, input)
  if code == nil then error("cannot run a command: " .. tostring(out), 0) end
  return { code = code, stdout = out, stderr = err }
end

-- opts: host, sock, env (the step's declared env, a { NAME = "value" } table).
function M.new(opts)
  return setmetatable({
    host = opts.host, sock = opts.sock, env = opts.env or {}, become = opts.become, remote = true,
  }, Backend)
end

-- ---- the connection lifecycle (the parent) ----

local masters = {} -- target name -> { host, sock, log }

local function alive(m)
  return exec.run(M.check_argv(m.host, m.sock)) == 0
end

local function read_all(path)
  local f = io.open(path, "rb")
  if not f then return "" end
  local text = f:read("*a")
  f:close()
  return (text:gsub("%s+$", ""))
end

-- Makes sure the target's master is running: starts it, or starts it again if
-- it died. Returns true, or nil and why it could not.
function M.ensure(name, host)
  local m = masters[name]
  if not m then
    local pid = exec.getpid()
    local dir = M.socket_dir(exec.getuid(), os.getenv("XDG_RUNTIME_DIR"))
    m = {
      host = host,
      sock = M.sock_path(dir, pid),
      log = string.format("%s/%d.%s.log", dir, pid, name),
      dir = dir,
    }
    masters[name] = m
  end
  -- Checked before anything is asked of a socket in it, every time: whoever owns
  -- the directory owns what the step's commands and secrets are sent to.
  local ok, problem = fs.private_dir(m.dir, "the socket directory")
  if not ok then return nil, problem end
  if alive(m) then return true end
  local code, _, err = exec.run(M.master_argv(m.host, m.sock, m.log), nil, { detach = true })
  local why = read_all(m.log)
  os.remove(m.log)
  if code ~= 0 then
    if why == "" then why = err ~= "" and err or ("ssh exited with " .. tostring(code)) end
    return nil, "cannot connect to " .. host .. ": " .. why
  end
  return true
end

-- Whether the target's master is still up.
function M.alive(name)
  local m = masters[name]
  return m ~= nil and alive(m)
end

-- The backend for a step on the target, in the step's child: the socket path
-- is what it inherited from the parent.
function M.for_target(name, env, become_value)
  local m = masters[name]
  if not m then error("no ssh connection to target " .. name, 0) end
  return M.new({ host = m.host, sock = m.sock, env = env, become = become_value })
end

-- Stops the processes of the step with this token on the target's host.
function M.kill(name, token, signal)
  local m = masters[name]
  if m then exec.run(M.kill_argv(m.host, m.sock, token, signal)) end
end

-- Ends every master this process started.
function M.close_all()
  for name, m in pairs(masters) do
    exec.run(M.exit_argv(m.host, m.sock))
    masters[name] = nil
  end
end

return M
