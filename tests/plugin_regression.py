"""Exercise actual plugin Lua and input methods with an offline host."""
from pathlib import Path
import argparse
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--cxx", default="c++")
args = parser.parse_args()
root = args.source_root.resolve()
fixtures = Path(__file__).resolve().parent
source = (root / "src/main.cpp").read_text()
bodies = source[source.index("struct Binding {"):source.index("static void mouse(")]
config = source[source.index("static int addRule("):source.index("static SP<IPC::Socket1::SCommand> status;")]
declarations_start = "static bool inputCaptured();" if "static bool inputCaptured();" in source else "static bool localInputAllowed();"
aliases = source[source.index("using MouseFn ="):source.index("static SP<CEventLoopTimer> timer;")]
declarations = source[source.index(declarations_start):source.index("struct Binding {")]
inputs = source[source.index("static void mouse("):source.index("static int addRule(")]
timer_start = source.index("[](SP<CEventLoopTimer> self, void *)")
timer_end = source.index("},\n      nullptr", timer_start) + 1
timer = "\nstatic auto timerTick = " + source[timer_start:timer_end] + ";\n"
listeners = ""
for name in ("Begin", "Update", "End"):
    start = source.index("[](IPointer::SSwipe" + name + "Event e,")
    end = source.index("}));", start) + 1
    listeners += "\nstatic auto swipe" + name + " = " + source[start:end] + ";\n"
code = ((fixtures / "plugin_host.hpp").read_text() + aliases + declarations + bodies +
        inputs + config + timer + listeners +
        (fixtures / "plugin_cases.cpp").read_text())
args.build_dir.mkdir(parents=True, exist_ok=True)
cpp = args.build_dir / "plugin_regression.cpp"
exe = args.build_dir / "plugin_regression"
cpp.write_text(code)
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "lua"], text=True))
subprocess.run([*shlex.split(args.cxx), "-std=c++23", "-O0", "-g", "-I" + str(root / "src"),
                str(cpp), "-o", str(exe), *flags], check=True)
subprocess.run([str(exe)], check=True)
