-- gamekit/ui.lua — drive authored runtime_ui HUD nodes.
--
-- HUD nodes are authored in the .pescene (text, panels) and only their CONTENT is
-- updated at runtime via node:set_ui{...}. set_ui cannot resize a node, so a
-- progress "bar" is rendered as a block-character gauge inside the node's text.
-- Never build per-frame quads for authored nodes — set_ui mutates the retained tag.

local ui = {}

-- UTF-8 bytes for the gauge glyphs, spelled out so the source is safe on any Lua
-- version (avoids the 5.3+ "\u{}" escape). U+2588 FULL BLOCK / U+2591 LIGHT SHADE.
local BLOCK_FULL = string.char(0xE2, 0x96, 0x88)
local BLOCK_EMPTY = string.char(0xE2, 0x96, 0x91)

-- Set arbitrary set_ui fields (body/title/label/fill/text_color/align_h/...).
function ui.set(node, fields)
    if node and node.set_ui then node:set_ui(fields) end
end

-- Replace a Text node's body.
function ui.text(node, str)
    if node and node.set_ui then node:set_ui({ body = tostring(str) }) end
end

-- Render a 0..1 gauge as filled/empty blocks in the node body.
-- opts: width (cells, default 12), label (prefix), show_percent (bool),
-- tint (bool; lerps text colour green->red as it empties).
function ui.bar(node, frac, opts)
    if not node or not node.set_ui then return end
    opts = opts or {}
    frac = math.max(0.0, math.min(1.0, frac or 0.0))
    local width = opts.width or 12
    local filled = math.floor(frac * width + 0.5)
    local body = string.rep(BLOCK_FULL, filled) .. string.rep(BLOCK_EMPTY, width - filled)
    if opts.label then body = opts.label .. " " .. body end
    if opts.show_percent then body = body .. string.format(" %d%%", math.floor(frac * 100 + 0.5)) end
    local fields = { body = body }
    if opts.tint then
        fields.text_color = { r = 1.0 - frac, g = frac, b = 0.1, a = 1.0 }
    end
    node:set_ui(fields)
end

-- Show/hide an authored UI node.
function ui.show(node, visible)
    if node and node.set_ui then node:set_ui({ visible = visible ~= false }) end
end

return ui
