
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

local assert = assert
local tonumber = tonumber
local gmatch = string.gmatch
local gsub = string.gsub
local match = string.match
local tconcat = table.concat

local _ENV = nil

local function parse_uri (s)
    local ipos = 1
    local t = {}
    local npos
    t.scheme, npos = match(s, '^([^:/?#]+):()', ipos)
    ipos = npos or ipos
    t.authority, npos = match(s, '^//([^/?#]*)()', ipos)
    ipos = npos or ipos
    t.path, npos = match(s, '^([^?#]*)()', ipos)
    ipos = npos
    t.query, npos = match(s, '^%?([^#]*)()', ipos)
    ipos = npos or ipos
    t.fragment, npos = match(s, '^#(.*)()', ipos)
    ipos = npos or ipos
    assert(ipos > #s)
    return t
end

local function recompose (uri)
    local t = {}
    if uri.scheme then
        t[1] = uri.scheme
        t[2] = ':'
    end
    if uri.authority then
        t[#t+1] = '//'
        t[#t+1] = uri.authority
    end
    assert(uri.path)
    t[#t+1] = uri.path
    if uri.query then
        t[#t+1] = '?'
        t[#t+1] = uri.query
    end
    if uri.fragment then
        t[#t+1] = '#'
        t[#t+1] = uri.fragment
    end
    return tconcat(t)
end

local function remove_dot_segments (path)
    local t = {}
    while #path > 0 do
        local n = 0
        if n == 0 then
            path, n = gsub(path, '^%.%./', '')
        end
        if n == 0 then
            path, n = gsub(path, '^%./', '')
        end

        if n == 0 then
            path, n = gsub(path, '^/%./', '/')
        end
        if n == 0 then
            path, n = gsub(path, '^/%.$', '/')
         end

        if n == 0 then
            path, n = gsub(path, '^/%.%./', '/')
            if n == 1 then
                t[#t] = nil
            end
        end
        if n == 0 then
            path, n = gsub(path, '^/%.%.$', '/')
            if n == 1 then
                t[#t] = nil
            end
        end

        if n == 0 then
            path, n = gsub(path, '^%.$', '')
        end
        if n == 0 then
            path, n = gsub(path, '^%.%.$', '')
        end

        if n == 0 then
            t[#t+1], path = match(path, '^(.[^/]*)(.*)')
        end
    end
    return tconcat(t)
end

local function merge_path (base, rel)
    if base.authority and base.path == '' then
        return '/' .. rel.path
    elseif match(base.path, '/') then
        return gsub(base.path, '[^/]+$', '') .. rel.path
    else
        return rel.path
    end
end

local function resolve_uri (base, rel)
    local b = parse_uri(base)
    assert(b.fragment == nil)
    local r = parse_uri(rel)
    local t = {}
    if r.scheme then
        t.scheme    = r.scheme
        t.authority = r.authority
        t.path      = remove_dot_segments(r.path)
        t.query     = r.query
    else
        if r.authority then
            t.authority = r.authority
            t.path      = remove_dot_segments(r.path)
            t.query     = r.query
        else
            if r.path == '' then
                t.path  = b.path
                t.query = r.query and r.query or b.query
            else
                if match(r.path, '^/') then
                    t.path = remove_dot_segments(r.path)
                else
                    t.path = remove_dot_segments(merge_path(b, r))
                end
                t.query = r.query
            end
            t.authority = b.authority
        end
        t.scheme = b.scheme
    end
    t.fragment = r.fragment
    return recompose(t)
end

local function get_jsonptr (v, ptr)
    local doc = v.schema
    local base = v.base
    if ptr ~= '' then
        if not match(ptr, '^/') then  -- must start with /'
            return nil, 'invalid pointer'
        end
        if match(ptr, '~[^01]') or match(ptr, '~$') then
            return nil, 'invalid escape'
        end
        for _tok in gmatch(ptr, '/([^/]*)') do
            local id = doc['$id'] or doc.id
            if id then
                base = resolve_uri(base, id)
            end
            local tok = gsub(_tok, '~1', '/')
            tok = gsub(tok, '~0', '~')
            if (tok == '0' or match(tok, '^[1-9][0-9]*$')) and not doc[tok] then
                tok = 1 + tonumber(tok)
            end
            if tok == '-' and #doc > 0 then
                doc = doc[#doc]
            else
                doc = doc[tok]
            end
            if doc == nil then
                return nil, 'not found'
            end
        end
    end
    return doc, base
end

local function escape_jsonptr (s)
    s = gsub(s, '~', '~0')
    s = gsub(s, '/', '~1')
    return s
end

return {
    resolve_uri    = resolve_uri,
    get_jsonptr    = get_jsonptr,
    escape_jsonptr = escape_jsonptr,
}
--
-- Copyright (c) 2025-2026 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
