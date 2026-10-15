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
config = source[source.index("static int addRule("):source.index("static void loadNative(")]
code = ((fixtures / "plugin_host.hpp").read_text() + bodies +
        "\nstatic void clearGestures() {}\n" + config +
        (fixtures / "plugin_cases.cpp").read_text())
args.build_dir.mkdir(parents=True, exist_ok=True)
cpp = args.build_dir / "plugin_regression.cpp"
exe = args.build_dir / "plugin_regression"
cpp.write_text(code)
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "lua"], text=True))
subprocess.run([args.cxx, "-std=c++23", "-O0", "-g", "-I" + str(root / "src"),
                str(cpp), "-o", str(exe), *flags], check=True)
subprocess.run([str(exe)], check=True)
