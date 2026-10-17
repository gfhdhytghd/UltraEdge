-- Offline-configured. Requires safely installed UltraEdge and updated Hymission.
local edge = hl.plugin and hl.plugin.ultraedge
if not edge or type(edge.rule) ~= "function" then return end
edge.clear()

local stage = hl.plugin and hl.plugin.hymission
if stage and type(stage.stage_open) == "function" and
    type(stage.stage_close) == "function" and type(stage.stage_region) == "function" then
  assert(edge.rule({
    monitor = "*", target = "left", mode = "move",
    move_ms = 100, movement_gap_ms = 80, noise = 0.5, band = 2, cooldown_ms = 400,
    on_enter = function() stage.stage_open() end,
    on_exit = function() stage.stage_close() end,
    hold = function(monitor) return stage.stage_region(monitor) end,
  }))
end

-- The right strip follows actual AI-HUB window geometry, including the gap
-- between its right edge and the output boundary. No fixed 360/370 px snapshot.
local function shell_quote(s) return "'" .. s:gsub("'", "'\\''") .. "'" end
local function ai_region(name)
  for _, win in ipairs(hl.get_windows()) do
    local mon, ws = win.monitor, win.workspace
    if win.class == "ai-hub-firefox" and win.mapped and not win.hidden and
        ws and ws.name ~= "special:sidechat_hidden" and mon and mon.name == name then
      local at = win.at
      -- monitor.width/height are physical dimensions in Hyprland 0.56.2;
      -- logical dimensions follow scale + transform.
      local w, h = mon.width / mon.scale, mon.height / mon.scale
      if mon.transform % 2 == 1 then w, h = h, w end
      local left = math.max(mon.x, at.x)
      return { x = left, y = mon.y, width = mon.x + w - left, height = h }
    end
  end
  return nil
end
assert(edge.rule({
  monitor = "*", target = "right", mode = "move",
  move_ms = 100, movement_gap_ms = 80, noise = 0.5, band = 2, cooldown_ms = 400,
  hold_wait_ms = 5000, exit_distance = 80,
  on_enter = function(monitor) hl.exec_cmd("bash /home/wilf/.config/HyprV/hypr/scripts/sidechat.sh show " .. shell_quote(monitor) .. " --no-cursor-warp") end,
  on_exit = function() hl.exec_cmd("bash /home/wilf/.config/HyprV/hypr/scripts/sidechat.sh hide '' --no-cursor-warp") end,
  hold = ai_region,
}))
