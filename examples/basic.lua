local edge = hl.plugin and hl.plugin.edge_trigger
if not edge then return end
edge.clear()
-- Basic hot corner: enter immediately, exit past 40 logical pixels.
assert(edge.rule({target="top-left", mode="enter", exit_distance=40,
  on_enter=function() hl.exec_cmd("notify-send 'Hot corner'") end}))
-- Basic hot edge: dwell 300ms before opening a native action via its Lua API.
assert(edge.rule({target="bottom", mode="dwell", dwell_ms=300,
  on_enter=function() hl.exec_cmd("notify-send 'Bottom edge dwell'") end}))
-- Pause/reset without executing enter/exit callbacks:
-- edge.pause(true); edge.pause(false)
