
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

local assert = assert
local error = error
local pairs = pairs
local setmetatable = setmetatable
local tonumber = tonumber
local type = type
local char = string.char
local find = string.find
local gsub = string.gsub
local match = string.match
local sub = string.sub
local tconcat = table.concat
local resolve_uri = require'schema.utils'.resolve_uri
local get_jsonptr = require'schema.utils'.get_jsonptr

local _ENV = nil

local core = {}

local mt = {}

local store = {}

local function _new (schema, parent, kw, ofs1, ofs2)
    local meta_schema = type(schema) == 'table' and schema['$schema'] or nil
    meta_schema = meta_schema or (parent and parent.meta_schema or nil)
    meta_schema = assert(meta_schema or core.default_schema, 'unknown schema')
    meta_schema = gsub(meta_schema, '#$', '')
    local meta_validator = store[meta_schema]
    local resolver = core.custom_resolver
    if not meta_validator and type(resolver) == 'function' then
        local v = resolver(meta_schema)
        if v then
            meta_validator = _new(v)
        end
    end
    meta_validator = meta_validator or store[core.default_schema]
    if meta_validator then
        local result = meta_validator:validate(schema)
        assert(result.valid, 'invalid schema')
    end
    local ptr = tconcat({ parent and parent.ptr or '', kw, ofs1, ofs2 }, '/')
    local abs = parent and parent.abs or nil
    if abs then
        if not find(abs, '#', 1, true) then
            abs = abs .. '#'
        end
        abs = tconcat({ abs, kw, ofs1, ofs2 }, '/')
    end
    local obj = {
        schema      = schema,
        meta_schema = meta_schema,
        ptr         = ptr,
        abs         = abs,
    }
    setmetatable(obj, { __index = mt })
    if schema == true then
        obj.funcs = {
            function (_, _, data_ptr)
                return {
                    valid = true,
                    instanceLocation        = data_ptr,
                    keywordLocation         = ptr,
                    absoluteKeywordLocation = abs,
                }
            end
        }
    elseif schema == false then
        obj.funcs = {
            function (_, _, data_ptr)
                return {
                    valid = false,
                    instanceLocation        = data_ptr,
                    keywordLocation         = ptr,
                    absoluteKeywordLocation = abs,
                }
            end
        }
    else
        obj:_build(parent)
    end
    return obj
end
core._new = _new

function core.new (schema)
    return _new(schema)
end

local function new_external (path)
    local resolver = core.custom_resolver
    if type(resolver) == 'function' then
        local doc = resolver(path)
        if doc ~= nil then
            if type(doc) == 'table' and not doc['$id'] and not doc.id then
                doc['$id'] = path
            end
            return _new(doc)
        end
    else
        error("no external resolver for " .. path)
    end
end

local function split_ref (s)
    local pos = find(s, '#', 1, true)
    if pos then
        if pos == 1 then
            return nil, sub(s, 2)
        else
            return sub(s, 1, pos - 1), sub(s, pos + 1)
        end
    else
        return s, ''
    end
end

local function merge (orig, deref)
    local t = {}
    local n = 0
    for k, v in pairs(orig) do
        if k ~= '$ref' and k ~= '$recursiveRef' and k ~= '$dynamicRef' then
            t[k] = v
            n = n + 1
        end
    end
    if n == 0 then
        return deref
    end
    for k, v in pairs(deref) do
        local t_k = t[k]
        if type(t_k) == 'table' then
            for kk, vv in pairs(v) do
                if t_k[kk] == nil then
                    t_k[kk] = vv
                end
            end
        elseif t_k == nil then
            t[k] = v
        end
    end
    return t
end

local _core = {
    ['$schema']          = true,
    ['$vocabulary']      = true,  -- >= draft2019-09
    id                   = true,  -- == draft-04
    ['$id']              = true,  -- >= draft-06
    ['$anchor']          = true,  -- >= draft2019-09
    ['$recursiveAnchor'] = true,  -- == draft2019-09
    ['$dynamicAnchor']   = true,  -- >= draft2020-12
    ['$ref']             = true,
    ['$recursiveRef']    = true,  -- == draft2019-09
    ['$dynamicRef']      = true,  -- >= draft2020-12
    definitions          = true,  -- <= draft-07
    ['$defs']            = true,  -- >= draft2019-09
    ['$comment']         = true,  -- >= draft-07
}

local in_place = {
    allOf = true,
    anyOf = true,
    oneOf = true,
    ['not'] = true,
    ['if'] = true,
    ['then'] = true,
    ['else'] = true,
    dependentSchemas = true,
}

local unevaluated = {
    unevaluatedItems = true,
    unevaluatedProperties = true,
}

function mt:_build (parent)
    local schema = self.schema
    local funcs = {}
    self.funcs = funcs

    if parent then
        assert(parent.level < 99, "infinite recursion")
        self.parent            = parent
        self.base              = parent.base
        self.refs              = parent.refs
        self.format_annotation = parent.format_annotation
        self.format_assertion  = parent.format_assertion
        self.validation        = parent.validation
        self.level             = parent.level + 1
    else
        self:_vocabulary()
        self:_reference(schema)
        self.base              = ''
        self.level             = 1
    end

    self:_store()

    local dynamic_ref = schema['$dynamicRef']
    if type(dynamic_ref) == 'string' then
        return self:_dynamic_ref(dynamic_ref)
    end
    local recursive_ref = schema['$recursiveRef']
    if type(recursive_ref) == 'string' then
        assert(recursive_ref == '#')
        return self:_recursive_ref()
    end
    local ref = schema['$ref']
    if type(ref) == 'string' then
        return self:_ref(ref)
    end

    for k, v in pairs(schema) do
        if in_place[k] then
            local fn = core.keyword[k]
            if type(fn) == 'function' then
                funcs[#funcs+1] = fn(v, self)
            end
        end
    end
    for k, v in pairs(schema) do
        if not _core[k] and not in_place[k] and not unevaluated[k] then
            local fn = core.custom_keyword[k] or core.keyword[k]
            if type(fn) == 'function' then
                funcs[#funcs+1] = fn(v, self)
            end
        end
    end
    for k, v in pairs(schema) do
        if unevaluated[k] then
            local fn = core.keyword[k]
            if type(fn) == 'function' then
                funcs[#funcs+1] = fn(v, self)
            end
        end
    end
end

function mt:_vocabulary ()
    local meta_schema = store[self.meta_schema]
    local vocabulary = meta_schema and meta_schema.schema['$vocabulary'] or nil
    if type(vocabulary) == 'table' then
        self.format_annotation = false
        self.format_assertion  = core.format_assertion
        self.validation        = false
        for k, v in pairs(vocabulary) do
            if v then
                if match(k, 'vocab/format%-annotation$') then
                    self.format_annotation = true
                end
                if match(k, 'vocab/format%-assertion$') then
                    self.format_assertion = true
                end
                if match(k, 'vocab/validation$') then
                    self.validation = true
                end
            else
                if match(k, 'vocab/format$') then  -- draft2019-09
                    self.format_annotation = true
                end
            end
        end
    else
        self.format_annotation = true
        self.format_assertion  = core.format_assertion
        self.validation        = true
    end
end

function mt:_reference (schema)
    local refs = {}

    local function walk (t, base)
        local id = t['$id'] or t.id
        if type(id) == 'string' then
            id = gsub(id, '#$', '')
            if not match(id, '^urn:uuid:') and not match(id, '://') then
                id = resolve_uri(base, id)
            end
            refs[id] = t
            base = id
        end
        local anchor = t['$anchor'] or t['$dynamicAnchor']
        if type(anchor) == 'string' then
            local url = resolve_uri(base, '#' .. anchor)
            refs[url] = t
        end
        for k, v in pairs(t) do
            if type(v) == 'table' and k ~= 'enum' and k ~= 'const' then
                walk(v, base)
            end
        end
    end  -- walk

    walk(schema, '')
    self.refs = refs
end

function mt:_store ()
    local schema = self.schema
    local id = schema['$id'] or schema.id
    if type(id) == 'string' then
        assert(id ~= '')
        assert(id ~= '#')
        id = gsub(id, '#$', '')
        if not match(id, '^urn:uuid:') and not match(id, '://') then
            id = resolve_uri(self.base, id)
        end
        store[id] = self
        self.base = id
        self.abs = id
    end
end

function mt:_dynamic_ref (ref)
    local _, fragment = split_ref(ref)
    assert(fragment ~= '')
    if match(fragment, '^%w') then
        local sav
        local v = self.parent
        while v ~= nil do
            if v.schema['$dynamicAnchor'] == fragment and v.schema['$id'] and not v.schema['$ref'] then
                sav = v
            end
            v = v.parent
        end
        if sav then
            self.funcs = sav.funcs
            return
        end
    end

    return self:_ref(ref)
end

function mt:_recursive_ref ()
    local sav
    local v = self.parent
    while v ~= nil do
        if v.schema['$id'] and not v.schema['$ref'] then
            if v.schema['$recursiveAnchor'] then
                sav = v
            elseif not sav then
                self.funcs = v.funcs
                return
            end
        end
        v = v.parent
    end
    if sav then
        self.funcs = sav.funcs
        return
    end

    v = self
    while true do
        if v.schema['$id'] or v.schema.id or not v.parent then
            self.funcs = v.funcs
            return
        end
        v = v.parent
    end
end

function mt:_ref (ref)
    local schema = self.schema
    local path, fragment = split_ref(ref)
    if path and not match(path, '^urn:uuid:') and not match(path, '://') then
        local base = self.parent and self.parent.base or self.base
        path = resolve_uri(base, path)
    end

    local curr = self
    local deref
    if fragment == '' then
        if path then
            local v = store[path]
            if v then
                deref = v.schema
            end
            if deref == nil then
                deref = self.refs[path]
            end
            if deref == nil then
                curr = new_external(path)
                deref = curr.schema
            end
        else
            local v = self
            while true do
                if v.schema['$id'] or v.schema.id or not v.parent then
                    self.funcs = v.funcs
                    return
                end
                v = v.parent
            end
        end
    else
        if path then
            local v = store[path]
            if v then
                curr = v
            else
                if curr.refs[path] == nil then
                    curr = new_external(path)
                end
            end
        end

        if match(fragment, '^%w') then
            local new_base = path or curr.base
            local url = new_base .. '#' .. fragment
            curr.base = new_base
            deref = curr.refs[url]
        else
            deref = curr:_get_jsonptr(path, fragment)
        end
    end
    assert(deref ~= nil, 'not found ' .. ref)

    local v = self.parent
    while v ~= nil do
        if v.schema == deref then
            self.funcs = v.funcs
            return
        end
        v = v.parent
    end

    local kw = schema['$dynamicRef'] and '$dynamicRef' or '$ref'
    if type(deref) == 'table' and not find(self.meta_schema, 'draft-0', 1, true) then
        self.funcs = _new(merge(schema, deref), curr, kw).funcs
    else
        self.funcs = _new(deref, curr, kw).funcs
    end
end

function mt:_get_jsonptr(path, fragment)
    local jptr = gsub(fragment, '%%(%x%x)', function (n)
                                                return char(tonumber(n, 16))
                                            end)
    local doc
    if path then
        doc = self.refs[path]
    end
    if doc then
        local new_base = resolve_uri(self.base, path)
        self.base = new_base
        return (get_jsonptr({ schema = doc, base = new_base }, jptr))
    else
        local v = self
        while v ~= nil do
            local deref, new_base = get_jsonptr(v, jptr)
            if deref ~= nil then
                self.base = new_base
                return deref
            end
            v = v.parent
        end
    end
end

function mt:_mk_output (valid, msg, kw, data_ptr, assertion)
    local ptr = self.ptr .. '/' .. kw
    local abs = self.abs
    if abs then
        if not find(abs, '#', 1, true) then
            abs = abs .. '#/' .. kw
        else
            abs = abs .. '/' .. kw
        end
    end
    if assertion then
        return {
            valid                   = valid,
            instanceLocation        = data_ptr,
            keywordLocation         = ptr,
            absoluteKeywordLocation = abs,
            error                   = msg,
        }
    else
        return {
            valid                   = true,
            instanceLocation        = data_ptr,
            keywordLocation         = ptr,
            absoluteKeywordLocation = abs,
            annotation              = msg,
        }
    end
end

function mt:_mk_output_anno (kw, data_ptr, anno)
    local ptr = self.ptr .. '/' .. kw
    local abs = self.abs
    if abs then
        if not find(abs, '#', 1, true) then
            abs = abs .. '#/' .. kw
        else
            abs = abs .. '/' .. kw
        end
    end
    return {
        valid                   = true,
        instanceLocation        = data_ptr,
        keywordLocation         = ptr,
        absoluteKeywordLocation = abs,
        annotation              = anno,
    }
end

local function merge_output_basic (valid, outputs, kw_abs, kw_ptr, data_ptr, msg)
    if valid then
        local annotations = {}
        if outputs then
            for i = 1, #outputs do
                local v = outputs[i]
                if v.valid == true then
                    if v.annotations then
                        for j = 1, #v.annotations do
                            annotations[#annotations+1] = v.annotations[j]
                        end
                    elseif v.annotation then
                        annotations[#annotations+1] = v
                    end
                end
            end
        end
        return {
            valid                   = valid,
            instanceLocation        = data_ptr,
            keywordLocation         = kw_ptr,
            absoluteKeywordLocation = kw_abs,
            annotations             = #annotations > 0 and annotations or nil,
        }
    else
        local errors = {}
        if msg then
            errors[1] = {
                valid                   = false,
                instanceLocation        = data_ptr,
                keywordLocation         = kw_ptr,
                absoluteKeywordLocation = kw_abs,
                error                   = msg,
            }
        end
        if outputs then
            for i = 1, #outputs do
                local v = outputs[i]
                if v.valid == false then
                    if v.errors then
                        for j = 1, #v.errors do
                            errors[#errors+1] = v.errors[j]
                        end
                    else
                        errors[#errors+1] = v
                    end
                end
            end
        end
        return {
            valid                   = valid,
            instanceLocation        = data_ptr,
            keywordLocation         = kw_ptr,
            absoluteKeywordLocation = kw_abs,
            errors                  = errors,
        }
    end
end

local function merge_output_detailed (valid, outputs, kw_abs, kw_ptr, data_ptr, msg)
    if valid then
        local annotations = {}
        if outputs then
            for i = 1, #outputs do
                local v = outputs[i]
                if v.valid == true and (v.annotation or v.annotations) then
                    annotations[#annotations+1] = v
                end
            end
        end
        if #annotations == 0 then
            annotations = nil
        elseif #annotations == 1 then
            annotations = annotations[1]
            if not msg then
                return annotations
            end
        end
        return {
            valid                   = valid,
            instanceLocation        = data_ptr,
            keywordLocation         = kw_ptr,
            absoluteKeywordLocation = kw_abs,
            annotation              = msg,
            annotations             = annotations,
        }
    else
        local errors = {}
        if outputs then
            for i = 1, #outputs do
                local v = outputs[i]
                if v.valid == false then
                    errors[#errors+1] = v
                end
            end
        end
        if #errors == 0 then
            errors = nil
        elseif #errors == 1 then
            errors = errors[1]
            if not msg then
                return errors
            end
        end
        return {
            valid                   = valid,
            instanceLocation        = data_ptr,
            keywordLocation         = kw_ptr,
            absoluteKeywordLocation = kw_abs,
            error                   = msg,
            errors                  = errors,
        }
    end
end

function mt:_merge_output (valid, outputs, kw, data_ptr, msg)
    if core.output_format == 'flag' then
        return {
            valid = valid,
        }
    end
    local ptr = tconcat({ self.ptr, kw }, '/')
    local abs = self.abs
    if abs and kw then
        if not find(abs, '#', 1, true) then
            abs = abs .. '#/' .. kw
        else
            abs = abs .. '/' .. kw
        end
    end
    if core.output_format == 'basic' then
        return merge_output_basic(valid, outputs, abs, ptr, data_ptr, msg)
    else
        return merge_output_detailed(valid, outputs, abs, ptr, data_ptr, msg)
    end
end

function mt:_validate (data, data_ptr, ofs, evaluated)
    data_ptr = tconcat({ data_ptr or '', ofs }, '/')
    evaluated = evaluated or {}
    local valid = true
    local outputs = {}
    local funcs = self.funcs
    for i = 1, #funcs do
        local fn = funcs[i]
        local output = fn(self, data, data_ptr, evaluated)
        valid = valid and output.valid
        outputs[#outputs+1] = output
    end
    return self:_merge_output(valid, outputs, nil, data_ptr)
end

function mt:validate (data)
    return self:_validate(data)
end

return core
--
-- Copyright (c) 2025-2026 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
