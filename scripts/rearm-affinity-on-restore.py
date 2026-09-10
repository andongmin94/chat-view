from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    content = target.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected one anchor in {path}, found {count}: {old[:140]!r}"
        )
    target.write_text(
        content.replace(old, new, 1),
        encoding="utf-8",
        newline="\n",
    )


replace_once(
    "src/hud/hud-window.cpp",
    "    if (capture_risk_) {\n"
    "        if (edit_mode_) {\n"
    "            capture_and_persist_bounds();\n"
    "            edit_mode_ = false;\n"
    "            apply_window_mode();\n"
    "            update_host_state();\n"
    "        }\n"
    "        ShowWindow(window_, SW_HIDE);\n"
    "        return true;\n"
    "    }\n\n"
    "    if (!capture_exclusion_intact()) {\n",
    "    if (capture_risk_) {\n"
    "        if (edit_mode_) {\n"
    "            capture_and_persist_bounds();\n"
    "            edit_mode_ = false;\n"
    "            apply_window_mode();\n"
    "            update_host_state();\n"
    "        }\n"
    "        ShowWindow(window_, SW_HIDE);\n"
    "        return true;\n"
    "    }\n\n"
    "    if (!SetWindowDisplayAffinity(\n"
    "            window_, WDA_EXCLUDEFROMCAPTURE)) {\n"
    "        debug_windows_error(\n"
    "            L\"SetWindowDisplayAffinity(restore HUD)\");\n"
    "        fail_closed_capture_exclusion();\n"
    "        return false;\n"
    "    }\n"
    "    if (!capture_exclusion_intact()) {\n",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    "                << L\"The HUD exited while capture suppression was active \"\n",
    "                << L\"The HUD exited during the capture-suppression transition \"\n",
)

architecture_path = ROOT / "docs/architecture.md"
architecture = architecture_path.read_text(encoding="utf-8")
old = (
    "While policy suppression has already hidden the HUD, the periodic affinity probe is paused; "
    "affinity is checked immediately before and after every restoration, so a hidden-window query cannot terminate the process while the safety state is active."
)
if old not in architecture:
    raise RuntimeError("Architecture restore-affinity paragraph was not found")
architecture = architecture.replace(
    old,
    "While policy suppression has already hidden the HUD, the periodic affinity probe is paused. Before any restoration, the runtime explicitly reapplies `WDA_EXCLUDEFROMCAPTURE`, verifies it while the window is still hidden, shows the HUD, and verifies it again. A hide transition therefore cannot leave the restored HUD relying on stale affinity state.",
    1,
)
architecture_path.write_text(architecture, encoding="utf-8", newline="\n")

readme_path = ROOT / "README.md"
readme = readme_path.read_text(encoding="utf-8")
old = (
    "While hidden by that policy, the HUD pauses the periodic hidden-window affinity query and instead verifies capture exclusion immediately before and after restoration."
)
if old not in readme:
    raise RuntimeError("README restore-affinity sentence was not found")
readme = readme.replace(
    old,
    "While hidden by that policy, the HUD pauses the periodic hidden-window affinity query. It explicitly reapplies and verifies capture exclusion before showing again, then verifies once more after restoration.",
    1,
)
readme_path.write_text(readme, encoding="utf-8", newline="\n")

for obsolete in (
    ".github/workflows/rearm-affinity-on-restore.yml",
    "scripts/rearm-affinity-on-restore.py",
):
    target = ROOT / obsolete
    if target.exists():
        target.unlink()
