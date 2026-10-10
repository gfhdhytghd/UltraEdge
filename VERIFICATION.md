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
- Local edge-trigger.lua and hyprland-plugins.lua: skill syntax checker passed.
- Hymission build and CTest 5/5 passed, including added dynamic hold-strip and
  idempotent action alias regression cases. Commit:
  `7a9abd2e5fe804fa8da0dbaf1ccf2f04770d5c8a`, directly after preserved `e16d35c`.

Persistent local edits:

- `/home/wilf/.config/HyprV/hypr/edge-trigger.lua` (new guarded module).
- `/home/wilf/.config/HyprV/hypr/hyprland-plugins.lua` (require appended;
  pre-existing changes preserved).
- `/home/wilf/.config/HyprV/hypr/scripts/sidechat.sh` (idempotent show/hide,
  explicit target monitor, retain toggle, fix set-e-sensitive retry increments).

No install, plugin load/unload, hyprpm update, explicit config reload, mouse
injection, remote creation, push or release was performed. The persistent edits
are not an assertion of live application; optional APIs are guarded while the
new plugin is absent. Live runtime/behavior acceptance remains PENDING; follow
the manual steps in README after a user-controlled safe install/update.
