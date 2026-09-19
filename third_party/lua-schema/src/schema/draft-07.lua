
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

local core = require'schema.core'

local _ENV = nil

core.default_schema = "http://json-schema.org/draft-07/schema"
core.format_assertion = false

return core.new({
    ['$schema'] = "http://json-schema.org/draft-07/schema#",
    ['$id'] = "http://json-schema.org/draft-07/schema#",
    title = "Core schema meta-schema",
    definitions = {
        schemaArray = {
            type = 'array',
            minItems = 1,
            items = { ['$ref'] = '#' },
        },
        nonNegativeInteger = {
            type = 'integer',
            minimum = 0,
        },
        nonNegativeIntegerDefault0 = {
            allOf = {
                { ['$ref'] = "#/definitions/nonNegativeInteger" },
                { default = 0 },
            },
        },
        simpleTypes = {
            enum = {
                'array',
                'boolean',
                'cdata',
                'function',
                'integer',
                'nil',
                'null',
                'number',
                'object',
                'string',
                'table',
                'thread',
                'userdata',
            },
        },
        stringArray = {
            type = 'array',
            items = { type = 'string' },
            uniqueItems = true,
            default = {},
        }
    },
    type = { 'object', 'boolean' },
    properties = {
        ['$id'] = {
            type = 'string',
            format = 'uri-reference',
        },
        ['$schema'] = {
            type = 'string',
            format = 'uri',
        },
        ['$ref'] = {
            type = 'string',
            format = 'uri-reference',
        },
        ['$comment'] = {
            type = 'string',
        },
        title = {
            type = 'string',
        },
        description = {
            type = 'string',
        },
        default = true,
        readOnly = {
            type = 'boolean',
            default = false,
        },
        writeOnly = {
            type = 'boolean',
            default = false,
        },
        examples = {
            type = 'array',
            items = true,
        },
        multipleOf = {
            type = 'number',
            exclusiveMinimum = 0,
        },
        maximum = {
            type = 'number',
        },
        exclusiveMaximum = {
            type = 'number',
        },
        minimum = {
            type = 'number',
        },
        exclusiveMinimum = {
            type = 'number',
        },
        maxLength = { ['$ref'] = "#/definitions/nonNegativeInteger" },
        minLength = { ['$ref'] = "#/definitions/nonNegativeIntegerDefault0" },
        pattern = {
            type = 'string',
            format = 'regex',
        },
        luaPattern = {
            type = 'string',
            format = 'lua-regex',
        },
        lpegPattern = {
            type = 'string',
            format = 'lpeg-regex',
        },
        additionalItems = { ['$ref'] = '#' },
        items = {
            anyOf = {
                { ['$ref'] = '#' },
                { ['$ref'] = "#/definitions/schemaArray" },
            },
            default = true,
        },
        maxItems = { ['$ref'] = "#/definitions/nonNegativeInteger" },
        minItems = { ['$ref'] = "#/definitions/nonNegativeIntegerDefault0" },
        uniqueItems = {
            type = 'boolean',
            default = false,
        },
        contains = { ['$ref'] = '#' },
        maxProperties = { ['$ref'] = "#/definitions/nonNegativeInteger" },
        minProperties = { ['$ref'] = "#/definitions/nonNegativeIntegerDefault0" },
        required = { ['$ref'] = "#/definitions/stringArray" },
        additionalProperties = { ['$ref'] = '#' },
        definitions = {
            type = 'object',
            additionalProperties = { ['$ref'] = '#' },
            default = {},
        },
        properties = {
            type = 'object',
            additionalProperties = { ['$ref'] = '#' },
            default = {},
        },
        patternProperties = {
            type = 'object',
            additionalProperties = { ['$ref'] = '#' },
            propertyNames = { format = 'regex' },
            default = {},
        },
        luaPatternProperties = {
            type = 'object',
            additionalProperties = { ['$ref'] = '#' },
            propertyNames = { format = 'lua-regex' },
            default = {},
        },
        dependencies = {
            type = 'object',
            additionalProperties = {
                anyOf = {
                    { ['$ref'] = '#' },
                    { ['$ref'] = "#/definitions/stringArray" },
                },
            },
        },
        propertyNames = { ['$ref'] = '#' },
        const = true,
        enum = {
            type = 'array',
            items = true,
            minItems = 1,
            uniqueItems = true,
        },
        type = {
            anyOf = {
                { ['$ref'] = "#/definitions/simpleTypes" },
                {
                    type = 'array',
                    items = { ['$ref'] = "#/definitions/simpleTypes" },
                    minItems = 1,
                    uniqueItems = true,
                },
            },
        },
        format = { type = 'string' },
        contentMediaType = { type = 'string' },
        contentEncoding = { type = 'string' },
        ['if'] = { ['$ref'] = '#' },
        ['then'] = { ['$ref'] = '#' },
        ['else'] = { ['$ref'] = '#' },
        allOf = { ['$ref'] = "#/definitions/schemaArray" },
        anyOf = { ['$ref'] = "#/definitions/schemaArray" },
        oneOf = { ['$ref'] = "#/definitions/schemaArray" },
        ['not'] = { ['$ref'] = '#' },
    },
    default = true,
})

--
-- Copyright (c) 2025-2026 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
