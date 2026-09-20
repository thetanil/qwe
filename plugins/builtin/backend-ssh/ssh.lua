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

-- How long an unused master lives. A killed qwe (SIGKILL, a crash) cannot close its
-- masters, so this bounds the leak; a normal end closes them at once (close_all).
-- It is long enough that a job queued behind max-sessions, or a gap between two
-- steps, keeps its connection instead of paying a new handshake, and short enough
-- that an abandoned master does not linger. QWE_TEST_MASTER_TTL overrides it (seconds).
M.MASTER_TTL = 120

-- The bounds on the parent's own ssh calls, in seconds. The parent runs an event loop
-- and every call here blocks it, so none may wait on a slow or dead host for long
-- (qwe-ssh-sec I.5). CONNECT_TIMEOUT is for the pre-connect phase, before any timer is
-- armed; RECONNECT_TIMEOUT for a master that died mid-run, the one place another
-- job's timer may be late. CHECK_TIMEOUT covers -O check and -O exit, which take a
-- few milliseconds unless the master is wedged.
M.CONNECT_TIMEOUT = 10
M.RECONNECT_TIMEOUT = 3
M.CHECK_TIMEOUT = 2
M.KILL_WAIT = 5 -- the most close_all waits for one remote kill

function M.master_argv(host, sock, log, ttl, connect_timeout)
  return {
    "ssh", "-M", "-N", "-f", "-S", sock, "-o", "ControlPersist=" .. tostring(ttl or M.MASTER_TTL),
    "-o", "BatchMode=yes", "-o", "ConnectTimeout=" .. tostring(connect_timeout or M.CONNECT_TIMEOUT), "-o", "LogLevel=ERROR", "-E", log, host,
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
local swept = {} -- socket directories already swept by this process

-- Removes the sockets an earlier, killed run left in dir: those of a qwe that is
-- no longer running and whose master no longer answers. A master that is still up
-- (waiting out its ttl) keeps its socket; it removes it itself when it expires.
local function sweep(dir, my_pid)
  for _, name in ipairs(fs.list(dir) or {}) do
    local pid = name:match("^(%d+)%.%x+$")
    if pid and tonumber(pid) ~= my_pid and not fs.isdir("/proc/" .. pid) then
      local path = dir .. "/" .. name
      -- the destination is required by ssh but unused by -O check
      if exec.run(M.check_argv("stale", path), nil, { timeout = M.CHECK_TIMEOUT }) ~= 0 then os.remove(path) end
    end
  end
end

local function alive(m)
  return exec.run(M.check_argv(m.host, m.sock), nil, { timeout = M.CHECK_TIMEOUT }) == 0
end

local function read_all(path)
  local f = io.open(path, "rb")
  if not f then return "" end
  local text = f:read("*a")
  f:close()
  return (text:gsub("%s+$", ""))
end

-- Where the master's socket and log go: the directory dir.
local function place(m, dir)
  local pid = exec.getpid()

  m.dir = dir
  m.sock = M.sock_path(dir, pid)
  m.log = string.format("%s/%d.%s.log", dir, pid, m.name)
end

-- Makes sure the target's master is running: starts it, or starts it again if
-- it died, giving the connection connect_timeout seconds (default CONNECT_TIMEOUT).
-- Returns true; or nil, why it could not, and "refused" when the socket directory
-- failed its check (a fault to report, not a host to give up on). A target that
-- could not be connected to is marked unreachable, and the run stops asking.
function M.ensure(name, host, connect_timeout)
  local m = masters[name]
  if not m then
    m = { host = host, name = name }
    place(m, M.socket_dir(exec.getuid(), os.getenv("XDG_RUNTIME_DIR")))
    masters[name] = m
  end
  -- Checked before anything is asked of a socket in it, every time: whoever owns
  -- the directory owns what the step's commands and secrets are sent to.
  local ok, problem, kind = fs.private_dir(m.dir, "the socket directory")
  if not ok and kind == "create" and m.dir ~= M.socket_dir(exec.getuid(), nil) then
    -- $XDG_RUNTIME_DIR names a directory this user cannot write to (a container, su):
    -- /tmp, which is checked just the same, rather than a step that cannot run
    place(m, M.socket_dir(exec.getuid(), nil))
    ok, problem = fs.private_dir(m.dir, "the socket directory")
  end
  if not ok then return nil, problem, "refused" end
  if not swept[m.dir] then
    swept[m.dir] = true
    sweep(m.dir, exec.getpid())
  end
  if alive(m) then return true end
  local ct = connect_timeout or M.CONNECT_TIMEOUT
  local argv = M.master_argv(m.host, m.sock, m.log, tonumber(os.getenv("QWE_TEST_MASTER_TTL")), ct)
  local code, out, err = exec.run(argv, nil, { detach = true, timeout = ct })
  if code == nil then err = out end -- it could not start, or it timed out
  local why = read_all(m.log)
  os.remove(m.log)
  if code ~= 0 then
    m.unreachable = true
    if why == "" then why = err ~= "" and err or ("ssh exited with " .. tostring(code)) end
    return nil, "cannot connect to " .. host .. ": " .. why
  end
  m.unreachable = nil
  return true
end

-- Whether the run has given up on the target: its master could not be started.
-- Its steps still run and fail at once on a socket that is not there.
function M.unreachable(name)
  local m = masters[name]
  return m ~= nil and m.unreachable == true
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

-- Stops the processes of the step with this token on the target's host. It does
-- not wait: the parent is inside the grace window of a teardown, and the result
-- was never used. Nothing is sent through a master that is not up.
local kills = {} -- pids of the kills in flight

function M.kill(name, token, signal)
  local m = masters[name]

  if not m or m.unreachable or not alive(m) then return end
  local pid = exec.run(M.kill_argv(m.host, m.sock, token, signal), nil, { background = true })
  if pid then kills[#kills + 1] = pid end
end

-- Ends every master this process started, after the kills in flight have had their
-- say (closing the master first would cut them off).
function M.close_all()
  for _, pid in ipairs(kills) do exec.wait(pid, M.KILL_WAIT) end
  kills = {}
  for name, m in pairs(masters) do
    if not m.unreachable then exec.run(M.exit_argv(m.host, m.sock), nil, { timeout = M.CHECK_TIMEOUT }) end
    masters[name] = nil
  end
end

return M
