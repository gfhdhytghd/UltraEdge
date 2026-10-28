# UltraEdge：fork Hyprland v0.57.0 只读审查

## 结论与范围

发现 **1 项 blocker、3 项 should-fix、1 项 nit**。当前版本存在发布阻塞项：`reload full-reset` 关闭 Lua 状态后，插件仍用旧状态清理 registry 引用。

CMake Release 构建通过，CTest **4/4** 通过。Meson Release 构建通过，测试 **4/4** 通过。构建通过不代表生命周期和手势协议正确。

- 插件基线：`6f8a325995117c8ba0690bfeac9a3144fdabdce5`。本地 `HEAD`、`origin/main`、`origin/HEAD` 指向同一提交。没有联网刷新远端引用。
- Hyprland 基线：`f6725e43eed50eaa061aa25fe7c9f8cdc9d355e2`。其工作区干净。
- Aquamarine 源码：`4abfcabbf3908cabccead39e5b5b8361dbf2546a`。
- 隔离 pkg-config 版本：Hyprland `0.57.0`、Aquamarine `0.15.1`、hyprutils `0.14.3`、hyprland-protocols `0.8.0`。
- 构建副本：`/tmp/ultraedge-057-review.xf1Fqd`，由当前插件的 `git archive HEAD` 创建。
- 以下 `Hyprland/...` 路径均相对 `../../Hyprland/`。插件路径均相对当前目录。
- 只向当前插件目录写本报告。没有修改源码、提交、推送、运行 hyprpm、加载插件或操作合成器。
- 交付检查：`git diff --exit-code` 通过；22 个受版本控制文件与 HEAD 归档副本的 SHA-256 全部一致。`git status --short` 仅列出 `?? REVIEW_057.md`。

发现针对当前快照成立；不声称这些问题全部由本次移植引入。

## 发现

### R01 — blocker：full-reset 后清理失效的 Lua 状态

**插件证据**

- `src/main.cpp:74-80`：`Binding` 保存裸 `lua_State*`；析构直接调用 `luaL_unref`。
- `src/main.cpp:495-498`：只将 coroutine 转成主 Lua 状态，没有保存状态的生命周期标记。
- `src/main.cpp:644-648`：插件依赖 `config.preReload` 清理规则和 Lua 引用。

**fork 证据**

- `Hyprland/src/ipc/s1/Commands.cpp:1355-1364`：`full-reset` 先销毁旧 config manager，再创建并初始化新 manager。
- `Hyprland/src/config/lua/ConfigManager.cpp:425-437`：旧 manager 析构时，将生命周期标记置空并关闭 Lua 状态。
- `Hyprland/src/config/lua/ConfigManager.cpp:603-610`：新 manager 的 `init()` 调用 `reload()`，此时才发出 `preReload`。
- `Hyprland/src/config/lua/LuaRef.cpp:32-36,52-57`：fork 自身用生命周期标记阻止关闭状态后的 `luaL_unref`。

**失败场景**

1. 用户配置至少一条带 `on_enter` 的规则。
2. 用户发出 `reload full-reset`。
3. fork 关闭旧 Lua 状态。
4. 新 manager 发出 `preReload`。
5. 插件的 `rules.clear()` 调用 `Binding::~Binding()`，向旧 Lua 状态传入 `luaL_unref`。

旧状态内存没有复用时，这是释放后访问。内存复用为新状态时，旧 registry 编号也不再有效。正常 reload 的顺序不同：它在关闭旧状态之前发出 `preReload`，不属于本项触发场景。

**验证**

已验证：源码调用顺序和离线方法探针。探针使用实际 `Binding`、规则注册和清理方法。它拦截关闭状态后的 `luaL_unref`，不访问释放后的内存：

```text
normal reload order: stale_unref_attempts=0
full-reset order: stale_unref_attempts=3 (intercepted; no dereference of freed state)
```

未验证：真实 full-reset 下的崩溃或 registry 损坏。探针没有运行 fork config manager，也没有证明具体崩溃形态。

**建议**

用 fork 的 `CLuaRef` / `SLuaStateLifetime` 管理引用。所有回调读取和析构都必须检查状态生命周期。不能仅调整当前 `preReload` 回调内部的清理顺序。另补 full-reset 回归，并检查新 manager 的插件 Lua API 重注册；`m_pluginLuaFunctions` 属于旧 manager（`Hyprland/src/config/lua/ConfigManager.cpp:1277,1334-1338`），该注册问题需与 fork 生命周期一起处理。

### R02 — should-fix：输入捕获重置丢弃手势归属，放行没有 begin 的尾部事件

**插件证据**

- `src/main.cpp:277-280`：`SwipeSession::clear()` 同时清空 entries 和 `captured`。
- `src/main.cpp:376-390`：原生或 Viewflow 输入捕获激活时，调用 `nativeSwipe.clear()`。
- `src/main.cpp:587-620`：捕获期间直接返回；捕获结束后只按剩余 `captured` 标记取消 update/end。
- 对照 `src/main.cpp:352-357`：pause/reload/layout 的 `clearGestures()` 特意保留 `captured`，以吞掉已接管手势的尾部。

**fork 证据**

- `Hyprland/src/managers/input/InputManager.cpp:2315-2345`：每个 begin/update/end 都创建独立的取消信息。未取消时调用原生手势管理器并向客户端发送事件；这里没有输入捕获门禁。
- `Hyprland/src/protocols/PointerGestures.cpp:105-147`：发送 update/end 时只检查当前指针焦点，没有检查该客户端是否收到 begin。

**失败场景**

插件接管 `consume=true` 的三指手势并取消 begin。手指尚未离开时，输入捕获激活。下一次 timer/input 调用清空 `captured`。之后的 update/end 不再取消，包括捕获仍激活时的事件。fork 向客户端发送没有 begin 的 update/end。

**验证**

已验证：离线探针执行实际 begin/update/end 回调和捕获清理方法：

```text
capture tail: begin_cancelled=1 update_during_capture_cancelled=0 end_after_capture_cancelled=0
clear control: begin_cancelled=1 end_cancelled=1
```

fork 的透传路径通过源码核对。未验证：真实 Wayland 客户端的反应。

**建议**

将手势归属与规则状态分开保存。输入捕获必须清理规则引用和待执行动作，同时保留已吞 begin 的 native 手势归属，直到对应 end。捕获期间仍必须取消这些 native 尾部事件。鼠标和轴事件继续转发原始 compositor handler，以维持远程输入捕获。

### R03 — should-fix：AI-HUB 示例接受非活动工作区的窗口

**插件证据**

- `examples/hyprv.lua:22-25`：hold provider 只检查 mapped、hidden、指定的隐藏工作区名称和 monitor，没有检查 `workspace.active`。
- `examples/hyprv.lua:31-32`：符合上述条件就返回 hold 区域。
- `src/engine.hpp:157-175`：有效 hold 区域会继续保持 active；provider 返回 nil 才退出。
- `tests/local-config.lua:13-19`：测试数据没有 active 字段，只覆盖指定的隐藏工作区名称。

**fork 证据**

- `Hyprland/src/config/lua/bindings/LuaBindingsQuery.cpp:114-125`：默认 `hl.get_windows()` 只要求 mapped。
- `Hyprland/src/config/lua/objects/LuaWindow.cpp:76-81`：mapped、hidden 与输入可见性是不同属性。
- `Hyprland/src/desktop/view/window/Window.cpp:622-623`：`isHidden()` 仅返回窗口的 hidden 标记。
- `Hyprland/src/config/lua/objects/LuaWorkspace.cpp:134-136`：`workspace.active` 明确表示该输出的活动普通或特殊工作区。

**失败场景**

AI-HUB 留在输出 R 的工作区 1。用户切换到 R 的工作区 2，窗口仍 mapped 且未设置 hidden。provider 继续返回工作区 1 的几何。指针留在该区域时，规则仍保持 active，无法把应用离开当前工作区视为 hold 消失。

**验证**

已验证：用实际 `examples/hyprv.lua` 和 fork 属性形状运行 Lua fixture：

```text
inactive workspace: active=false, hold rectangle present=true, width=420.0
named hidden-workspace control: hold rectangle present=false
```

未验证：真实工作区切换与外部 `sidechat.sh` 的组合行为。

**建议**

provider 增加活动工作区条件。补充普通非活动工作区和非活动特殊工作区用例。若设计允许 pinned 窗口跨工作区保持，则单独声明并测试该例外。

### R04 — should-fix：示例的分数缩放区域没有采用 fork 的逻辑尺寸取整

**插件证据**

- `examples/hyprv.lua:27-32`：旋转并除以 scale 后直接使用小数宽高。
- `src/main.cpp:217-220`：插件核心使用 monitor 的 `m_size`。
- `src/engine.hpp:14-15,162-173`：hold 使用半开区间；指针超过 hold 的右边界就执行 exit。

**fork 证据**

- `Hyprland/src/config/lua/objects/LuaMonitor.cpp:110-113,146-155`：`width/height` 是像素尺寸；`mode.width/height` 暴露 `m_size`，已是逻辑尺寸。
- `Hyprland/src/output/Monitor.cpp:669-671`：fork 用 `(transformed_pixel_size / scale).round()` 计算逻辑尺寸。
- `Hyprland/src/pointer/PointerManager.cpp:857-901,1077-1083`：指针边界来自输出逻辑矩形。
- `Hyprland/src/config/values/ConfigValues.cpp:702`：默认 `cursor:hotspot_padding` 为 0。

**失败场景**

2560×1440 输出使用 scale 1.5。fork 逻辑宽为 1707，provider 右边界为 1706.666667。指针 x=1706.95 在输出内部，却在 hold 区域外。AI-HUB 的区域一旦生效，停留在最右边缘的指针会触发 exit。

**验证**

已验证：实际 Lua provider 的边界计算与 fork 取整公式对照：

```text
fractional scale: provider_right=1706.666667, fork_right=1707, pointer_x=1706.950000, inside_fork=true, inside_hold=false
```

未验证：物理输出上的可见关闭行为。

**建议**

在此 fork 上直接使用 `mon.mode.width/height` 作为逻辑尺寸，不再除以 scale 或交换宽高。补充向上取整、向下取整和旋转输出用例。

### R05 — nit：两个构建入口都无条件传入 GCC 专用选项

**插件证据**

- `CMakeLists.txt:19`、`meson.build:19`：无条件传入 `-fno-gnu-unique`。

**fork 证据**

- `Hyprland/src/plugins/PluginAPI.cpp:289-296`：fork 的符号查找区分 Clang 和 GCC，保留了两种编译器路径。

**失败场景与验证**

已验证：同一隔离前缀下，Clang 23.1.3 的 CMake 配置通过，编译在读取插件源码前失败：

```text
clang++: error: unknown argument: '-fno-gnu-unique'
```

GCC 的发布构建通过，所以本项不阻塞当前 GCC 产物。Meson 使用同一选项；没有另外执行 Clang Meson 构建。

**建议**

CMake 按编译器或选项支持情况添加该选项。Meson 使用 `get_supported_arguments()`。保留 GCC 下禁用 GNU unique 符号的卸载要求。

## 其余检查与证据边界

| 检查范围 | 结果与定位 |
| --- | --- |
| ABI 门禁 | 已验证：`src/main.cpp:538-555` 在注册 Lua、hook、listener、timer 之前比较本地构造的 ABI 和 runtime ABI。构造格式匹配 `Hyprland/src/plugins/PluginAPI.cpp:23-35`；没有调用可被其他插件替换的 inline client helper。产物包含完整 f6725e43 提交号。错误 ABI 的真实加载拒绝未验证。 |
| 两个函数钩子 | 已验证：`src/main.cpp:30-36,435-436` 检查成员函数与替换函数的完整类型，包括 void 返回值及 wheel 的 `SP<IPointer>` 参数。`src/main.cpp:560-586` 要求完整 demangled 名称恰好匹配一次。`nm -D` 对 fork binary 的检查分别找到一个目标。fork 声明见 `Hyprland/src/managers/input/InputManager.hpp:107,110`。实际 trampoline 安装未验证。 |
| 原始调用转发 | 已验证：`src/main.cpp:251-268,392-433` 向原始 handler 传入原始完整事件；wheel 也传入 pointer。捕获期间不吞 mouse/axis。只有本地接管的 2FG 轴路径有意省略原始 wheel handler。 |
| 位移语义 | 已验证：插件的 `delta/unaccel` 选择及 touchpad flip 与 `Hyprland/src/managers/input/InputManager.cpp:130-158` 一致。事件字段见 `Hyprland/src/devices/IPointer.hpp:20-25,114-119`。 |
| 2FG 轴来源 | 已验证：`src/main.cpp:399-404` 要求真实 pointer、touchpad 标记、FINGER 来源、libinput handle 和 2FG 方法。fork 字段见 `Hyprland/src/devices/IPointer.hpp:42-49`；转换见 `Hyprland/src/devices/Mouse.cpp:17-19,56-65`；Aquamarine 原始轴来源见 `../../aquamarine/src/backend/Session.cpp:616-649`。真实设备和自然滚动手感未验证。 |
| monitor 字段与事件 | 已验证：核心使用逻辑 `m_position/m_size`，不重复缩放。`State::monitorState()->monitors()` 返回活动输出（`Hyprland/src/state/MonitorState.cpp:25-51`）。插件只在 added/removed/layoutChanged 上重置，不依赖事件中的旧几何；发出点见 `Hyprland/src/output/Monitor.cpp:382,392,1331`。示例的偏差单列 R03/R04。 |
| 会话锁 | 已验证：状态机将 locked 当作 silent reset（`src/main.cpp:231-233`）；fork 的 `isSessionLocked()` 查询原生 session-lock 状态（`Hyprland/src/managers/SessionLockManager.cpp:143-145`）。没有真实锁屏验收。 |
| 正常 reload | 已验证：fork 在关闭旧状态之前发出 `preReload`（`Hyprland/src/config/lua/ConfigManager.cpp:610,694`）；插件清理规则和手势引用。coroutine 规则绑定主 Lua 状态，既有回归通过。full-reset 例外见 R01。 |
| 正常卸载 | 已验证：`src/main.cpp:683-704` 先清 listener，再清手势引用、撤 hook、cancel/remove timer、清规则、注销 IPC。fork 在 `PLUGIN_EXIT` 后移除所有 owner hook，撤销 Lua 注册，再 dlclose（`Hyprland/src/plugins/PluginSystem.cpp:139-184`）。Lua 缓存闭包按注册 ID 查找，卸载后报错（`Hyprland/src/config/lua/ConfigManager.cpp:1241-1250,1316-1323`），因此插件没有显式 removeLuaFunction 不构成正常卸载缺陷。 |
| timer | 已验证：插件持有 timer 强引用，并在退出时 cancel/remove。fork timer 分发检查 cancelled（`Hyprland/src/managers/eventLoop/EventLoopManager.cpp:142-163`）。没有插件自己的 Wayland idle source。 |
| config value | 已验证：mouse 中的 `CConfigValue` 使用 fork 同名类型。其基类析构从 cache registry 移除对象（`Hyprland/src/config/ConfigValue.cpp:26-31`）。插件没有注册额外 config value。 |
| 注册失败回滚 | 已验证：fork 的 init 异常路径跳过 `PLUGIN_EXIT`，但移除 hook、IPC 和 Lua 注册（`Hyprland/src/plugins/PluginSystem.cpp:117-120,143-175`）。当前源码在 timer/listener 创建后的分配异常未做故障注入，不把极端分配失败列成已复现发现。 |
| 构建元数据 | 已验证：CMake/Meson 只从选定 pkg-config 树定位 `src/plugins/PluginAPI.hpp`；两种 GCC 构建通过。README 明确标出 fork f6725e43；`hyprpm.toml` 输出名与构建产物一致。仓库没有 flake。`VERIFICATION.md` 明确记录历史 0.56.2 证据，本报告不将其当作 0.57 验收。Clang 问题见 R05。 |

## 命令与结果

以下变量只用于简化复核命令：

```sh
H=/tmp/claude-1000/-home-wilf-data-hyprland-plugins/a85e6616-cf82-4173-94c4-fde1f868f04e/scratchpad/hl057
R=/tmp/ultraedge-057-review.xf1Fqd
source "$H/env.sh"
```

### 基线与隔离构建

```sh
git rev-parse HEAD
git log -1 --format='%H %D' HEAD
git status --porcelain=v1
git -C "$H/Hyprland" rev-parse HEAD
git -C "$H/Hyprland" status --short
git -C "$H/aquamarine" rev-parse HEAD
git archive HEAD | tar -x -C "$R"
pkg-config --modversion hyprland aquamarine hyprutils hyprland-protocols
pkg-config --cflags hyprland
bash "$H/build.sh" "$R"
ctest --test-dir "$R/build-057" --output-on-failure
"$R/build-057/edge-tests"
```

结果：build log 最后一行 `EXIT 0`；CTest 4/4 通过；独立 engine 输出 `394 state-machine checks passed`。构建选中的 Hyprland include 路径全部位于隔离 prefix，没有使用 `/usr/include/hyprland`。证据：`$R/build-057.log`、`$R/build-057/build.ninja`、`$R/build-057/Testing/Temporary/LastTest.log`。

```sh
meson setup "$R/build-meson-057" "$R" --buildtype=release
meson compile -C "$R/build-meson-057"
meson test -C "$R/build-meson-057" --print-errorlogs
```

结果：构建通过，测试 4/4 通过。证据：`$R/meson-057.log`、`$R/build-meson-057/meson-logs/testlog.txt`。

### 符号与产物检查

```sh
nm -D --defined-only "$H/Hyprland/build/Hyprland" | c++filt | rg 'CInputManager::onMouse(Moved|Wheel)\('
readelf -Ws "$R/build-057/libUltraEdge.so" | rg 'UNIQUE'
strings "$R/build-057/libUltraEdge.so" | rg 'f6725e43|ABI mismatch'
```

结果：两个 hook 名称各匹配一个导出符号；没有 GNU UNIQUE 符号；产物包含目标提交号及 ABI 拒绝消息。

### 离线发现探针

```sh
python "$R/review_probes.py"
lua "$R/review_examples.lua" "$R/examples/hyprv.lua"
```

结果：R01/R02 的实际插件方法探针通过；R03/R04 的实际 Lua provider 反例通过。探针源码和日志保留于 `$R/review_probes.py`、`$R/review-probes.cpp`、`$R/review-probes.log`、`$R/review_examples.lua`、`$R/review-examples.log`。所有探针都在副本中生成；没有改写被审查的插件方法。

### Clang 反例

```sh
clang++ --version
clang++ -fno-gnu-unique -x c++ -fsyntax-only /dev/null
cmake -S "$R" -B "$R/build-clang-057" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_CXX_COMPILER=clang++
cmake --build "$R/build-clang-057"
```

结果：Clang 23.1.3；配置通过；编译失败，错误为 `unknown argument: '-fno-gnu-unique'`。证据：`$R/clang-057.log`。这项失败不覆盖或否定 GCC 构建结果。

## 未验证

- 没有启动 nested compositor。没有在任何运行中的 compositor 中加载、卸载插件或执行 full-reset。
- 没有验证真实加载的 ABI 拒绝、hook trampoline、正常卸载或重复加载。
- 没有真实鼠标、触摸板、锁屏、多输出、分数缩放或旋转输出验收。
- 没有真实原生输入捕获、Viewflow 插件或 Wayland 手势客户端验收。
- 没有调用外部 `sidechat.sh`，没有检查本机持久配置。
- 离线方法探针使用仓库现有 host fixture。它验证方法逻辑，不验证 fork 的完整 C++ 对象布局或客户端行为。
- 没有注入内存分配失败，也没有验证初始化中途异常下的所有回调销毁路径。
- 没有修改或修复上述发现。报告交付不代表运行验收通过。

## Fixes

本节更新前文的只读审查结论与验证状态。前文保留原审查基线。**R01–R05 全部完成修复并本地提交；本报告列出的发布阻塞项在以下验证范围内消除。** 真实物理输入与屏幕效果仍未验证。

- 修复工作区：`../UltraEdge-fix`，分支 `fix/review-057`。
- 起点：`6f8a325995117c8ba0690bfeac9a3144fdabdce5`；构建与嵌套验证的源码提交：`67b82fc`。
- 原审查工作区的受版本控制源码没有变化。没有合并、推送、运行 hyprpm 或调用宿主 compositor IPC。
- 所有修复提交均包含 fork 行为说明和 `Co-Authored-By: Codex <noreply@openai.com>`。

### 各项修复

| 发现 | 本地提交 | 改动 | 测试与反向检查 | 未完成事项 |
| --- | --- | --- | --- | --- |
| R01 / full-reset Lua API 额外要求 | `fcb92e4` | `Binding` 保存 `SLuaStateLifetime`。调用 action/provider 和释放 registry 引用前检查状态标记。保存 config manager 的弱身份；初始化及 `preReload` 时，仅在 manager 更换后注册 rule/clear/pause。弱身份同时覆盖内存地址复用。正常 reload 不重复注册。 | `plugin-lua-input-regression` 覆盖状态失效后的 action/provider、关闭后的引用清理、manager 更换与普通 reload。分别恢复无生命周期检查的 action、旧析构条件、移除 preReload 重注册后，测试均因对应断言失败。嵌套实例带真实 Lua 引用执行两次 full-reset，通过。 | 无代码遗留；物理桌面未验收。 |
| R02 | `b42ac2f` | 输入捕获清空 native 手势的规则引用，但保留已吞 begin 的手势归属。update/end 在捕获期间及释放后继续取消该流；end 结束归属。捕获期间新 begin 的流继续交给原 handler。鼠标和轴的原始调用保持转发。 | 回归覆盖 native/Viewflow、consume=true/false、timer 清理、捕获期间 update、释放后 update/end、新 begin。恢复 `nativeSwipe.clear()` 后，`end.cancelled` 断言失败。 | 真实触摸板与客户端协议接收未验收。 |
| R03 | `1d31ec7` | AI-HUB provider 要求 `workspace.active`。保留指定隐藏工作区的排除条件。 | `offline-lua-config` 覆盖非活动普通工作区、非活动特殊工作区与活动特殊工作区。移除 active 条件后，测试第 17 行断言失败。 | 外部 sidechat 脚本与真实应用未验收。 |
| R04 | `1d31ec7` | provider 直接使用 fork 的 `mon.mode.width/height`，不再重复缩放或旋转。 | 同一 Lua 回归覆盖 scale 1.5 的向上取整、scale 1.7 的向下取整、旋转输出、负坐标及右边缘指针。恢复旧尺寸公式后，测试第 28 行断言失败。 | 物理输出的屏幕效果未验收。 |
| R05 | `67b82fc` | CMake 用 `check_cxx_compiler_flag`；Meson 用 `get_supported_arguments`。GCC 保留 `-fno-gnu-unique`；Clang 不传入该选项。 | GCC/Clang × CMake/Meson 四组构建及各自完整测试均通过。将两份构建文件分别恢复到原版本后，Clang 编译均以 `unknown argument: '-fno-gnu-unique'` 失败。GCC 产物没有 GNU UNIQUE 符号。 | 无。 |

R01 依赖的 fork 契约没有改变：config manager 在关闭 Lua 前将 lifetime 的 state 置空；full-reset 在新 manager 的 reload 中发出 preReload。具体位置见原 R01 的 fork 证据。修复没有延长旧 manager 的生命周期。

### 构建与完整测试

以下命令在 `UltraEdge-fix` 中执行，使用原审查的隔离前缀：

```sh
source ../../env.sh
bash ../../build.sh "$PWD"
ctest --test-dir build-057 --output-on-failure
cmake -S . -B build-clang-057 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang-057 -j2
ctest --test-dir build-clang-057 --output-on-failure
meson setup build-meson-057 --buildtype=release
meson compile -C build-meson-057 -j2
meson test -C build-meson-057 --print-errorlogs
CXX=clang++ meson setup build-meson-clang-057 --buildtype=release
meson compile -C build-meson-clang-057 -j2
meson test -C build-meson-clang-057 --print-errorlogs
```

| 编译器 / 构建入口 | 构建结果 | 完整测试 | 日志（相对修复工作区） |
| --- | --- | --- | --- |
| GCC / CMake | 通过，`EXIT 0` | 4/4 通过 | `build-057.log`、`ctest-057.log` |
| Clang / CMake | 通过 | 4/4 通过 | `clang-057.log` |
| GCC / Meson | 通过 | 4/4 通过 | `meson-057.log` |
| Clang / Meson | 通过 | 4/4 通过 | `meson-clang-057.log` |

反向检查只修改独立副本，没有撤销修复分支的代码。证据保存在 `/tmp/ultraedge-fix-reverse/`：

- `r01-lifetime/result.log`：恢复旧 action 调用，`!action(*orphan, ...)` 断言失败。
- `r01-destructor/result.log`：恢复 `if (L)` 析构条件，`closedUnrefs==0` 断言失败。探针拦截了失效状态上的 unref，没有解引用释放后的内存。
- `r01-reregister/result.log`：移除 preReload 中的注册调用，`registrations==6 && replacement->functions.size()==3` 断言失败。
- `r02-tail/result.log`：恢复捕获时清空归属，end 取消断言失败。
- `r03-active/result.log`、`r04-geometry/result.log`：分别恢复旧活动条件、旧几何公式，Lua 断言失败。
- `r05-compiler/cmake.log`、`r05-compiler/meson.log`：使用原构建文件，Clang 在选项解析时失败。

R01/R02 的独立副本使用修复后的测试执行：

```sh
python tests/plugin_regression.py --source-root /tmp/ultraedge-fix-reverse/ITEM --build-dir /tmp/ultraedge-fix-reverse/ITEM/build
```

其中 `ITEM` 对应上面的 R01/R02 子目录。R03/R04 使用：

```sh
lua tests/local-config.lua /tmp/ultraedge-fix-reverse/r03-active/hyprv.lua
lua tests/local-config.lua /tmp/ultraedge-fix-reverse/r04-geometry/hyprv.lua
```

所有反向检查均返回非零，且失败原因与对应修复一致。

### 专属 h2 嵌套验证

仅使用 `/run/user/1000/h2`。运行器和每个 hyprctl 子进程都接收以下环境。hyprctl 另外使用该实例新建 socket 目录中的精确 `HYPRLAND_INSTANCE_SIGNATURE`：

```sh
export NESTED_XDG=/run/user/1000/h2
export NESTED_LOG=$PWD/nested.log
export XDG_RUNTIME_DIR=$NESTED_XDG
python build-057/nested-check.py > nested-check.log 2>&1
```

检查脚本与输出保留在修复工作区。脚本调用 `../../nested/run.sh`，没有调用默认运行目录。验证结果：

1. `hyprctl version` 确认运行提交为 `f6725e43eed50eaa061aa25fe7c9f8cdc9d355e2`。
2. `plugin load` 返回 `ok`；列表包含 UltraEdge。
3. Lua `rule/clear/pause` 均通过类型与调用断言；IPC 状态返回一条规则且 error 为空。
4. 普通 `reload` 返回 `ok`；Lua API 再次通过调用断言。
5. 带 on_enter/on_exit/hold 三个真实 registry 引用连续执行两次 `reload full-reset`。每次返回 `ok`，进程持续运行，Lua API 可再次注册规则。
6. `plugin unload` 返回 `ok`；列表为 `no plugins loaded`；Lua rule API 为 absent。
7. 卸载后仍能读取 fork version。脚本只终止自己启动的 PID `3717933`；退出码为 0；h2 无剩余 compositor socket。

`nested.log` 没有 `name collision` 或 `Lua registration failed`。没有接触其他 agent 的运行目录。该结果是嵌套 runtime 证据，不是物理桌面的输入或视觉验收。

初次检查脚本误以为 `hyprctl eval` 会打印 Lua 返回值，因此在 full-reset 前的结果字符串断言失败。fork 的 eval 只返回 `ok`，repl 才打印返回值。脚本改用 repl 后完成上述检查；初次日志保留为 `nested-check-initial.log` 和 `nested-initial.log`。这是检查脚本的修正，没有据此修改插件。

### 遗留边界

没有未修复的报告条目。真实输入捕获设备、Viewflow 插件、Wayland 客户端、AI-HUB/sidechat 应用和物理多输出的验收仍未执行。源码及测试提交留在本地修复分支，未推送或合并到主线。
