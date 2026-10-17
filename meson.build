project('UltraEdge', 'cpp', version: '0.1.0', default_options: ['cpp_std=c++23'])
hypr = dependency('hyprland', method: 'pkg-config')
# Match CMake's HYPR_HEADER_ROOT: never fall back to system Hyprland headers.
fs = import('fs')
pkgconfig = find_program('pkg-config')
hypr_header_root = ''
foreach flag : run_command(pkgconfig, '--cflags-only-I', 'hyprland', check: true).stdout().strip().split()
  candidate = flag.substring(2)
  if hypr_header_root == '' and fs.is_file(candidate / 'src/plugins/PluginAPI.hpp')
    hypr_header_root = candidate
  endif
endforeach
if hypr_header_root == ''
  error('Selected hyprland.pc has no src/plugins/PluginAPI.hpp header root')
endif
shared_library('UltraEdge', 'src/main.cpp',
  include_directories: include_directories(hypr_header_root),
  dependencies: [hypr, dependency('lua'), dependency('libeis-1.0'), meson.get_compiler('cpp').find_library('dl', required: false)],
  cpp_args: ['-DWLR_USE_UNSTABLE', '-fno-gnu-unique'], install: true,
  override_options: ['b_lundef=false'])
test('edge-state-machine', executable('edge-tests', 'tests/engine.cpp', include_directories: include_directories('src')))

lua = find_program('lua', required: false)
if lua.found()
  test('offline-gesture-config', lua, args: [files('tests/gesture-config.lua'), meson.current_source_dir() / 'examples/gestures.lua'])
  test('offline-lua-config', lua, args: [files('tests/local-config.lua'), meson.current_source_dir() / 'examples/hyprv.lua'])
endif

python = find_program('python3')
test('plugin-lua-input-regression', python, args: [files('tests/plugin_regression.py'), '--build-dir', meson.current_build_dir() / 'plugin-regression', '--cxx', ' '.join(meson.get_compiler('cpp').cmd_array())])
