-- Helpers API test suite (called from main.lua)

function run_helpers_tests()
    pe_log("=== Helpers API Tests ===")
    T.reset()

    -- IsDepthAndStencil
    T.check("is_depth_and_stencil (d24_s8)", is_depth_and_stencil("d24_s8") == true)
    T.check("is_depth_and_stencil (d32f_s8)", is_depth_and_stencil("d32f_s8") == true)
    T.check("is_depth_and_stencil (d32f)", is_depth_and_stencil("d32f") == false)
    T.check("is_depth_and_stencil (rgba8)", is_depth_and_stencil("rgba8") == false)

    -- IsDepthOnly
    T.check("is_depth_only (d32f)", is_depth_only("d32f") == true)
    T.check("is_depth_only (d24_s8)", is_depth_only("d24_s8") == false)
    T.check("is_depth_only (rgba8)", is_depth_only("rgba8") == false)

    -- IsStencilOnly
    T.check("is_stencil_only (s8)", is_stencil_only("s8") == true)
    T.check("is_stencil_only (d24_s8)", is_stencil_only("d24_s8") == false)
    T.check("is_stencil_only (rgba8)", is_stencil_only("rgba8") == false)

    -- HasDepth
    T.check("has_depth (d32f)", has_depth("d32f") == true)
    T.check("has_depth (d24_s8)", has_depth("d24_s8") == true)
    T.check("has_depth (s8)", has_depth("s8") == false)
    T.check("has_depth (rgba8)", has_depth("rgba8") == false)

    -- HasStencil
    T.check("has_stencil (d24_s8)", has_stencil("d24_s8") == true)
    T.check("has_stencil (d32f_s8)", has_stencil("d32f_s8") == true)
    T.check("has_stencil (s8)", has_stencil("s8") == true)
    T.check("has_stencil (d32f)", has_stencil("d32f") == false)
    T.check("has_stencil (rgba8)", has_stencil("rgba8") == false)

    -- HasDepthOrStencil
    T.check("has_depth_or_stencil (d32f)", has_depth_or_stencil("d32f") == true)
    T.check("has_depth_or_stencil (s8)", has_depth_or_stencil("s8") == true)
    T.check("has_depth_or_stencil (d24_s8)", has_depth_or_stencil("d24_s8") == true)
    T.check("has_depth_or_stencil (rgba8)", has_depth_or_stencil("rgba8") == false)

    -- GetAspectMask
    T.check("get_aspect_mask (rgba8)", get_aspect_mask("rgba8") == "color")
    T.check("get_aspect_mask (d32f)", get_aspect_mask("d32f") == "depth")
    T.check("get_aspect_mask (s8)", get_aspect_mask("s8") == "stencil")
    T.check("get_aspect_mask (d24_s8)", get_aspect_mask("d24_s8") == "depth|stencil")
    T.check("get_aspect_mask (d32f_s8)", get_aspect_mask("d32f_s8") == "depth|stencil")

    -- IsReadOnlyAccess
    T.check("is_read_only_access (shader_read)", is_read_only_access("shader_read") == true)
    T.check("is_read_only_access (depth_read)", is_read_only_access("depth_read") == true)
    T.check("is_read_only_access (transfer_read)", is_read_only_access("transfer_read") == true)
    T.check("is_read_only_access (memory_read)", is_read_only_access("memory_read") == true)
    T.check("is_read_only_access (shader_write)", is_read_only_access("shader_write") == false)
    T.check("is_read_only_access (color_write)", is_read_only_access("color_write") == false)
    T.check("is_read_only_access (transfer_write)", is_read_only_access("transfer_write") == false)
    T.check("is_read_only_access (shader_read|transfer_read)", is_read_only_access("shader_read|transfer_read") == true)
    T.check("is_read_only_access (shader_read|shader_write)", is_read_only_access("shader_read|shader_write") == false)
    T.check("is_read_only_access (none)", is_read_only_access("none") == false)

    T.summary("Helpers API Tests")
end
