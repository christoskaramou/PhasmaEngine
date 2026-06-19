-- gamekit/audio.lua — thin wrappers over the engine audio bindings.
--
-- sfx(path) plays a one-shot; music(path) plays a (looping) music stream. Volume
-- helpers pass straight through. All calls are nil-guarded so a build without
-- PE_AUDIO simply no-ops.

local kaudio = {}

function kaudio.sfx(path)
    if audio and audio.play then return audio.play(path) end
end

function kaudio.sfx_at(path, x, y, z)
    if audio and audio.play_3d then return audio.play_3d(path, x or 0.0, y or 0.0, z or 0.0) end
    if audio and audio.play then return audio.play(path) end
end

function kaudio.music(path)
    if audio and audio.play_music then return audio.play_music(path) end
end

function kaudio.stop_music()
    if audio and audio.stop_music then return audio.stop_music() end
end

function kaudio.master_volume(v)
    if audio and audio.set_master_volume then audio.set_master_volume(v or 1.0) end
end

return kaudio
