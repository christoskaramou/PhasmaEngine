-- gamekit/deck.lua — draw/discard/reshuffle over a list of card specs.
--
-- new(specs) seeds the draw pile from a list of arbitrary card tables. draw(n)
-- moves cards into the hand (auto-reshuffling the discard pile when the draw pile
-- runs dry); discard(card) moves a hand card to the discard pile. Pure data — no
-- scene coupling. (Ships for the card fast-follow; not in the topdown smoke.)

local Deck = {}
Deck.__index = Deck

function Deck.new(specs)
    local self = setmetatable({ draw_pile = {}, hand = {}, discard_pile = {} }, Deck)
    for _, s in ipairs(specs or {}) do
        self.draw_pile[#self.draw_pile + 1] = s
    end
    return self
end

function Deck:shuffle()
    local p = self.draw_pile
    for i = #p, 2, -1 do
        local j = math.random(1, i)
        p[i], p[j] = p[j], p[i]
    end
    return self
end

function Deck:reshuffle()
    for i = #self.discard_pile, 1, -1 do
        self.draw_pile[#self.draw_pile + 1] = table.remove(self.discard_pile, i)
    end
    return self:shuffle()
end

function Deck:draw(n)
    n = n or 1
    local drawn = {}
    for _ = 1, n do
        if #self.draw_pile == 0 then self:reshuffle() end
        if #self.draw_pile == 0 then break end -- nothing left anywhere
        local card = table.remove(self.draw_pile)
        self.hand[#self.hand + 1] = card
        drawn[#drawn + 1] = card
    end
    return drawn
end

function Deck:discard(card)
    for i = #self.hand, 1, -1 do
        if self.hand[i] == card then
            table.remove(self.hand, i)
            break
        end
    end
    self.discard_pile[#self.discard_pile + 1] = card
    return self
end

function Deck:counts()
    return #self.draw_pile, #self.hand, #self.discard_pile
end

return Deck
