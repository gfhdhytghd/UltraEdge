# Offline verification — 2026-10-10

- Installed Hyprland and development pkg-config: 0.56.2.
- Real binary exports `CInputManager::onMouseMoved(IPointer::SMotionEvent)`;
  local Hyprland source confirms the event carries accelerated/unaccelerated
  relative motion before pointer clipping.
- CMake Release build: passed; CTest 2/2 passed.
- Meson build: passed; tests 2/2 passed. Plugin target permits host-resolved
  symbols (`b_lundef=false`), as required by Hyprland plugin linking.
- Engine: 141 checks, including all modes, all edge/corner directions, pause,
  noise, tangent/reverse movement, latch/exit/cooldown, asynchronous geometry,
  negative/offset/scaled logical outputs and shared-boundary crossing.
- Offline Lua fixture: API absence guards, real-geometry-shaped hold provider,
  rotation/scale, hidden workspace and command quoting passed.
- Local sidechat.sh: bash syntax and 6 isolated fake-hyprctl checks passed;
  no real app launch, dispatch or compositor IPC occurred in those tests.
- Local ultraedge.lua and hyprland-plugins.lua: skill syntax checker passed.
- Hymission build and CTest 5/5 passed, including added dynamic hold-strip and
  idempotent action alias regression cases. Commit:
  `7a9abd2e5fe804fa8da0dbaf1ccf2f04770d5c8a`, directly after preserved `e16d35c`.

Persistent local edits:

- `/home/wilf/.config/HyprV/hypr/ultraedge.lua` (new guarded module).
- `/home/wilf/.config/HyprV/hypr/hyprland-plugins.lua` (require appended;
  pre-existing changes preserved).
- `/home/wilf/.config/HyprV/hypr/scripts/sidechat.sh` (idempotent show/hide,
  explicit target monitor, retain toggle, fix set-e-sensitive retry increments).

No install, plugin load/unload, hyprpm update, explicit config reload, mouse
injection, remote creation, push or release was performed. The persistent edits
are not an assertion of live application; optional APIs are guarded while the
new plugin is absent. Live runtime/behavior acceptance remains PENDING; follow
the manual steps in README after a user-controlled safe install/update.

## UltraEdge 0.2.0 extension verification

- Repository moved to `/home/wilf/data/hyprland_plugins/UltraEdge`.
- CMake Release (`build-v2`) and Meson (`build-v2-meson`) plugin builds passed.
- Both test suites: 3/3 passed; engine now has 292 checks.
- Added swipe checks for 2/3/4 fingers, cardinal/diagonal directions, wrong
  count/direction, cancellation, reversal, latch/exit, insufficient distance,
  shared boundary rejection and stale gesture generations after reset/crossing.
- Gesture Lua example ran against offline fixtures; missing/pressed key combos,
  actual callable dispatcher shape and cleanup after key release passed.
- Main Lua examples and local renamed guarded configuration passed the skill
  syntax checker. Existing six isolated sidechat checks still passed.
- Real Hyprland binary exports the onMouseWheel axis/device signature. Local
  headers expose the originating device's real libinput scroll method; the
  two-finger path requires a real touchpad, FINGER axis source and actual 2FG
  method. Native swipe input uses the reported finger count.
- No new Hymission modifications were needed. Its earlier 7a9abd2 commit stays
  unchanged. No runtime acceptance is claimed for the new gesture path.
- Local module renamed from `hypr/edge-trigger.lua` to `hypr/ultraedge.lua`; the
  existing plugins config require now points at it. Left/AI-HUB actions remain
  their prior configured behaviors. Gesture rules are provided as examples,
  rather than assigning unrequested actions to this machine's finger gestures.
- Generated old-path build directories were removed after the repository rename
  so future default CMake/Meson configuration does not reuse stale absolute paths.
