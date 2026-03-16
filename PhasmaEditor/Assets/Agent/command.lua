local m, err = load_model("glTF-Sample-Models/Sponza/glTF/Sponza.gltf")
if m then
    m:set_label("Sponza")
    focus_camera_on(m, 20.0)
    pe_log("Sponza loaded successfully")
else
    pe_log("Error: " .. tostring(err))
end
