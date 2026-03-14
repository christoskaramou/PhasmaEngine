-- Shared test utilities (loaded automatically, sets global 'T')
T = {}
T.passed = 0
T.failed = 0

function T.check(name, condition)
    if condition then
        T.passed = T.passed + 1
        pe_log("[PASS] " .. name)
    else
        T.failed = T.failed + 1
        pe_error("[FAIL] " .. name)
    end
end

function T.check_data(name, actual, expected)
    local pass = true
    if #actual ~= #expected then
        pe_error("[FAIL] " .. name .. ": expected " .. #expected .. " values, got " .. #actual)
        T.failed = T.failed + 1
        return
    end
    for i = 1, #expected do
        local a = actual[i]
        local e = expected[i]
        if type(a) == "userdata" or type(e) == "userdata" then
            if tostring(a) ~= tostring(e) then
                pass = false
                pe_error("[FAIL] " .. name .. "[" .. i .. "]: " .. tostring(a) .. " ~= " .. tostring(e))
            end
        else
            local diff = math.abs(a - e)
            if diff > 0.001 then
                pass = false
                pe_error("[FAIL] " .. name .. "[" .. i .. "]: " .. tostring(a) .. " ~= " .. tostring(e))
            end
        end
    end
    if pass then
        T.passed = T.passed + 1
        pe_log("[PASS] " .. name)
    else
        T.failed = T.failed + 1
    end
end

function T.summary(suite)
    if T.failed > 0 then
        pe_error(string.format("=== %s Complete: %d passed, %d failed ===", suite, T.passed, T.failed))
    else
        pe_log(string.format("=== %s Complete: %d passed, %d failed ===", suite, T.passed, T.failed))
    end
end

function T.reset()
    T.passed = 0
    T.failed = 0
end
