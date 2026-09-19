
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

local core = require'schema.core'
core.keyword = require'schema.keyword'
core.format = require'schema.format'
core.format_assertion = true
core.output_format = 'detailed'
core.custom_keyword = {}
core.custom_format = {}
core.json = {}

require'schema.v1-2026'
core.default_schema = "https://json-schema.org/v1"

core._NAME = ...
core._VERSION = "0.1.2"
core._DESCRIPTION = "lua-schema : JSON Schema data validator"
core._COPYRIGHT = "Copyright (c) 2025-2026 Francois Perrad"
return core
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
