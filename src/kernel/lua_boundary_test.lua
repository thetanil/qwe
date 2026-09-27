-- Every C function qwe's kernel exposes to Lua (qwe.exec, qwe.fs,
-- qwe.secrets, qwe.cbor), called with a wrong-typed argument at every
-- position, and with fewer than its required arguments. Ticket
-- sca-round3/03 (the class ticket 02 fixed one instance of): none of these
-- must crash, hang or leak (this file, run through luarun under
-- --config=asan, is the leak check -- LeakSanitizer over every case below).
--
-- The matrix is a table, not one test per function, so a function added to
-- one of these modules without a row here is caught: every key each
-- module's require() table actually has must appear in the matrix.

local ALL_TYPES = { "nil", "boolean", "number", "string", "table", "function", "userdata" }

local function sample(t)
	if t == "nil" then return nil end
	if t == "boolean" then return true end
	if t == "number" then return 7 end
	if t == "string" then return "x" end
	if t == "table" then return {} end
	if t == "function" then return print end
	if t == "userdata" then return io.stdout end
	error("no sample for type " .. t)
end

-- Every type in ALL_TYPES not in the allowed set.
local function disallowed(allowed)
	local want, out = {}, {}
	for _, t in ipairs(allowed) do want[t] = true end
	for _, t in ipairs(ALL_TYPES) do
		if not want[t] then out[#out + 1] = t end
	end
	return out
end

-- A matrix entry:
--   args: one per position, each either
--     { allowed = { "table" }, good = {} }  -- only these types must not raise
--   or "any", meaning every type (including none, at the end) is accepted --
--   there is no wrong type there, by the function's own contract.
--   required: arguments 1..required must all be present or the call raises
--   (default #args). A position beyond `required` is optional: the "missing
--   argument" check never tries calls that only omit optional positions.
local matrix = {
	exec = {
		lib = require("qwe.exec"),
		fns = {
			getpid = { args = {}, required = 0 },
			getuid = { args = {}, required = 0 },
			preamble = {
				args = { { allowed = { "table" }, good = {} } },
			},
			run = {
				-- argv required; stdin optional (nil or a string -- and a
				-- number, since luaL_checklstring coerces one, same as
				-- argv's own elements, ticket 02); opts (a table) is read
				-- only if it is one -- any other type is silently ignored,
				-- not an error, so it is "any" here too.
				args = {
					{ allowed = { "table" }, good = { "true" } },
					{ allowed = { "nil", "string", "number" }, good = nil },
					"any",
				},
				required = 1,
			},
			wait = {
				-- an unlikely-real pid, and a short bound: never blocks.
				args = {
					{ allowed = { "number" }, good = 999999999 },
					{ allowed = { "number" }, good = 0.01 },
				},
			},
			bootstrap = "not a function: the bootstrap script constant",
		},
	},
	fs = {
		lib = require("qwe.fs"),
		fns = {
			-- luaL_checkstring coerces a number to its string form, so one
			-- is not a wrong type here (or anywhere else in this file marked
			-- the same way).
			list = { args = { { allowed = { "string", "number" }, good = "." } } },
			isdir = { args = { { allowed = { "string", "number" }, good = "." } } },
			private_dir = {
				args = {
					{ allowed = { "string", "number" }, good = (os.getenv("TEST_TMPDIR") or "/tmp") .. "/qwe-boundary-fs" },
					{ allowed = { "nil", "string", "number" }, good = nil },
				},
				required = 1,
			},
		},
	},
	secrets = {
		lib = require("qwe.secrets"),
		fns = {
			-- reveal's one argument is a table; the string-ness of its "value"
			-- field is a nested shape, not a top-level argument type, and is
			-- exercised by the wrong-typed-arguments cases already in
			-- luaexec_test.c's sibling tests instead (ticket 02's pattern).
			reveal = { args = { { allowed = { "table" }, good = { value = "v1:x" } } } },
			mask = { args = { { allowed = { "string", "number" }, good = "x" } } },
			check = { args = { { allowed = { "string", "number" }, good = "x" } } },
		},
	},
	cbor = {
		lib = require("qwe.cbor"),
		fns = {
			decode = { args = { { allowed = { "string", "number" }, good = "\1" } } },
			-- encode's argument is luaL_checkany: every type is valid (there
			-- is no wrong type, only "missing"), so nothing to fuzz per
			-- position, but it is still required.
			encode = { args = { "any" }, required = 1 },
			array = { args = { { allowed = { "table" }, good = {} } }, required = 0 },
			map = { args = { { allowed = { "table" }, good = {} } }, required = 0 },
			-- predicates: accept any value, including none, by design (that
			-- is their point) and never raise, so there is no wrong type or
			-- required argument to test either.
			is_array = { args = { "any" }, required = 0 },
			is_map = { args = { "any" }, required = 0 },
			is_secret = { args = { "any" }, required = 0 },
			secret = { args = { { allowed = { "string", "number" }, good = "x" } } },
			array_mt = "not a function: a metatable constant",
			map_mt = "not a function: a metatable constant",
			secret_mt = "not a function: a metatable constant",
			null = "not a function: the null sentinel (a light userdata)",
		},
	},
}

local failures = {}
local function fail(msg)
	failures[#failures + 1] = msg
end

local function good_or_sample(a)
	if a == "any" then return sample("number") end
	return a.good
end

for modname, mod in pairs(matrix) do
	local seen = {}
	for name, kind in pairs(mod.fns) do
		seen[name] = true
		if type(kind) == "string" then
			-- a documented skip (not a function): still checked against the
			-- module below, just never called.
		elseif type(mod.lib[name]) ~= "function" then
			fail(modname .. "." .. name .. ": in the matrix but not a function in the module")
		else
			local fn, spec = mod.lib[name], kind
			local n = #spec.args
			local required = spec.required or n

			-- Fewer than required arguments, for every length below it.
			for len = 0, required - 1 do
				local args = {}
				for i = 1, len do
					args[i] = good_or_sample(spec.args[i])
				end
				local ok = pcall(fn, unpack(args, 1, len))
				if ok then
					fail(("%s.%s: %d of %d required arguments did not raise"):format(modname, name, len, required))
				end
			end

			-- Every wrong type at every position, good values elsewhere.
			for pos, a in ipairs(spec.args) do
				if a ~= "any" then
					for _, bad in ipairs(disallowed(a.allowed)) do
						local args = {}
						for i = 1, n do
							if i == pos then
								args[i] = sample(bad)
							else
								args[i] = good_or_sample(spec.args[i])
							end
						end
						local ok, err = pcall(fn, unpack(args, 1, n))
						if ok then
							fail(("%s.%s: a %s at position %d did not raise"):format(modname, name, bad, pos))
						elseif type(err) ~= "string" then
							fail(("%s.%s: a %s at position %d raised a non-string error"):format(modname, name, bad, pos))
						end
					end
				end
			end
		end
	end
	for name in pairs(mod.lib) do
		if not seen[name] then
			fail(modname .. "." .. name .. ": exported by the module but missing from the matrix")
		end
	end
end

if #failures > 0 then
	error(("lua_boundary_test: %d failure(s):\n"):format(#failures) .. table.concat(failures, "\n"))
end
print("lua_boundary_test: ok")
