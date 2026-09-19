
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

if _VERSION < 'Lua 5.3' then
    require'compat53'
end

local core = require'schema.core'
local escape = require'schema.utils'.escape_jsonptr
local error = error
local pairs = pairs
local setmetatable = setmetatable
local tostring = tostring
local type = type
local tointeger = math.tointeger
local match = string.match
local tconcat = table.concat
local len = utf8.len
local re_compile = require're'.compile
local has_pcre, pcre = pcall(require, 'rex_pcre2')
local pcre_new = has_pcre and pcre.new or function () error('rex_pcre2 missing') end

local _ENV = nil

local function clone (t1)
    local t2 = {}
    for k, v in pairs(t1) do
        t2[k] = v
    end
    return t2
end

local function same (a, b)
    local seen = {}

    local function deep_eq (t1, t2)
        if t1 == t2 or seen[t1] then
            return true
        end
        seen[t1] = true
        for k, v2 in pairs(t2) do
            local v1 = t1[k]
            if type(v1) == 'table' and type(v2) == 'table' then
                local r = deep_eq(v1, v2)
                if not r then
                    return false
                end
            else
                if v1 ~= v2 then
                    return false
                end
            end
        end
        for k in pairs(t1) do
            local v2 = t2[k]
            if v2 == nil then
                return false
            end
        end
        return true
    end -- deep_eq

    if type(a) == 'table' and type(b) == 'table' then
        return deep_eq(a, b)
    else
        return false
    end
end

local function is_table (data)
    return type(data) == 'table'
end

--[[
        JSON Schema: A Language for Validating and Annotating JSON

]]

local keyword = {
    ['then']             = true,  -- if
    ['else']             = true,  -- then
    maxContains          = true,  -- contains
    minContains          = true,  -- contains
}

--[[ 11. Keywords for Applying Subschemas ]]

--[[ 11.1. Keywords for Applying Subschemas in Place ]]

keyword.allOf = function (schemas, parent)
    local validators = {}
    for i = 1, #schemas do
        validators[i] = core._new(schemas[i], parent, 'allOf', tostring(i - 1))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs = {}
        local evaluateds = {}
        local valid = true
        local msg
        for i = 1, #validators do
            local validator = validators[i]
            evaluateds[i] = clone(evaluated)
            local output = validator:_validate(data, data_ptr, nil, evaluateds[i])
            outputs[#outputs+1] = output
            valid = valid and output.valid
        end
        if valid then
            for i = 1, #evaluateds do
                local t = evaluateds[i]
                for k, v in pairs(t) do
                    if v then
                        evaluated[k] = v
                    end
                end
            end
        else
            msg = 'must match all schema in allOf'
        end
        return schema:_merge_output(valid, outputs, 'allOf', data_ptr, msg)
    end
end

keyword.anyOf = function (schemas, parent)
    local validators = {}
    for i = 1, #schemas do
        validators[i] = core._new(schemas[i], parent, 'anyOf', tostring(i - 1))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs = {}
        local evaluateds = {}
        local nb = 0
        local goods = {}
        for i = 1, #validators do
            local validator = validators[i]
            evaluateds[i] = clone(evaluated)
            local output = validator:_validate(data, data_ptr, nil, evaluateds[i])
            outputs[#outputs+1] = output
            if output.valid then
                nb = nb + 1
                goods[#goods+1] = i
            end
        end
        local valid = nb > 0
        local msg
        if valid then
            for i = 1, #goods do
                for k, v in pairs(evaluateds[goods[i]]) do
                    if v then
                        evaluated[k] = v
                    end
                end
            end
        else
            msg = 'must match a schema in anyOf'
        end
        return schema:_merge_output(valid, outputs, 'anyOf', data_ptr, msg)
    end
end

keyword.oneOf = function (schemas, parent)
    local validators = {}
    for i = 1, #schemas do
        validators[i] = core._new(schemas[i], parent, 'oneOf', tostring(i - 1))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs = {}
        local evaluateds = {}
        local nb = 0
        local good
        for i = 1, #validators do
            local validator = validators[i]
            evaluateds[i] = clone(evaluated)
            local output = validator:_validate(data, data_ptr, nil, evaluateds[i])
            outputs[#outputs+1] = output
            if output.valid then
                nb = nb + 1
                good = i
            end
        end
        local valid = nb == 1
        local msg
        if valid then
            for k, v in pairs(evaluateds[good]) do
                if v then
                    evaluated[k] = v
                end
            end
        else
            msg = 'must match exactly one schema in oneOf'
        end
        return schema:_merge_output(valid, outputs, 'oneOf', data_ptr, msg)
    end
end

keyword['not'] = function (schema_not, parent)
    local validator = core._new(schema_not, parent, 'not')
    return function (schema, data, data_ptr)
        local output = validator:_validate(data, data_ptr, nil)
        local valid = not output.valid
        local msg
        if not valid then
            msg = 'must NOT be valid'
        end
        return schema:_merge_output(valid, { output }, 'not', data_ptr, msg)
    end
end

keyword['if'] = function (schema_if, parent) -- then, else
    local validator_if = core._new(schema_if, parent, 'if')
    local validator_then
    local validator_else
    local schema_p = parent.schema
    local schema_then = schema_p['then']
    if schema_then ~= nil then
        validator_then = core._new(schema_then, parent, 'then')
    end
    local schema_else = schema_p['else']
    if schema_else ~= nil then
        validator_else = core._new(schema_else, parent, 'else')
    end
    return function (schema, data, data_ptr, evaluated)
        local output = validator_if:_validate(data, data_ptr, nil, evaluated)
        if output.valid then
            if validator_then then
                return validator_then:_validate(data, data_ptr, nil, evaluated)
            else
                return schema:_merge_output(true, nil, 'then', data_ptr)
            end
        else
            if validator_else then
                return validator_else:_validate(data, data_ptr, nil, evaluated)
            else
                return schema:_merge_output(true, nil, 'else', data_ptr)
            end
        end
    end
end

keyword.dependentSchemas = function (depend, parent)
    local is_object = core.json.is_object or is_table
    local validators = {}
    for k, schema in pairs(depend) do
        validators[k] = core._new(schema, parent, 'dependentSchemas', escape(k))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, validator in pairs(validators) do
                if data[k] then
                    local output = validator:_validate(data, data_ptr, nil, evaluated)
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'dependentSchemas', data_ptr)
    end
end

keyword.propertyDependencies = function (depend, parent)  -- v1-2026
    local is_object = core.json.is_object or is_table
    local validators = {}
    for k, v in pairs(depend) do
        local t = {}
        for kv, schema in pairs(v) do
            t[kv] = core._new(schema, parent, 'propertyDependencies', escape(k), escape(kv))
        end
        validators[k] = t
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(validators) do
                if data[k] then
                    for kv, validator in pairs(v) do
                        if data[k] == kv then
                            local output = validator:_validate(data, data_ptr, nil, evaluated)
                            outputs[#outputs+1] = output
                            valid = valid and output.valid
                        end
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'propertyDependencies', data_ptr)
    end
end

--[[ 11.2. Keywords for Applying Subschemas to Child Instances ]]

keyword.prefixItems = function (schemas, parent)
    local is_array = core.json.is_array or is_table
    local validators = {}
    for i = 1, #schemas do
        validators[i] = core._new(schemas[i], parent, 'prefixItems', tostring(i + 1))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_array(data) then
            outputs = {}
            for i = 1, #data do
                local validator = validators[i]
                if not validator then
                    break
                end
                local output = validator:_validate(data[i], data_ptr, tostring(i - 1))
                outputs[#outputs+1] = output
                valid = valid and output.valid
                evaluated[i] = output.valid
            end
        end
        return schema:_merge_output(valid, outputs, 'prefixItems', data_ptr)
    end
end

keyword.items = function (items, parent)
    local is_array = core.json.is_array or is_table
    local schema_p = parent.schema
    if type(items) ~= 'table' or #items == 0 then
        local validator = core._new(items, parent, 'items')
        local prefix = 1
        local prefix_items = schema_p.prefixItems
        if prefix_items then
            prefix = prefix + #prefix_items
        end
        return function (schema, data, data_ptr, evaluated)
            local outputs
            local valid = true
            if is_array(data) then
                outputs = {}
                for i = prefix, #data do
                    local output = validator:_validate(data[i], data_ptr, tostring(i - 1))
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                end
                for i = prefix, #data do
                    evaluated[i] = valid
                end
            end
            return schema:_merge_output(valid, outputs, 'items', data_ptr)
        end
    else
        -- draft2019-09
        local validators = {}
        for i = 1, #items do
            validators[i] = core._new(items[i], parent, 'items', tostring(i - 1))
        end
        local extra = schema_p.additionalItems
        if extra ~= nil then
            local validator_extra = core._new(extra, parent, 'additionalItems')
            setmetatable(validators, { __index = function () return validator_extra end })
        end
        return function (schema, data, data_ptr, evaluated)
            local outputs
            local valid = true
            if is_array(data) then
                outputs = {}
                for i = 1, #data do
                    local validator = validators[i]
                    if not validator then
                        break
                    end
                    local output = validator:_validate(data[i], data_ptr, tostring(i - 1))
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                    evaluated[i] = output.valid
                end
            end
            return schema:_merge_output(valid, outputs, 'items', data_ptr)
        end
    end
end

keyword.contains = function (cont, parent)  -- minContains, maxContains
    local is_array = core.json.is_array or is_table
    local validator = core._new(cont, parent, 'contains')
    local schema_p = parent.schema
    local min = schema_p.minContains or 1
    local max = schema_p.maxContains
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        local msg
        if is_array(data) then
            outputs = {}
            local nb = 0
            for i = 1, #data do
                local output = validator:_validate(data[i], data_ptr, tostring(i - 1))
                outputs[#outputs+1] = output
                if output.valid then
                    nb = nb + 1
                    evaluated[i] = output.valid
                end
            end
            if nb < min then
                valid = false
                msg = "must contain at least " .. tostring(min) .. " valid item(s)"
            elseif max and nb > max then
                valid = false
                msg = "must contain at least " .. tostring(min) .. " and no more than " .. tostring(max) .. " valid item(s)"
            end
        end
        return schema:_merge_output(valid, outputs, 'contains', data_ptr, msg)
    end
end

keyword.properties = function (props, parent)
    local is_object = core.json.is_object or is_table
    local validators = {}
    for k, schema in pairs(props) do
        validators[k] = core._new(schema, parent, 'properties', escape(k))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(data) do
                local validator = validators[k]
                if validator then
                    local output = validator:_validate(v, data_ptr, escape(k))
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                    evaluated[k] = output.valid
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'properties', data_ptr)
    end
end

keyword.patternProperties = function (props, parent)
    local is_object = core.json.is_object or is_table
    local regex = {}
    local validators = {}
    for patt, schema in pairs(props) do
        regex[patt] = pcre_new(patt)
        validators[patt] = core._new(schema, parent, 'patternProperties', escape(patt))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(data) do
                if type(k) == 'string' then
                    for patt, validator in pairs(validators) do
                        if regex[patt]:match(k) then
                            local output = validator:_validate(v, data_ptr, escape(k))
                            outputs[#outputs+1] = output
                            valid = valid and output.valid
                            evaluated[k] = output.valid
                        end
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'patternProperties', data_ptr)
    end
end

keyword.luaPatternProperties = function (props, parent)
    local is_object = core.json.is_object or is_table
    local validators = {}
    for k, schema in pairs(props) do
        validators[k] = core._new(schema, parent, 'luaPatternProperties', escape(k))
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(data) do
                if type(k) == 'string' then
                    for patt, validator in pairs(validators) do
                        if match(k, patt) then
                            local output = validator:_validate(v, data_ptr, escape(k))
                            outputs[#outputs+1] = output
                            valid = valid and output.valid
                            evaluated[k] = output.valid
                        end
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'luaPatternProperties', data_ptr)
    end
end

keyword.additionalProperties = function (add, parent)
    local is_object = core.json.is_object or is_table
    local validator = core._new(add, parent, 'additionalProperties')
    local schema_p = parent.schema
    local properties = schema_p.properties or {}
    local pattern_properties = schema_p.patternProperties or {}
    local lua_pattern_properties = schema_p.luaPatternProperties or {}
    local regex = {}
    for patt in pairs(pattern_properties) do
        regex[patt] = pcre_new(patt)
    end
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(data) do
                if type(k) == 'string' then
                    local found = false
                    if properties[k] then
                        found = true
                    else
                        for patt in pairs(pattern_properties) do
                            if regex[patt]:match(k) then
                                found = true
                            end
                        end
                        for patt in pairs(lua_pattern_properties) do
                            if match(k, patt) then
                                found = true
                            end
                        end
                    end
                    if not found then
                        local output = validator:_validate(v, data_ptr, escape(k))
                        outputs[#outputs+1] = output
                        valid = valid and output.valid
                        evaluated[k] = output.valid
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'additionalProperties', data_ptr)
    end
end

keyword.dependencies = function (depend, parent) -- draft-07
    local is_array = core.json.is_array or is_table
    local is_object = core.json.is_object or is_table
    local validators = {}
    for k, v in pairs(depend) do
        if not is_table(v) or not is_array(v) then
            validators[k] = core._new(v, parent, 'dependencies', escape(k))
        elseif #v == 0 then
            validators[k] = core._new(true, parent, 'dependencies', escape(k))
        else
            validators[k] = v
        end
    end
    return function (schema, data, data_ptr)
        local outputs
        local valid = true
        local msg
        if is_object(data) then
            outputs = {}
            for k, v in pairs(validators) do
                if data[k] then
                    if v._validate then
                        local output = v:_validate(data, data_ptr, escape(k))
                        outputs[#outputs+1] = output
                        valid = valid and output.valid
                    else
                        for i = 1, #v do
                            local r = v[i]
                            if data[r] == nil then
                                msg = "must have property " .. r .. " when property " .. k .. " is present"
                                valid = false
                            end
                        end
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'dependencies', data_ptr, msg)
    end
end

keyword.propertyNames = function (name, parent)
    local is_object = core.json.is_object or is_table
    local validator = core._new(name, parent, 'propertyNames')
    return function (schema, data, data_ptr)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k in pairs(data) do
                if type(k) == 'string' then
                    local output = validator:_validate(k, data_ptr, escape(k))
                    output.annotations = nil
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'propertyNames', data_ptr)
    end
end

--[[ 12. Keywords for Unevaluated Locations ]]

keyword.unevaluatedItems = function (uneval, parent)
    local is_array = core.json.is_array or is_table
    local validator = core._new(uneval, parent, 'unevaluatedItems')
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_array(data) then
            outputs = {}
            for i = 1, #data do
                if not evaluated[i] then
                    local output = validator:_validate(data[i], data_ptr, tostring(i - 1))
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                    evaluated[i] = output.valid
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'unevaluatedItems', data_ptr)
    end
end

keyword.unevaluatedProperties = function (uneval, parent)
    local is_object = core.json.is_object or is_table
    local validator = core._new(uneval, parent, 'unevaluatedProperties')
    return function (schema, data, data_ptr, evaluated)
        local outputs
        local valid = true
        if is_object(data) then
            outputs = {}
            for k, v in pairs(data) do
                if not evaluated[k] then
                    local output = validator:_validate(v, data_ptr, escape(k))
                    outputs[#outputs+1] = output
                    valid = valid and output.valid
                    evaluated[k] = output.valid
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'unevaluatedProperties', data_ptr)
    end
end

--[[
        JSON Schema Validation: A Vocabulary for Structural Validation of JSON

]]

--[[ 7. Keywords for Structural Validation ]]

--[[ 7.1. Validation Keywords for Any Instance Type ]]

keyword.type = function (type_)
    local json = core.json
    local null = json.null
    local isa = {
        boolean      = function (data) return type(data) == 'boolean' end,
        cdata        = function (data) return type(data) == 'cdata' end,
        ['function'] = function (data) return type(data) == 'function' end,
        ['nil']      = function (data) return data == nil end,
        null         = function (data) return data == null end,
        number       = function (data) return type(data) == 'number' end,
        integer      = function (data) return type(data) == 'number' and (tointeger(data) and true or false) end,
        string       = function (data) return type(data) == 'string' end,
        array        = json.is_array or is_table,
        object       = json.is_object or is_table,
        table        = is_table,
        thread       = function (data) return type(data) == 'thread' end,
        userdata     = function (data) return type(data) == 'userdata' end,
    }

    if type(type_) == 'string' then
        local fn = isa[type_]
        return function (schema, data, data_ptr)
            local msg
            local valid = fn and fn(data)
            if not valid then
                msg = "must be " .. type_
            end
            return schema:_mk_output(valid, msg, 'type', data_ptr, schema.validation)
        end
    else
        return function (schema, data, data_ptr)
            local msg
            local valid = false
            for i = 1, #type_ do
                local fn = isa[type_[i]]
                if fn and fn(data) then
                    valid = true
                    break
                end
            end
            if not valid then
                msg = "must be " .. tconcat(type_, ', ')
            end
            return schema:_mk_output(valid, msg, 'type', data_ptr, schema.validation)
        end
    end
end

keyword.enum = function (enum)
    return function (schema, data, data_ptr)
        local valid = false
        local msg
        local type_ = type(data)
        for i = 1, #enum do
            local elt = enum[i]
            if type_ == type(elt) then
                if type_ == 'table' then
                    if same(data, elt) then
                        valid = true
                        break
                    end
                else
                    if data == elt then
                        valid = true
                        break
                    end
                end
            end
        end
        if not valid then
            msg = "must be equal to one of the allowed values"
        end
        return schema:_mk_output(valid, msg, 'enum', data_ptr, schema.validation)
    end
end

keyword.const = function (cst)
    if type(cst) == 'table' then
        return function (schema, data, data_ptr)
            local msg
            local valid = same(data, cst)
            if not valid then
                msg = "must be equal to constant"
            end
            return schema:_mk_output(valid, msg, 'const', data_ptr, schema.validation)
        end
    else
        return function (schema, data, data_ptr)
            local msg
            local valid = data == cst
            if not valid then
                msg = "must be equal to constant"
            end
            return schema:_mk_output(valid, msg, 'const', data_ptr, schema.validation)
        end
    end
end

--[[ 7.2. Validation Keywords for Numeric Instances ]]

keyword.multipleOf = function (multiple)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'number' then
            valid = (data % multiple) == 0
            if not valid then
                msg = "must be multiple of " .. tostring(multiple)
            end
        end
        return schema:_mk_output(valid, msg, 'multipleOf', data_ptr, schema.validation)
    end
end

keyword.maximum = function (max, parent)
    local schema_p = parent.schema
    if schema_p.exclusiveMaximum ~= true then
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data <= max
            end
            if not valid then
                msg = "must be <= " .. tostring(max)
            end
            return schema:_mk_output(valid, msg, 'maximum', data_ptr, schema.validation)
        end
    else
        -- draft-04
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data < max
            end
            if not valid then
                msg = "must be < " .. tostring(max)
            end
            return schema:_mk_output(valid, msg, 'maximum', data_ptr, schema.validation)
        end
    end
end

keyword.exclusiveMaximum = function (max)
    if type(max) == 'number' then
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data < max
            end
            if not valid then
                msg = "must be < " .. tostring(max)
            end
            return schema:_mk_output(valid, msg, 'exclusiveMaximum', data_ptr, schema.validation)
        end
    end
end

keyword.minimum = function (min, parent)
    local schema_p = parent.schema
    if schema_p.exclusiveMinimum ~= true then
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data >= min
            end
            if not valid then
                msg = "must be >= " .. tostring(min)
            end
            return schema:_mk_output(valid, msg, 'minimum', data_ptr, schema.validation)
        end
    else
        -- draft-04
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data > min
            end
            if not valid then
                msg = "must be > " .. tostring(min)
            end
            return schema:_mk_output(valid, msg, 'minimum', data_ptr, schema.validation)
        end
    end
end

keyword.exclusiveMinimum = function (min)
    if type(min) == 'number' then
        return function (schema, data, data_ptr)
            local valid = true
            local msg
            if type(data) == 'number' then
                valid = data > min
            end
            if not valid then
                msg = "must be > " .. tostring(min)
            end
            return schema:_mk_output(valid, msg, 'exclusiveMinimum', data_ptr, schema.validation)
        end
    end
end

--[[ 7.3. Validation Keywords for Strings ]]

keyword.maxLength = function (max)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'string' then
            valid = len(data) <= max
        end
        if not valid then
            msg = "must NOT have more than " .. tostring(max) .. " characters"
        end
        return schema:_mk_output(valid, msg, 'maxLength', data_ptr, schema.validation)
    end
end

keyword.minLength = function (min)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'string' then
            valid = len(data) >= min
        end
        if not valid then
            msg = "must NOT have fewer than " .. tostring(min) .. " characters"
        end
        return schema:_mk_output(valid, msg, 'minLength', data_ptr, schema.validation)
    end
end

keyword.pattern = function (patt)
    local regex = pcre_new(patt)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'string' then
            valid = regex:match(data) and true or false
        end
        if not valid then
            msg = "must match pattern " .. patt
        end
        return schema:_mk_output(valid, msg, 'pattern', data_ptr, schema.validation)
    end
end

keyword.luaPattern = function (patt)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'string' then
            valid = match(data, patt) and true or false
        end
        if not valid then
            msg = "must match pattern " .. patt
        end
        return schema:_mk_output(valid, msg, 'luaPattern', data_ptr, schema.validation)
    end
end

keyword.lpegPattern = function (patt)
    local regex = re_compile(patt)
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if type(data) == 'string' then
            valid = regex:match(data) and true or false
        end
        if not valid then
            msg = "must match pattern " .. patt
        end
        return schema:_mk_output(valid, msg, 'lpegPattern', data_ptr, schema.validation)
    end
end

--[[ 7.4. Validation Keywords for Arrays ]]

keyword.maxItems = function (max)
    local is_array = core.json.is_array or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if is_array(data) then
            valid = #data <= max
        end
        if not valid then
            msg = "must NOT have more than " .. tostring(max) .. " items"
        end
        return schema:_mk_output(valid, msg, 'maxItems', data_ptr, schema.validation)
    end
end

keyword.minItems = function (min)
    local is_array = core.json.is_array or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if is_array(data) then
            valid = #data >= min
        end
        if not valid then
            msg = "must NOT have fewer than " .. tostring(min) .. " items"
        end
        return schema:_mk_output(valid, msg, 'minItems', data_ptr, schema.validation)
    end
end

keyword.uniqueItems = function (uniq)
    if uniq then
        local is_array = core.json.is_array or is_table
        return function (schema, data, data_ptr)
            local valid = true
            local outputs = {}
            if is_array(data) then
                local found = {}
                local tfound = {}
                local hnum = {}
                for i = 1, #data do
                    local v = data[i]
                    if type(v) == 'table' then
                        tfound[#tfound+1] = v
                        hnum[#tfound] = i
                    else
                        if found[v] then
                            valid = false
                            local msg = "must NOT have duplicate items (items ## " .. tostring(found[v]) .. " and " .. tostring(i) .. " are identical)"
                            outputs[#outputs+1] = schema:_mk_output(false, msg, 'uniqueItems', data_ptr, schema.validation)
                        end
                        found[v] = i
                    end
                end
                for i = 1, #tfound - 1 do
                    for j = i + 1, #tfound do
                        if same(tfound[i], tfound[j]) then
                            valid = false
                            local msg = "must NOT have duplicate items (items ## " .. tostring(hnum[i]) .. " and " .. tostring(hnum[j]) .. " are identical)"
                            outputs[#outputs+1] = schema:_mk_output(false, msg, 'uniqueItems', data_ptr, schema.validation)
                        end
                    end
                end
            end
            return schema:_merge_output(valid, outputs, 'uniqueItems', data_ptr)
        end
    end
end

--[[ 7.5. Validation Keywords for Objects ]]

keyword.maxProperties = function (max)
    local is_object = core.json.is_object or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if is_object(data) then
            local nb = 0
            for _ in pairs(data) do
                nb = nb + 1
            end
            valid = nb <= max
        end
        if not valid then
            msg = "must NOT have more than " .. tostring(max) .. " properties"
        end
        return schema:_mk_output(valid, msg, 'maxProperties', data_ptr, schema.validation)
    end
end

keyword.minProperties = function (min)
    local is_object = core.json.is_object or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local msg
        if is_object(data) then
            local nb = 0
            for _ in pairs(data) do
                nb = nb + 1
            end
            valid = nb >= min
        end
        if not valid then
            msg = "must NOT have fewer than " .. tostring(min) .. " properties"
        end
        return schema:_mk_output(valid, msg, 'minProperties', data_ptr, schema.validation)
    end
end

keyword.required = function (required)
    local is_object = core.json.is_object or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local outputs = {}
        if is_object(data) then
            for i = 1, #required do
                local r = required[i]
                if data[r] == nil then
                    valid = false
                    local msg = "must have required property " .. r
                    outputs[#outputs+1] = schema:_mk_output(false, msg, 'required', data_ptr, schema.validation)
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'required', data_ptr)
    end
end

keyword.dependentRequired = function (depend)
    local is_object = core.json.is_object or is_table
    return function (schema, data, data_ptr)
        local valid = true
        local outputs = {}
        if is_object(data) then
            for k, required in pairs(depend) do
                if data[k] then
                    for i = 1, #required do
                        local r = required[i]
                        if data[r] == nil then
                            valid = false
                            local msg = "must have property " .. r .. " when property " .. k .. " is present"
                            outputs[#outputs+1] = schema:_mk_output(false, msg, 'dependentRequired', data_ptr, schema.validation)
                        end
                    end
                end
            end
        end
        return schema:_merge_output(valid, outputs, 'dependentRequired', data_ptr)
    end
end

--[[ 8. Semantic Content With format ]]

keyword.format = function (fmt)
    local fn = core.custom_format[fmt] or core.format[fmt]
    if fn then
        return function (schema, data, data_ptr)
            local valid, err = fn(data)
            local annotation = schema.format_annotation and not schema.format_assertion
            return schema:_mk_output(valid, err or fmt, 'format', data_ptr, not annotation)
        end
    else
        return function (schema, _, data_ptr)
            local err = 'unknown format: ' .. fmt
            local annotation = schema.format_annotation and not schema.format_assertion
            return schema:_mk_output(false, err, 'format', data_ptr, not annotation)
        end
    end
end

--[[ 9. Keywords for the Contents of String-Encoded Data ]]

keyword.contentEncoding = function (anno)
    return function (schema, data, data_ptr)
        return schema:_mk_output_anno('contentEncoding', data_ptr, type(data) == 'string' and anno or nil)
    end
end

keyword.contentMediaType = function (anno)
    return function (schema, data, data_ptr)
        return schema:_mk_output_anno('contentMediaType', data_ptr, type(data) == 'string' and anno or nil)
    end
end

keyword.contentSchema = function (anno, parent)
    local schema_p = parent.schema
    if schema_p.contentMediaType then
        return function (schema, data, data_ptr)
            return schema:_mk_output_anno('contentSchema', data_ptr, type(data) == 'string' and anno or nil)
        end
    end
end

--[[10. Keywords for Basic Meta-Data Annotations ]]

-- title
-- description
-- default
-- deprecated
-- readOnly
-- writeOnly
-- examples

return setmetatable(keyword, {
    __index = function (_, kw)
        return function (anno)
            return function (schema, _, data_ptr)
                return schema:_mk_output_anno(kw, data_ptr, anno)
            end
        end
    end,
})
--
-- Copyright (c) 2025-2026 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
