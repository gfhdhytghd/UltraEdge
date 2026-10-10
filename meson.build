project('hypr-edge-trigger', 'cpp', version: '0.1.0', default_options: ['cpp_std=c++23'])
shared_library('hypr-edge-trigger', 'src/main.cpp', dependencies: [dependency('hyprland'), dependency('lua')], cpp_args: ['-DWLR_USE_UNSTABLE', '-fno-gnu-unique'], install: true, override_options: ['b_lundef=false'])
test('edge-state-machine', executable('edge-tests', 'tests/engine.cpp', include_directories: include_directories('src')))

lua = find_program('lua', required: false)
if lua.found()
  test('offline-lua-config', lua, args: [files('tests/local-config.lua'), meson.current_source_dir() / 'examples/hyprv.lua'])
endif
