from pathlib import Path
import base64
import json
import zlib

payload_text = "".join(
    Path(f"scripts/.runtime-telemetry-payload-{index:02d}").read_text(
        encoding="ascii").strip()
    for index in range(6)
)
payload = json.loads(
    zlib.decompress(base64.b64decode(payload_text)).decode("utf-8"))

# The two Control Center fallback paths use different indentation because one
# sits inside an if block and the other is a separate method. Keep both
# replacements explicit instead of treating whitespace-distinct anchors as
# duplicates.
for operation in payload["patch_ops"]:
    if (
        operation[0] == "replace_count"
        and operation[1] == "src/config/main.cpp"
        and "output_value_" in operation[2]
        and "EnableWindow(edit_button_, FALSE);" in operation[2]
        and operation[4] == 2
    ):
        operation[4] = 1
        break
else:
    raise SystemExit("the nested Control Center fallback patch was not found")

payload["patch_ops"].append([
    "replace",
    "src/config/main.cpp",
    '''        set_colored_text(
            output_value_,
            L"●  Unknown",
            kColorMuted,
            output_color_);
        EnableWindow(edit_button_, FALSE);''',
    '''        set_colored_text(
            output_value_,
            L"●  Unknown",
            kColorMuted,
            output_color_);
        set_colored_text(
            history_value_,
            L"●  Unknown",
            kColorMuted,
            history_color_);
        EnableWindow(edit_button_, FALSE);''',
])


def write_file(path_text, content):
    path = Path(path_text)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path_text, old, new):
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"{path_text}: expected one anchor, found {count}: "
            f"{old[:120]!r}")
    path.write_text(
        text.replace(old, new, 1), encoding="utf-8", newline="\n")


def replace_exact_count(path_text, old, new, expected):
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != expected:
        raise SystemExit(
            f"{path_text}: expected {expected} anchors, found {count}: "
            f"{old[:120]!r}")
    path.write_text(
        text.replace(old, new), encoding="utf-8", newline="\n")


def replace_between(
    path_text, start_marker, end_marker, replacement
):
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit(
            f"{path_text}: start marker not found: {start_marker!r}")
    if text.find(start_marker, start + len(start_marker)) >= 0:
        raise SystemExit(
            f"{path_text}: start marker was not unique: "
            f"{start_marker!r}")
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit(
            f"{path_text}: end marker not found after start: "
            f"{end_marker!r}")
    path.write_text(
        text[:start] + replacement + text[end:],
        encoding="utf-8",
        newline="\n")


for path, content in payload["write_files"].items():
    write_file(path, content)

for operation in payload["patch_ops"]:
    kind = operation[0]
    if kind == "replace":
        replace_once(operation[1], operation[2], operation[3])
    elif kind == "replace_count":
        replace_exact_count(
            operation[1],
            operation[2],
            operation[3],
            operation[4])
    elif kind == "between":
        replace_between(
            operation[1],
            operation[2],
            operation[3],
            operation[4])
    else:
        raise SystemExit(f"unknown patch operation: {kind}")

cmake = Path("CMakeLists.txt").read_text(encoding="utf-8")
control = Path(
    "src/common/control-status.hpp").read_text(encoding="utf-8")
runtime = Path(
    "src/plugin/runtime-controller.cpp").read_text(encoding="utf-8")
config = Path(
    "src/config/main.cpp").read_text(encoding="utf-8")
diagnostics = Path(
    "src/diagnostics/diagnostics-exporter.cpp").read_text(
        encoding="utf-8")

if "VERSION 0.3.9" not in cmake:
    raise SystemExit("version 0.3.9 was not installed")
if "kControlStatusVersion = 2U" not in control:
    raise SystemExit("control-status protocol v2 was not installed")
if "runtime-history-v1.bin" not in Path(
    "src/plugin/runtime-history-store.cpp").read_text(
        encoding="utf-8"):
    raise SystemExit("runtime-history store was not installed")
if "RuntimeTelemetryCircuitOpen" not in runtime:
    raise SystemExit("runtime circuit telemetry was not wired")
if config.count("history_value_") < 10:
    raise SystemExit("Control Center restart-history row was incomplete")
if "Restart history" not in config:
    raise SystemExit(
        "Control Center restart-history row was not installed")
if "Last restart event:" not in diagnostics:
    raise SystemExit(
        "diagnostics restart history was not installed")
