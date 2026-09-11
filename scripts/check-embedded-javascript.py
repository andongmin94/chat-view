#!/usr/bin/env python3

import argparse
import re
import subprocess
import tempfile
from pathlib import Path


RAW_SCRIPT_PATTERN = re.compile(r'LR"JS\((.*?)\)JS"', re.DOTALL)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract and syntax-check embedded JavaScript raw strings."
    )
    parser.add_argument("sources", nargs="+", type=Path)
    args = parser.parse_args()

    snippets: list[tuple[Path, int, str]] = []
    for source in args.sources:
        text = source.read_text(encoding="utf-8")
        matches = RAW_SCRIPT_PATTERN.findall(text)
        if not matches:
            raise RuntimeError(f"No LR\"JS raw string was found in {source}")
        snippets.extend((source, index, script) for index, script in enumerate(matches, 1))

    with tempfile.TemporaryDirectory(prefix="chatview-js-") as directory:
        root = Path(directory)
        for source, index, script in snippets:
            destination = root / f"{source.stem}-{index}.js"
            destination.write_text(script, encoding="utf-8")
            subprocess.run(
                ["node", "--check", str(destination)],
                check=True,
            )

    print(f"Validated {len(snippets)} embedded JavaScript block(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
