project('UltraEdge', 'cpp', version: '0.1.0', default_options: ['cpp_std=c++23'])
shared_library('UltraEdge', 'src/main.cpp', dependencies: [dependency('hyprland'), dependency('lua'), dependency('libeis-1.0'), meson.get_compiler('cpp').find_library('dl', required: false)], cpp_args: ['-DWLR_USE_UNSTABLE', '-fno-gnu-unique'], install: true, override_options: ['b_lundef=false'])
test('edge-state-machine', executable('edge-tests', 'tests/engine.cpp', include_directories: include_directories('src')))

lua = find_program('lua', required: false)
if lua.found()
  test('offline-gesture-config', lua, args: [files('tests/gesture-config.lua'), meson.current_source_dir() / 'examples/gestures.lua'])
  test('offline-lua-config', lua, args: [files('tests/local-config.lua'), meson.current_source_dir() / 'examples/hyprv.lua'])
endif

python = find_program('python3')
test('plugin-lua-input-regression', python, args: [files('tests/plugin_regression.py'), '--build-dir', meson.current_build_dir() / 'plugin-regression', '--cxx', ' '.join(meson.get_compiler('cpp').cmd_array())])
