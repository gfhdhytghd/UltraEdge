# UltraEdge

Independent Hyprland plugin for exposed edges and corners, targeting Hyprland v0.57.0
([gfhdhytghd/Hyprland](https://github.com/gfhdhytghd/Hyprland/releases/tag/v0.57.0) `f6725e43`). Supports basic
hot edges/corners and a deliberate **arrive → stop → push again** gesture, plus edge-qualified
two/three/four-finger directional touchpad gestures and configurable continuous
outward pointer movement (`move_ms`, default 100ms), from mouse or touchpad. No
Hymission dependency; actions and optional hold geometry are supplied by config.

The repository is now `/home/wilf/data/hyprland_plugins/UltraEdge`; the plugin
name and library are `UltraEdge` / `libUltraEdge.so`, Lua namespace is
`hl.plugin.ultraedge`, and read-only status is `hyprctl ultraedge-state`. Replace
the old edge_trigger namespace when migrating. Local guarded config is now
`hypr/ultraedge.lua`; no install/reload was performed as part of the rename.

## Build

Requires matching Hyprland development headers, C++23, pkg-config, Lua,
libeis-1.0 and nlohmann/json headers. Exact compositor/header ABI is checked at
plugin load.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
# Alternative:
meson setup build-meson
meson compile -C build-meson
meson test -C build-meson --print-errorlogs
# Engine-only tests without Hyprland:
cmake -S . -B build-engine -DBUILD_PLUGIN=OFF
cmake --build build-engine
ctest --test-dir build-engine --output-on-failure
```

`hyprpm.toml` builds the plugin and tests; no install command or helper process is
included. The output is `build/libUltraEdge.so`. Use separate build directories
and the matching `PKG_CONFIG_PATH` for each compositor/header target. Both input
hook signatures are checked at compile time and each lookup must match exactly
one native function at load. This repository has no
remote requirement. Add/install/update only from a safe user-controlled session;
plugin updates and loads change the running compositor. The authoring workflow
here does not install, load/unload, update, inject input, push or publish.

## Rules

Use `hl.plugin.ultraedge.rule(table)` in Lua. Returns true, or nil plus a
validation error. `clear()` removes all rules and `pause(boolean)` clears all
state without executing actions. Reload clears Lua registry references before
config evaluation, preventing duplicate rules and stale callbacks. Rule actions
receive the originating monitor name. Exceptions/false return from an action
clear that rule and apply cooldown; returning nothing is success. Rules execute
in registration order; overlapping rules can each fire, so avoid overlaps when
mutually exclusive actions are desired.

```lua
local edge = hl.plugin and hl.plugin.ultraedge
local stage = hl.plugin and hl.plugin.hymission
if edge and stage and type(stage.stage_open) == "function" and
    type(stage.stage_close) == "function" and type(stage.stage_region) == "function" then
  edge.clear()
  assert(edge.rule({
    target = "left", mode = "push", monitor = "*",
    on_enter = function() stage.stage_open() end,
    on_exit = function() stage.stage_close() end,
    hold = function(name) return stage.stage_region(name) end,
  }))
end
```

Lua uses actual function callbacks, including native actions via `hl.dsp` /
`hl.exec_cmd` and plugin-owned Lua APIs. It does **not** pretend a plugin string
dispatcher is registered: Hyprland 0.57 has removed the legacy parser and
`addDispatcherV2` always returns false. Legacy JSON dispatcher rules and
`ultraedge:pause` strings are unavailable on this branch; use Lua callbacks and
`hl.plugin.ultraedge.pause(boolean)`. The old
[legacy.conf](examples/legacy.conf) is retained as a historical 0.56.2 example.
The unused `plugin:ultraedge:rules` config key and `hold_command` route are
removed; use `rule(table)` and a Lua `hold(name)` provider instead. See
[basic.lua](examples/basic.lua) for enter and
dwell examples, and [hyprv.lua](examples/hyprv.lua) for the local Stage/AI-HUB setup.

| Field | Default | Meaning |
| --- | --- | --- |
| `target` | `left` | `left`, `right`, `top`, `bottom`, `top-left`, `top-right`, `bottom-left`, `bottom-right` |
| `monitor` | `*` | Exact output name or all outputs; selection happens per rule |
| `mode` | `push` | `enter` immediately, `dwell` after dwell, `push` after stop + second movement; `move` after continuous outward movement; `swipe` after an edge-qualified touchpad gesture |
| `band` | 2 | Edge band, logical pixels; corners must be within both bands |
| `pause_ms` | 180 | Quiet period before second push can start |
| `push_angle` | 90 | Full movement cone in degrees, centered on the outward edge normal (corner diagonal); range (0, 180], used by `move` and `push`. Outside motion interrupts progress. |
| `push` | 8 | Outward motion accumulated per required axis |
| `noise` | 0.5 | Ignore relative events at or below this vector magnitude |
| `burst_ms` | 160 | Maximum gap between second-push events; longer gaps discard accumulation |
| `move_ms` | 100 | Move mode: required duration of outward movement after arrival, milliseconds |
| `movement_gap_ms` | 80 | Move mode: maximum interval between significant outward events; a longer gap restarts timing |
| `dwell_ms` | 300 | Continuous residence for dwell mode |
| `exit_distance` | 80 | Fixed holding width if no dynamic provider, inclusive boundary |
| `cooldown_ms` | 400 | Suppression after exit or failed action |
| `hold_wait_ms` | 0 | Optional first-geometry grace for asynchronous launch; fallback to exit_distance during grace |
| `fingers` | 3 | Swipe mode: 2, 3 or 4 |
| `direction` | `right` | Swipe: left/right/up/down and up-left/up-right/down-left/down-right |
| `swipe_distance` | 40 | Accumulated gesture distance per required axis, in reported gesture/scroll units |
| `consume` | true | Capture matching gesture stream before native gesture/client handling; false observes alongside it |
| `on_enter`, `on_exit` | required / optional | Lua functions |
| `hold` | absent | Lua callback `(monitor_name) → {x,y,width,height}` or nil |

A 16ms compositor event-loop timer drives dwell and hold checks even while
the desktop has no animations; second-push motion is processed immediately.

All geometry and accelerated pointer movement use compositor logical coordinates;
output rectangles are already scaled and transformed. Push thresholds use the
same relative motion consumed by the compositor (`delta` or `unaccel` according
to `input:force_no_accel`, including touchpad axis flips), never the difference
between clamped cursor positions. Tablet absolute motion and pointer warps can
enter/dwell or exit, but cannot supply a second push. For an edge, outward
movement must lie within `push_angle` (default 90°, ±45° from the outward
normal); for a corner, the cone is centered on the diagonal and both axes must move
outward beyond noise and accumulate `push` each. Arriving motion is discarded.
Continuous movement, reversal and sideways movement require a new stop. Noise
is per event and does not contribute to the push. No fabricated motion is used.

Actions latch once. Dynamic hold callbacks are evaluated while active and must
return global logical rectangles; nil causes one exit. An asynchronous provider
can use `hold_wait_ms`; after its first valid rectangle, later nil exits
immediately, even during the grace window. Lua providers run synchronously in
the compositor: keep them fast, read-only and nonblocking.
Session lock, pause, config reload, output layout changes/removal and plugin exit
clear state without executing actions. Pause is reset by config reload.

## Edge touchpad gestures and key combinations

```lua
local edge = hl.plugin and hl.plugin.ultraedge
if edge then
  assert(edge.rule({
    target = "left", mode = "swipe", fingers = 3, direction = "right",
    swipe_distance = 40,
    on_enter = hl.dsp.focus({workspace = "+1"}),
  }))
end
```

Arrive at the exposed edge/corner first, then begin the gesture while still
inside `band`. Recognition requires the configured finger count and direction;
wrong fingers, insufficient distance, reversal, native cancellation and a
mostly perpendicular movement do not dispatch. The pointer must remain in the
edge band through completion. Leaving/re-entering, crossing outputs, pause,
reload or output layout changes invalidate the original gesture generation.
Activation happens once and uses the same hold/exit/cooldown mechanism as the
other modes. Leave the holding region to rearm. See
[gestures.lua](examples/gestures.lua) for 2/3/4-finger and dispatcher examples.

Three/four-finger input uses Hyprland's native swipe begin/update/end events;
action executes only at end, so a cancelled stream never dispatches. A device
that reports a native two-finger swipe is supported as well. Most libinput
trackpads expose two fingers as scrolling instead: UltraEdge accepts finger-axis
events only when the real originating device is a touchpad and its current
libinput scroll method is `LIBINPUT_CONFIG_SCROLL_2FG`. Mouse wheels, button
scroll, edge scroll, virtual devices and unverified devices cannot masquerade
as two fingers. Two-finger completion is an axis stop or 140ms with no scroll
updates; scroll events do not expose a native gesture cancellation flag.
`swipe_distance` for that path uses raw scroll units and direction follows the
reported deltas, including the device's natural-scroll setting. It is not a
physical-finger-count or millimetre measurement. Tune distance/direction on the
actual device during manual acceptance.

`consume=true` captures a stream when a qualifying rule begins at an exposed
edge, including streams later rejected by direction or distance. Elsewhere,
input passes through. `consume=false` observes and can coexist with native
bindings/client scrolling; both may act. Shared boundaries never qualify.
Other plugins that already cancel a native begin retain priority. Existing
in-progress captured streams are swallowed to completion after reset, without
retaining callbacks. This avoids forwarding an orphan end/update to clients.

Native InputCapture or Viewflow remote capture suspends local edge processing,
including timer-driven dwell/hold and swipe completion. Capture silently resets
pending and active state; mouse/axis events still reach the original compositor
handlers. The Viewflow query is resolved once per outer input/timer invocation,
with no retained plugin handle or export across invocations.

Hyprland supports `hl.is_key_down(keysym_or_keycode)`. No extra
key-tracking implementation is needed. Guard **the action callback** for any
mode, including ordinary non-modifier keys:

```lua
local function when_keys(keys, dsp)
  return function()
    for _, key in ipairs(keys) do
      if not hl.is_key_down(key) then return false end
    end
    return dsp()
  end
end
-- Use this on_enter with enter, dwell, push or swipe:
-- on_enter = when_keys({"Super_L", "Shift_L"}, hl.dsp.exec_cmd("kitty"))
-- on_enter = when_keys({"a"}, hl.dsp.exec_cmd("kitty"))
```

Keys are checked at dispatch time, so the user can press them after arriving
at the edge. `false` rejects activation, clears the rule and applies its
cooldown; no exit action is owed for a rejected activation. The key guard does
not affect whether a swipe stream was captured at begin; use `consume=false`
if pass-through is desired when a callback rejects. Avoid guarding `on_exit`
with activation keys: releasing those keys should not prevent cleanup. For
either Super key use `hl.is_key_down("Super_L") or
hl.is_key_down("Super_R")` in the guard. Use callable `hl.dsp` closures for native
actions on this branch.

## Multiple outputs

Shared boundaries are not hot edges in **any mode**. The engine uses global
logical rectangles, including negative origins, differing logical output sizes,
rotations, offset and partial shared segments. Exposed parts of a partially
shared side still work. Corners must have both sides exposed; shared endpoints
within the configured corner band are conservatively masked. Mirrored or
overlapping regions are masked where another output covers the outward point.

Moving to a different output (or leaving all outputs) discards the candidate,
stop, push and latch without enter/exit dispatch. This guarantees a cross-output
movement cannot close/reopen an application or inherit a gesture. Applications
can remain open after that transition; a later independent trigger uses
idempotent actions. Output geometry changes also reset silently. This is an
intentional behavior boundary, including pointer warps between outputs.

## Stage and this machine

Hymission supplies Lua `stage_open`, `stage_close` and `stage_region` APIs.
Use `stage_region(name)` as the hold provider on Hyprland 0.57; legacy
`hold_command` region lookup is unavailable here. Open/close are idempotent,
global Stage actions, not toggles. The rectangle follows actual target reservation width during
animation, including the Stage desktop gap and output reservation gaps. A
manual close returns no hold region, so the paired close remains harmless.

Local persistent configuration is in
`/home/wilf/.config/HyprV/hypr/ultraedge.lua`, required by
`hyprland-plugins.lua`, with guards for missing plugin APIs. Both edges use `mode = "move"` with
`move_ms = 100` and `movement_gap_ms = 80`. Left outward movement opens
Stage and closes it when leaving its dynamic strip. Right outward movement runs
`sidechat.sh show MONITOR --no-cursor-warp`; exit runs
`sidechat.sh hide '' --no-cursor-warp`. The empty monitor argument on hide keeps
the flag in the script's third argument position. These edge calls skip explicit
window focus, including focus restoration on hide, to avoid moving the pointer
away from the edge. Keyboard and three-finger callers omit the flag and retain
their existing focus behavior. This example requires a local `sidechat.sh` that
supports `--no-cursor-warp`; the script is not bundled with UltraEdge.
The script retains its
no-argument toggle for existing binds and adds idempotent show/hide based on
actual workspace visibility. Explicit show can move the existing window to the
triggering output. Its launch remains asynchronous.

The right hold strip uses the actual `ai-hub-firefox` window's left coordinate
and its output's logical right boundary, with full output height so top/bottom
margins and the small gap beside the window remain held. It follows resized or
moved window geometry, not the current 360px rule. Hidden special-workspace
windows do not supply geometry. There is a 5s first-geometry wait, bounded by the
80px fallback strip while waiting; leaving before launch completes queues a
hide behind the script's lock. The right side thus explicitly hides on exit.

## Manual acceptance (pending)

1. From a safe context install the independent plugin and update Hymission from
   committed sources, then activate the persistent configuration. Do not mix a
   manually loaded Hymission with a hyprpm-managed instance.
2. Read `hyprctl ultraedge-state` and verify rule count/error. In Lua inspect
   API presence only: `hyprctl repl 'return type(hl.plugin.ultraedge.rule)'`.
   Read Stage region for the actual output and compare it with Stage placement.
3. At an exposed left side: first arrival and continuous push must do nothing;
   stop at least 180ms, then push at least 8 logical units. Stage opens once,
   stays while inside its dynamic region, and closes once when leaving. Verify
   sideways/reverse motion, jitter, manual Stage open/close and cooldown.
4. Repeat at an exposed right side for AI-HUB. Check existing visible/hidden
   windows, asynchronous launch, resizing, movement and trigger on another
   output. Existing shortcut toggle must still work; leaving the hold strip
   hides rather than toggles.
5. Test every edge/corner mode on offset, scaled, negative-coordinate outputs.
   Cross shared boundaries repeatedly, including near endpoints: no enter or
   exit action and no inherited stop/push. Test monitor removal, config reload,
   pause/resume and session lock. Observe only with real user mouse input.

6. For every configured 2/3/4-finger direction, start at the edge and finish
   beyond threshold. Test wrong count, reversal, cancellation, insufficient
   distance, leave-and-return and shared boundaries. Test keys pressed before
   and after arrival, missing keys, and release before exit. Compare capture
   and pass-through intentionally; ensure normal scrolling away from the edge.

Passing builds and offline regression tests proves source compatibility and
state-machine behavior only. The current compositor has not been used to load
or exercise this new plugin; live acceptance is pending.

### Mouse movement duration

```lua
assert(hl.plugin.ultraedge.rule({
  target = "left", mode = "move", move_ms = 100, movement_gap_ms = 80,
  on_enter = function() hl.plugin.hymission.stage_open() end,
  on_exit = function() hl.plugin.hymission.stage_close() end,
  hold = function(name) return hl.plugin.hymission.stage_region(name) end,
}))
```

触边后继续向外移动 `move_ms` 毫秒才触发，不需要先停顿。光标被边缘
夹住时仍使用真实相对移动事件计时；到达边缘的那次移动不计入时长。
停止超过 `movement_gap_ms`、反向、侧向移动或离开边缘会重新计时。
定时器不会替代移动事件触发。小于 `noise` 的抖动不延长移动时间。
此模式也适用于触控板光标移动；双／三／四指方向手势仍使用 `swipe`。
