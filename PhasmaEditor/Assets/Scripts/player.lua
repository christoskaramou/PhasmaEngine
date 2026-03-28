local props = exposed {
    health = 100,
    speed = 5.0,
    tag = "Player1"
}

hooks {
    init = function()
        pe_log("[Player] init: health=" .. props.health .. " speed=" .. props.speed .. " tag=" .. props.tag)
    end,
    update_editor = function()
        -- Silent normally, we'll check from outside
    end
}
