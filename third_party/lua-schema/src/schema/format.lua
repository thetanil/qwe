
--
-- lua-schema : <https://fperrad.frama.io/lua-schema>
--

local assert = assert
local pairs = pairs
local pcall = pcall
local rawset = rawset
local tonumber = tonumber
local type = type
local date = os.date
local time = os.time
local lpeg = require'lpeg'
local re_compile = require're'.compile
local has_pcre, pcre = pcall(require, 'rex_pcre2')
local pcre_new = has_pcre and pcre.new

local match = lpeg.match
local B = lpeg.B  -- behind
local C = lpeg.C  -- capture
local P = lpeg.P
local R = lpeg.R  -- range
local S = lpeg.S  -- set
local V = lpeg.V  -- variable
local utfR = lpeg.utfR -- utf8 range

local Cc = lpeg.Cc  -- constant capture
local Ct = lpeg.Ct  -- table capture

local _ENV = nil

    -- rfc 2234

local ALPHA          = R('AZ', 'az')
local DIGIT          = R'09'
local DQUOTE         = P'"'
local HEXDIG         = R('09', 'AF', 'af')

    -- rfc 3339

local function check_date_time (capt)
    local t1 = capt.year and {} or date('*t')
    for k, v in pairs(capt) do
        t1[k] = v
    end
    local epoch = assert(time(t1))
    local t2 = date('*t', epoch)
    for k, v in pairs(capt) do
        assert(t2[k] == v, k)
    end
    return epoch
end

local date_fullyear   = (Cc'year' * ((DIGIT * DIGIT * DIGIT * DIGIT) / tonumber)) % rawset
local date_month      = (Cc'month' * ((DIGIT * DIGIT) / tonumber)) % rawset
local date_mday       = (Cc'day' * ((DIGIT * DIGIT) / tonumber)) % rawset
local time_hour       = (Cc'hour' * ((DIGIT * DIGIT) / tonumber)) % rawset
local time_minute     = (Cc'min' * ((DIGIT * DIGIT) / tonumber)) % rawset
local time_second     = (Cc'sec' * ((DIGIT * DIGIT) / tonumber)) % rawset
local time_secfrac    = P'.' * DIGIT^1
local time_numoffset  = S"+-" * ( R'01' * DIGIT + P'2' * R'03' )* P':' * R'05' * DIGIT
local time_offset     = S"Zz" + time_numoffset
local partial_time    = time_hour * P':' * time_minute * P':' * time_second * time_secfrac^-1
local full_date       = Ct(date_fullyear * P'-' * date_month * P'-' * date_mday) / check_date_time
local full_time       = Ct(partial_time * time_offset) / check_date_time
local date_time       = Ct(date_fullyear * P'-' * date_month * P'-' * date_mday * S"Tt" * partial_time * time_offset) / check_date_time

local dur_second      = DIGIT^1 * P'S'
local dur_minute      = DIGIT^1 * P'M' * dur_second^-1
local dur_hour        = DIGIT^1 * P'H' * dur_minute^-1
local dur_time        = P'T' * (dur_hour + dur_minute + dur_second)
local dur_day         = DIGIT^1 * P'D'
local dur_week        = DIGIT^1 * P'W'
local dur_month       = DIGIT^1 * P'M' * dur_day^-1
local dur_year        = DIGIT^1 * P'Y' * dur_month^-1
local dur_date        = (dur_day + dur_month + dur_year) * dur_time^-1
local duration        = P'P' * (dur_date + dur_time + dur_week)

    -- rfc 3986

local sub_delims    = S"!$&'()*+,;="
local unreserved    = ALPHA + DIGIT + S"-._~"
local pct_encoded   = P'%' * HEXDIG * HEXDIG
local pchar         = unreserved + pct_encoded + sub_delims + S":@"
local reg_name      = ( unreserved + pct_encoded + sub_delims )^0
local dec_octet     = P'25' * R'05'
                    + P'2' * R'04' * DIGIT
                    + P'1' * DIGIT * DIGIT
                    + R'19' * DIGIT
                    + DIGIT
local IPv4address   = dec_octet * P'.' * dec_octet * P'.' * dec_octet * P'.' * dec_octet
local IPvFuture     = S"Vv" * HEXDIG^1 * P'.' * ( unreserved + sub_delims + P':' )^1
local IPv6address_   = ( HEXDIG + P':' )^1  -- good enough and fast
local h16           = HEXDIG * HEXDIG^-3
local ls32          = ( h16 * P':' * h16 ) + IPv4address
local IPv6address   = h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    +      P'::' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    +                   P'::' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    + h16             * P'::' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    +                                P'::' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    + h16                          * P'::' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    + h16 * P':' * h16             * P'::' * h16 * P':' * h16 * P':' * h16 * P':' * ls32
                    +                                             P'::' * h16 * P':' * h16 * P':' * ls32
                    + h16                                       * P'::' * h16 * P':' * h16 * P':' * ls32
                    + h16 * P':' * h16                          * P'::' * h16 * P':' * h16 * P':' * ls32
                    + h16 * P':' * h16 * P':' * h16             * P'::' * h16 * P':' * h16 * P':' * ls32
                    +                                                          P'::' * h16 * P':' * ls32
                    + h16                                                    * P'::' * h16 * P':' * ls32
                    + h16 * P':' * h16                                       * P'::' * h16 * P':' * ls32
                    + h16 * P':' * h16 * P':' * h16                          * P'::' * h16 * P':' * ls32
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16             * P'::' * h16 * P':' * ls32
                    +                                                                       P'::' * ls32
                    + h16                                                                 * P'::' * ls32
                    + h16 * P':' * h16                                                    * P'::' * ls32
                    + h16 * P':' * h16 * P':' * h16                                       * P'::' * ls32
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16                          * P'::' * ls32
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16             * P'::' * ls32
                    +                                                                               P'::' * h16
                    + h16                                                                         * P'::' * h16
                    + h16 * P':' * h16                                                            * P'::' * h16
                    + h16 * P':' * h16 * P':' * h16                                               * P'::' * h16
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16                                  * P'::' * h16
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16                     * P'::' * h16
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16        * P'::' * h16
                    +                                                                                       P'::'
                    + h16                                                                                 * P'::'
                    + h16 * P':' * h16                                                                    * P'::'
                    + h16 * P':' * h16 * P':' * h16                                                       * P'::'
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16                                          * P'::'
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16                             * P'::'
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16                * P'::'
                    + h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16 * P':' * h16   * P'::'
local IP_literal    = P'[' * ( IPv6address_ + IPvFuture  ) * P']'
local host          = IP_literal + IPv4address + reg_name
local userinfo      = ( unreserved + pct_encoded + sub_delims + P':' )^0
local port          = DIGIT^0
local authority     = ( userinfo * P'@' )^-1 * host * ( P':' * port )^-1
local segment       = pchar^0
local segment_nz    = pchar^1
local segment_nz_nc = ( unreserved + pct_encoded + sub_delims + P'@' )^1
local path_abempty  = ( P'/' * segment )^0
local path_absolute = P'/' * ( segment_nz * ( P'/' * segment )^0 )^-1
local path_noscheme = segment_nz_nc * ( P'/' * segment )^0
local path_rootless = segment_nz * ( P'/' * segment )^0
local path_empty    = P(0)
local scheme        = ALPHA * ( ALPHA + DIGIT + S"+-.-" )^0
local hier_part     = P'//' * authority * path_abempty
                    + path_absolute
                    + path_rootless
                    + path_empty
local query         = ( pchar + S"/?" )^0
local fragment      = ( pchar + S"/?" )^0
local uri           = scheme * P':' * hier_part * ( P'?' * query )^-1 * ( P'#' * fragment )^-1

local relative_part = P'//' * authority * path_abempty
                    + path_absolute
                    + path_noscheme
                    + path_empty
local relative_ref  = relative_part * ( P'?' * query )^-1 * ( P'#' * fragment )^-1
local uri_reference = uri + relative_ref

    -- rfc 3987

local ucschar        = utfR(0xA0, 0xD7FF) + utfR(0xF900, 0xFDCF) + utfR(0xFDF0, 0xFFEF)
                     + utfR(0x10000, 0x1FFFD) + utfR(0x20000, 0x2FFFD) + utfR(0x30000, 0x3FFFD)
                     + utfR(0x40000, 0x4FFFD) + utfR(0x50000, 0x5FFFD) + utfR(0x60000, 0x6FFFD)
                     + utfR(0x70000, 0x7FFFD) + utfR(0x80000, 0x8FFFD) + utfR(0x90000, 0x9FFFD)
                     + utfR(0xA0000, 0xAFFFD) + utfR(0xB0000, 0xBFFFD) + utfR(0xC0000, 0xCFFFD)
                     + utfR(0xD0000, 0xDFFFD) + utfR(0xE1000, 0xEFFFD)
local iprivate       = utfR(0xE000, 0xF8FF) + utfR(0xF0000, 0xFFFFD) + utfR(0x100000, 0x10FFFD)
local iunreserved    = ALPHA + DIGIT + S"-._~" + ucschar
local ipchar         = iunreserved + pct_encoded + sub_delims + S":@"
local ireg_name      = ( iunreserved + pct_encoded + sub_delims )^0
local ihost          = IP_literal + IPv4address + ireg_name
local iuserinfo      = ( iunreserved + pct_encoded + sub_delims + P':' )^0
local iauthority     = ( iuserinfo * P'@' )^-1 * ihost * ( P':' * port )^-1
local isegment       = ipchar^0
local isegment_nz    = ipchar^1
local isegment_nz_nc = ( iunreserved + pct_encoded + sub_delims + P'@' )^1
local ipath_abempty  = ( P'/' * isegment )^0
local ipath_absolute = P'/' * ( isegment_nz * ( P'/' * isegment )^0 )^-1
local ipath_noscheme = isegment_nz_nc * ( P'/' * isegment )^0
local ipath_rootless = isegment_nz * ( P'/' * isegment )^0
local ipath_empty    = P(0)
local ihier_part     = P'//' * iauthority * ipath_abempty
                     + ipath_absolute
                     + ipath_rootless
                     + ipath_empty
local iquery         = ( ipchar + iprivate + S"/?" )^0
local ifragment      = ( ipchar + S"/?" )^0
local iri            = scheme * P':' * ihier_part * ( P'?' * iquery )^-1 * ( P'#' * ifragment )^-1

local irelative_part = P'//' * iauthority * ipath_abempty
                     + ipath_absolute
                     + ipath_noscheme
                     + ipath_empty
local irelative_ref  = irelative_part * ( P'?' * iquery )^-1 * ( P'#' * ifragment )^-1
local iri_reference  = iri + irelative_ref

    -- rfc 4122

local hexoctet2 = HEXDIG * HEXDIG * HEXDIG * HEXDIG
local uuid      = hexoctet2 * hexoctet2
                * P'-' * hexoctet2
                * P'-' * hexoctet2
                * P'-' * hexoctet2
                * P'-' * hexoctet2 * hexoctet2 * hexoctet2

    -- rfc 5321

local atext           = ALPHA + DIGIT + S"_+~-"
local atom            = atext^1
local dot_string      = atom * ( P'.' * atom )^0
local qcontentSMTP    = R('\32\33', '\35\91', '\93\126') + P'\92' * R'\32\126'
local quoted_string   = DQUOTE * qcontentSMTP^0 * DQUOTE
local local_part      = quoted_string + dot_string
local let_dig         = ALPHA + DIGIT
local ldh_str         = ( ALPHA + DIGIT + P'-' )^0 * B(let_dig)
local nrldh           = let_dig * ldh_str^-1
local xn_label        = S'Xx' * S'Nn' * P'--' * R'\0\127'^0
local sub_domain      = C( xn_label + nrldh ) / function (capt) assert(#capt <= 63, 'too long label') return capt end
local domain          = C( sub_domain * ( P'.' * sub_domain )^0 ) / function (capt) assert(#capt <= 253, 'too long domain') return capt end
local address_literal = P'[' * ( IPv4address + S"Ii" * S"Pp" * S"Vv" * P'6:' * IPv6address ) * P']'
local mailbox         = local_part * P'@' * ( domain + address_literal )

    -- rfc 6570 (+ errata 6937)

local operator      = S"+#./;?&=,!@|"
local varchar       = ALPHA + DIGIT + P'_' + pct_encoded
local varname       = varchar * ( P'.'^-1 * varchar )^0
local varspec       = varname * ( P'*' + P':' * R'19' * DIGIT^-3 )^-1
local variable_list = varspec * ( P',' * varspec )^0
local expression    = P'{' * operator^-1 * variable_list * P'}'
local literals      = ( R'\33\126' - S"\"%<>\\^`{|}" ) + pct_encoded + iprivate + ucschar
local uri_template  = ( expression + literals )^0

    -- rfc 6901

local reference_token = ( utfR(0x00, 0x2E) + utfR(0x30, 0x7D) + utfR(0x7F, 0x10FFFF) + P'~' * R'01' )^0
local json_pointer    = ( P'/' * reference_token )^0

    -- relative-json-pointer

local non_negative_integer  = P'0' + R'19' * R'09'^0
local index_manipulation    = S"+-" * non_negative_integer
local relative_json_pointer = non_negative_integer * ( P'#' + index_manipulation^-1 * json_pointer )

    -- lua

local lua_regex = P{
    'top';
    top = P'^'^-1 * V'pattern'^0 * P'$'^-1,
    pattern = V'capture' + V'item'^1,
    capture = P'(' * V'pattern'^0 * P')',
    item = V'class' * S"*+-?"^-1
         + P'%' * ( R'19'
                  + P'b' * P(2)
                  + P'f[' * V'set' * P']' ),
    class = V'not_magic' + V'escaped' + ( P'[' * P'^'^-1 * V'set' * P']' ),
    set = S"]-"^-1 * ( V'range' + V'escaped' + V'not_magic' )^1 * P'-'^-1,
    range = V'not_magic' * P'-' * V'not_magic',
    escaped = P'%' * ( S"ACDGLPSUWXZacdglpsuwxz" + ( P(1) - R('09', 'AZ', 'az') ) ),
    not_magic = P(1) - S"^$()%[]*+-?",
}

local format = {}

--[[
        JSON Schema Validation: A Vocabulary for Structural Validation of JSON

]]

--[[ 8. Semantic Content With format ]]

format['date-time'] = function (data)
    if type(data) == 'string' then
        local patt = date_time * -1
        local ok, result = pcall(match, patt, data)
        if not ok or result == nil then
            return false, result or 'invalid date-time'
        end
    end
    return true
end

format.date = function (data)
    if type(data) == 'string' then
        local patt = full_date * -1
        local ok, result = pcall(match, patt, data)
        if not ok or result == nil then
            return false, result or 'invalid date'
        end
    end
    return true
end

format.time = function (data)
    if type(data) == 'string' then
        local patt = full_time * -1
        local ok, result = pcall(match, patt, data)
        if not ok or result == nil then
            return false, result or 'invalid time'
        end
    end
    return true
end

format.duration = function (data)
    if type(data) == 'string' then
        local patt = duration * -1
        if not match(patt, data) then
            return false, 'invalid duration'
        end
    end
    return true
end

format.email = function (data)
    if type(data) == 'string' then
        local patt = mailbox * -1
        local ok, result = pcall(match, patt, data)
        if not ok or result == nil then
            return false, result or 'invalid email'
        end
    end
    return true
end

format['idn-email'] = function (data)
    -- RFC 6531
    if type(data) == 'string' then
        -- TODO
        return true
    end
    return true
end

format.hostname = function (data)
    if type(data) == 'string' then
        local patt = domain * -1
        local ok, result = pcall(match, patt, data)
        if not ok or result == nil then
            return false, result or 'invalid hostname'
        end
    end
    return true
end

format['idn-hostname'] = function (data)
    -- RFC 5890
    if type(data) == 'string' then
        -- TODO
        return true
    end
    return true
end

format.ipv4 = function (data)
    if type(data) == 'string' then
        local patt = IPv4address * -1
        if not match(patt, data) then
            return false, 'invalid ipv4'
        end
    end
    return true
end

format.ipv6 = function (data)
    if type(data) == 'string' then
        local patt = IPv6address * -1
        if not match(patt, data) then
            return false, 'invalid ipv6'
        end
    end
    return true
end

format.uri = function (data)
    if type(data) == 'string' then
        local patt = uri * -1
        if not match(patt, data) then
            return false, 'invalid uri'
        end
    end
    return true
end

format['uri-reference'] = function (data)
    if type(data) == 'string' then
        local patt = uri_reference * -1
        if not match(patt, data) then
            return false, 'invalid uri-reference'
        end
    end
    return true
end

format.iri = function (data)
    if type(data) == 'string' then
        local patt = iri * -1
        if not match(patt, data) then
            return false, 'invalid iri'
        end
    end
    return true
end

format['iri-reference'] = function (data)
    if type(data) == 'string' then
        local patt = iri_reference * -1
        if not match(patt, data) then
            return false, 'invalid iri-reference'
        end
    end
    return true
end

format.uuid = function (data)
    if type(data) == 'string' then
        local patt = uuid * -1
        if #data ~= 36 or not match(patt, data) then
            return false, 'invalid uuid'
        end
    end
    return true
end

format['uri-template'] = function (data)
    if type(data) == 'string' then
        local patt = uri_template * -1
        if not match(patt, data) then
            return false, 'invalid uri-template'
        end
    end
    return true
end

format['json-pointer'] = function (data)
    if type(data) == 'string' then
        local patt = json_pointer * -1
        if not match(patt, data) then
            return false, 'invalid json-pointer'
        end
    end
    return true
end

format['relative-json-pointer'] = function (data)
    if type(data) == 'string' then
        local patt = relative_json_pointer * -1
        if not match(patt, data) then
            return false, 'invalid relative-json-pointer'
        end
    end
    return true
end

format.regex = function (data)
    if type(data) == 'string' and pcre_new then
        local ok, err = pcall(pcre_new, data)
        if not ok then
            -- rex_pcre2/PCRE2 raise the same way for a syntactically bad pattern and for
            -- a failed allocation ("malloc failed" from rex_pcre2's own checks, "failed to
            -- allocate heap memory" or similar from PCRE2's own error text). The first is
            -- what this pcall is for (report it as a soft format failure); the second must
            -- not be swallowed as one -- qwe's validator treats every allocation failure as
            -- fatal (ticket 17; see src/kernel/validate.c's report_internal_error), never a
            -- degraded result.
            if type(err) == 'string' and (err:find('malloc failed', 1, true) or err:find('memory', 1, true)) then
                error(err, 0)
            end
            return false, 'invalid regex'
        end
    end
    return true
end

format['lua-regex'] = function (data)
    if type(data) == 'string' then
        local patt = lua_regex * -1
        if not match(patt, data) then
            return false, 'invalid lua-regex'
        end
    end
    return true
end

format['lpeg-regex'] = function (data)
    if type(data) == 'string' then
        local ok, result = pcall(re_compile, data)
        if not ok or result == nil then
            return false, result or 'invalid lpeg-regex'
        end
    end
    return true
end

return format
--
-- Copyright (c) 2025-2026 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
