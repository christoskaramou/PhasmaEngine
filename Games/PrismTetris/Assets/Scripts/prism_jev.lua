-- Runs the race driver (Bots/PhasmaGameDriver.exe, the game's one child process) and reads its output: think
-- time, calls and tokens from step and re-decide lines, its finish line, and its failure line.
local Jev = {}
Jev.__index = Jev

local DRIVER = "Bots/PhasmaGameDriver.exe"
local COST_PER_TOKEN = 0.042 / 1000000
local STOP_GRACE = 3
local KEY_FLAGS = {
    status = "--key-status",
    save = "--save-key-from-clipboard",
    remove = "--remove-key",
}

function Jev.new(port)
    return setmetatable({ port = port }, Jev):clear()
end

function Jev:clear()
    self.running = false
    self.think_ms = 0
    self.calls = 0
    self.input_tokens = 0
    self.cost = 0
    self.error = nil
    self.finished = false
    self.stop_timer = nil
    return self
end

-- Attaches to the running game: no --seed, which would reset it and end the race. Never over a key command:
-- starting the one child process kills the running one, and a save cut short could lose the saved key.
function Jev:start()
    if self.key_running then
        self.error = "Jev's key is still being saved; try again in a moment."
        return false
    end

    self:clear()
    local ok, message = proc.start(DRIVER, {
        "--url",
        "http://127.0.0.1:" .. self.port .. "/mcp",
        "--policy",
        "jev",
        "--key-source",
        "vault",
        "--steps",
        "10000",
    })
    if not ok then
        self.error = "Jev could not start: " .. tostring(message)
        return false
    end

    self.running = true
    return true
end

-- "status", "save" (the clipboard) or "remove": the driver answers with one "KEY <word>" line and never prints
-- the key. One at a time and never beside a race driver: the game runs one child process, and starting another
-- kills it.
function Jev:key(command)
    if self.running or self.key_running then
        return false
    end

    self.answer = nil
    self.key_error = nil

    local ok, message = proc.start(DRIVER, { KEY_FLAGS[command] })
    if not ok then
        self.answer = "error"
        self.key_error = "Jev's driver could not start: " .. tostring(message)
        return false
    end

    self.key_running = true
    return true
end

function Jev:add_call(tokens)
    self.calls = self.calls + 1
    self.input_tokens = self.input_tokens + tokens
    self.cost = self.input_tokens * COST_PER_TOKEN
end

function Jev:parse(line)
    local think, step_tokens = line:match("^%S+ step %d+ .*decision_ms=(%d+) input_tokens=(%d+)")
    if think then
        self.think_ms = tonumber(think)
        self:add_call(tonumber(step_tokens))
        return true
    end

    local redecide_tokens = line:match("^%S+ re%-decide %d+ .*input_tokens=(%d+)")
    if redecide_tokens then
        self:add_call(tonumber(redecide_tokens))
        return true
    end

    local answer = line:match("^KEY (%S+)")
    if answer then
        self.answer = answer
        if answer == "saved" then
            self.saved = true
        elseif answer == "missing" or answer == "removed" then
            self.saved = false
        end
        return true
    end

    if line:match("^Finished:") then
        self.finished = true
        return false
    end

    local failure = line:match("^Game driver failed: (.+)")
    if failure then
        self.error = failure
    end
    return false
end

function Jev:drain()
    local changed = false
    local line = proc.read_line()
    while line do
        changed = self:parse(line) or changed
        line = proc.read_line()
    end
    return changed
end

-- Reads new output; true when what the HUD shows changed or the driver (or a key command) stopped.
function Jev:update(dt)
    if not self.running and not self.key_running then
        return false
    end

    local changed = self:drain()
    if self.running and self.stop_timer then
        self.stop_timer = self.stop_timer - dt
        if self.stop_timer <= 0 then
            proc.stop()
        end
    end

    if proc.is_running() then
        return changed
    end

    -- A key command that ended without its KEY line failed.
    self:drain()
    if self.key_running and not self.answer then
        self.answer = "error"
    end

    self.running = false
    self.key_running = false
    self.stop_timer = nil
    return true
end

-- After a race the driver sees the game end and exits by itself; a graceful stop kills it 3 s later anyway.
function Jev:stop(graceful)
    if not self.running then
        return
    end

    if graceful then
        self.stop_timer = self.stop_timer or STOP_GRACE
        return
    end

    proc.stop()
    self.running = false
    self.stop_timer = nil
end

return Jev
