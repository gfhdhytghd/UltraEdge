-- Standalone example: substitute your dispatchers and avoid overlapping rules.
local edge = hl.plugin and hl.plugin.ultraedge
if not edge then return end

-- Native Hyprland Lua dispatchers are callable closures.
local function when_keys(keys, dsp)
  return function()
    for _, key in ipairs(keys) do
      if not hl.is_key_down(key) then return false end
    end
    return dsp()
  end
end

-- Arrive at left edge, then swipe right with three fingers while holding Super.
assert(edge.rule({
  target = "left", mode = "swipe", fingers = 3, direction = "right",
  swipe_distance = 40, consume = true,
  on_enter = when_keys({"Super_L"}, hl.dsp.focus({workspace = "+1"})),
}))

-- Two-finger scrolling is accepted only from a real touchpad in 2fg mode.
assert(edge.rule({
  target = "bottom", mode = "swipe", fingers = 2, direction = "up",
  swipe_distance = 40,
  on_enter = hl.dsp.exec_cmd("kitty"),
}))

local stage = hl.plugin and hl.plugin.hymission
if stage and stage.stage_open and stage.stage_close and stage.stage_region then
  assert(edge.rule({
    target = "left", mode = "swipe", fingers = 4, direction = "right",
    swipe_distance = 60,
    on_enter = when_keys({"Super_L", "Shift_L"}, function() stage.stage_open() end),
    -- Exit must still close after the user releases the activation keys.
    on_exit = function() stage.stage_close() end,
    hold = function(name) return stage.stage_region(name) end,
  }))
end

-- The same key wrapper works for enter, dwell and pause + second-push modes:
-- assert(edge.rule({target="top-left",mode="enter",
--   on_enter=when_keys({"a"},hl.dsp.exec_cmd("kitty"))}))
-- assert(edge.rule({target="top",mode="dwell",dwell_ms=300,
--   on_enter=when_keys({"Super_L"},hl.dsp.exec_cmd("kitty"))}))
-- assert(edge.rule({target="left",mode="push",pause_ms=180,push=8,
--   on_enter=when_keys({"Super_L","Shift_L"},hl.dsp.exec_cmd("kitty"))}))
