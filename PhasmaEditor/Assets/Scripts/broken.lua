-- Deliberately broken snippets live here for manual Lua error-path testing.
-- Keep them commented so regular editor/player scene smokes stay quiet.

-- hooks {
--     update = function()
--         this_does_not_exist()
--     end
-- }

-- comment all lines bellow
-- local speed = 1 -- radians per second
-- local radius = 1

-- local start_pos = transform:get_position()

-- hooks {
--     update = function()
--         local dt = engine.get_metrics().delta_ms / 1000.0
--         angle = angle + speed * dt
--
--         local x = start_pos.x + math.cos(angle) * radius
--         local z = start_pos.z + math.sin(angle) * radius
--
--         transform:set_position(vec3(x, start_pos.y, z))
--     end
-- }	
