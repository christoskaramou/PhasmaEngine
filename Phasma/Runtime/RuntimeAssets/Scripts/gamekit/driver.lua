-- Named, versioned game actions for MCP. Games own their controller and clock.
local driver = {}
local Controller = {}
Controller.__index = Controller

local TOOL_NAMES = { "game_describe", "game_observe", "game_act", "game_reset" }
local MAX_SEED = 2147483647
local MAX_STATE = 9007199254740991

local function failure(code, message)
    return { ok = false, error = { code = code, message = message } }
end

local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end

local function integer(value, low, high)
    return finite(value) and value % 1 == 0 and value >= low and value <= high
end

local function bounded_string(value, maximum)
    return type(value) == "string" and #value > 0 and #value <= maximum
end

local function request(args, allowed)
    if type(args) ~= "table" then
        return false
    end

    for key in pairs(args) do
        if not allowed[key] then
            return false
        end
    end

    return true
end

local function new_session(id)
    _G.__gamekit_driver_sequence = (_G.__gamekit_driver_sequence or 0) + 1
    return id .. ":" .. tostring({}):gsub("table: ", "") .. ":" .. _G.__gamekit_driver_sequence
end

local function legal_actions(actions)
    if type(actions) ~= "table" or #actions > 256 then
        return nil, "actions must be an array of at most 256 named actions"
    end

    local ids = {}
    local count = 0
    for index, action in pairs(actions) do
        if not integer(index, 1, #actions) or type(action) ~= "table" then
            return nil, "actions must be a dense array"
        end

        if not bounded_string(action.id, 128) or not bounded_string(action.description, 512) then
            return nil, "each action needs a bounded id and description"
        end

        if ids[action.id] or (action.score ~= nil and not finite(action.score)) then
            return nil, "action ids must be unique and optional scores must be finite"
        end

        ids[action.id] = true
        count = count + 1
    end

    if count ~= #actions then
        return nil, "actions must be a dense array"
    end

    return ids
end

function driver.new(spec)
    assert(type(spec) == "table", "driver.new requires a game specification")
    assert(bounded_string(spec.id, 96), "driver id must contain 1..96 characters")
    assert(
        bounded_string(spec.description, 2048),
        "driver description must contain 1..2048 characters"
    )
    assert(
        type(spec.observe) == "function" and type(spec.act) == "function",
        "driver needs observe and act callbacks"
    )
    assert(spec.reset == nil or type(spec.reset) == "function", "driver reset must be a callback")
    assert(spec.state_key == nil or type(spec.state_key) == "function", "driver state_key must be a callback")

    return setmetatable({
        spec = spec,
        session_id = new_session(spec.id),
        state_id = 1,
        _attached = false,
    }, Controller)
end

function Controller:describe()
    return {
        protocol_version = 1,
        game_id = self.spec.id,
        description = self.spec.description,
        reset_supported = self.spec.reset ~= nil,
        state_tracking = self.spec.state_key and "game_epoch" or "public_state",
        seed_min = 0,
        seed_max = MAX_SEED,
        tools = TOOL_NAMES,
    }
end

function Controller:observe()
    local ok, state = pcall(self.spec.observe)
    if not ok then
        return failure("callback_failed", tostring(state))
    end

    if type(state) ~= "table"
        or not bounded_string(state.mode, 128)
        or type(state.terminal) ~= "boolean"
        or not finite(state.score)
        or type(state.observation) ~= "table"
        or (state.busy ~= nil and type(state.busy) ~= "boolean") then
        return failure(
            "invalid_observation",
            "observe must return mode, terminal, score, observation and optional busy"
        )
    end

    local ids, message = legal_actions(state.actions)
    if not ids then
        return failure("invalid_observation", message)
    end

    local public = {
        mode = state.mode,
        terminal = state.terminal,
        score = state.score,
        busy = state.busy == true,
        observation = state.observation,
        actions = state.actions,
    }
    -- Real-time games may version action eligibility independently of moving visuals.
    local key_ok, key = true, public
    if self.spec.state_key then
        key_ok, key = pcall(self.spec.state_key, public)
    end
    if not key_ok or key == nil then
        return failure("invalid_observation", "state_key must return a serializable decision epoch")
    end

    local encoded_ok, fingerprint = pcall(driver._json.encode, key)
    if not encoded_ok then
        return failure("invalid_observation", tostring(fingerprint))
    end

    if self._fingerprint and self._fingerprint ~= fingerprint then
        self.state_id = self.state_id + 1
    end
    self._fingerprint = fingerprint
    self._legal = ids

    public.protocol_version = 1
    public.game_id = self.spec.id
    public.session_id = self.session_id
    public.state_id = self.state_id
    return public
end

function Controller:act(args)
    if not self._attached then
        return failure("detached", "game driver is detached")
    end

    if not request(args, { session_id = true, state_id = true, action_id = true })
        or not bounded_string(args.session_id, 256)
        or not integer(args.state_id, 1, MAX_STATE)
        or not bounded_string(args.action_id, 128) then
        return failure("invalid_request", "act requires session_id, integer state_id and action_id")
    end

    local state = self:observe()
    if state.ok == false then
        return state
    end

    if args.session_id ~= self.session_id or args.state_id ~= self.state_id then
        return failure(
            "stale_state",
            "observe the current session and state before acting; never retry an action"
        )
    end
    if state.busy then
        return failure("busy", "the current action is still running; observe until busy is false")
    end
    if not self._legal[args.action_id] then
        return failure("illegal_action", "action_id is not legal in the observed state")
    end

    -- Consume before calling game code: an error may have already changed the game.
    self.state_id = self.state_id + 1
    self._fingerprint = nil
    local ok, message = pcall(self.spec.act, args.action_id)
    if not ok then
        return failure("callback_failed", tostring(message))
    end

    return self:observe()
end

function Controller:reset(args)
    if not self._attached then
        return failure("detached", "game driver is detached")
    end
    if not request(args, { seed = true }) or not integer(args.seed, 0, MAX_SEED) then
        return failure("invalid_request", "reset requires an integer seed in 0..2147483647")
    end
    if not self.spec.reset then
        return failure("unsupported", "this game does not support reset")
    end

    self.session_id = new_session(self.spec.id)
    self.state_id = 1
    self._fingerprint = nil
    local ok, message = pcall(self.spec.reset, args.seed)
    if not ok then
        return failure("callback_failed", tostring(message))
    end

    return self:observe()
end

local function register(self, name, description, schema, method)
    mcp.register_tool({
        name = name,
        description = description,
        schema = schema,
        handler = function(args)
            -- The native MCP table marshaller serializes empty tables as {}.
            -- Canonical JSON preserves actions=[] when the game has no moves;
            -- the registry returns this object text as the tool's JSON object.
            return driver._json.encode(method(self, args))
        end,
    })
end

function Controller:attach()
    assert(driver._json and type(driver._json.encode) == "function", "driver requires gamekit.json")
    if _G.__gamekit_active_driver then
        _G.__gamekit_active_driver:detach()
    end

    self.session_id = new_session(self.spec.id)
    self.state_id = 1
    self._fingerprint = nil
    self._attached = true
    _G.__gamekit_active_driver = self

    if not mcp or not mcp.register_tool then
        return self
    end

    local empty = { type = "object", additionalProperties = false }
    register(self, "game_describe", "Describe this game's named-action protocol.", empty, self.describe)
    register(
        self,
        "game_observe",
        "Observe public game state and legal actions. Parse the returned JSON text.",
        empty,
        self.observe
    )
    register(self, "game_act", "Apply one legal action against the observed decision state; never retry.", {
        type = "object",
        additionalProperties = false,
        required = { "session_id", "state_id", "action_id" },
        properties = {
            session_id = { type = "string", minLength = 1, maxLength = 256 },
            state_id = { type = "integer", minimum = 1, maximum = MAX_STATE },
            action_id = { type = "string", minLength = 1, maxLength = 128 },
        },
    }, self.act)
    register(self, "game_reset", "Start a new game session with an explicit seed.", {
        type = "object",
        additionalProperties = false,
        required = { "seed" },
        properties = {
            seed = { type = "integer", minimum = 0, maximum = MAX_SEED },
        },
    }, self.reset)
    return self
end

function Controller:detach()
    self._attached = false
    if _G.__gamekit_active_driver ~= self then
        return
    end

    if mcp and mcp.unregister_tool then
        for _, name in ipairs(TOOL_NAMES) do
            mcp.unregister_tool(name)
        end
    end
    _G.__gamekit_active_driver = nil
end

function driver.attach(spec)
    return driver.new(spec):attach()
end

return driver
