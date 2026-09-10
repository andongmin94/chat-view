from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    content = target.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected one anchor in {path}, found {count}: {old!r}"
        )
    target.write_text(
        content.replace(old, new, 1),
        encoding="utf-8",
        newline="\n",
    )


replace_once(
    "src/hud/hud-window.cpp",
    "        if (wparam == kCaptureSafetyTimerId) {\n"
    "            if (!capture_exclusion_intact()) {\n",
    "        if (wparam == kCaptureSafetyTimerId) {\n"
    "            if (!capture_risk_ && !capture_exclusion_intact()) {\n",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    "    if (!wait_for_visibility(window, true)) {\n"
    "        return fail(\n"
    "            L\"The HUD did not resume after runtime capture suppression\",\n"
    "            child_process.get());\n"
    "    }\n",
    "    if (!wait_for_visibility(window, true)) {\n"
    "        if (WaitForSingleObject(child_process.get(), 0U) == WAIT_OBJECT_0) {\n"
    "            DWORD exit_code = STILL_ACTIVE;\n"
    "            GetExitCodeProcess(child_process.get(), &exit_code);\n"
    "            std::wcerr\n"
    "                << L\"The HUD exited while capture suppression was active \"\n"
    "                << L\"(exit code \" << exit_code << L\")\\n\";\n"
    "            return 1;\n"
    "        }\n"
    "        return fail(\n"
    "            L\"The HUD did not resume after runtime capture suppression\",\n"
    "            child_process.get());\n"
    "    }\n",
)

architecture_path = ROOT / "docs/architecture.md"
architecture = architecture_path.read_text(encoding="utf-8")
anchor = (
    "This interlock is defense in depth above the Windows affinity check; "
    "it is not presented as protection for physical capture-card paths."
)
if anchor not in architecture:
    raise RuntimeError("Architecture capture-policy anchor was not found")
architecture = architecture.replace(
    anchor,
    anchor
    + " While policy suppression has already hidden the HUD, the periodic affinity probe is paused; affinity is checked immediately before and after every restoration, so a hidden-window query cannot terminate the process while the safety state is active.",
    1,
)
architecture_path.write_text(architecture, encoding="utf-8", newline="\n")

for obsolete in (
    ".github/workflows/fix-hidden-capture-check.yml",
    "scripts/fix-hidden-capture-check.py",
):
    target = ROOT / obsolete
    if target.exists():
        target.unlink()
