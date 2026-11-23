"""Exercise the published Markdown files through the mmdoc CLI."""
from pathlib import Path
import subprocess
import sys
import tempfile

mmdoc = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as directory:
    src = Path(directory)
    assets = src / "images"
    assets.mkdir()
    (assets / "diagram.png").write_bytes(b"diagram")
    (src / "guide").mkdir()
    (src / "toc.md").write_text(
        "* [](#start)\n* [](#tip)\n* [](#root-alias)\n* [](#warning)\n"
    )
    (src / "intro.md").write_text(
        "# Welcome {#start}\n\n[Root]{#root-alias}\n\nSee [](#warning).\n"
    )
    (src / "guide" / "tips #1.md").write_text(
        "# `Tips` {#tip}\n\n[Return][home] or [stay](#term).\n\n[home]: #root-alias\n\n"
        "[`value`]{#term}\n\n::: {.warning #warning}\nKeep a backup.\n:::\n\n"
        "* Run `help`.\n\nTerm\n: Meaning\n\n| Key | Value |\n| --- | --- |\n| a | 42 |\n\n"
        "![Diagram](images/diagram.png) and [example](https://example.org/a#b)\n\n"
        "```sh\nprintf '%s\\n' '[literal](#start)'\n```\n"
    )
    (src / "private.md").write_text("# Unlisted {#hidden}\n\nUnlisted chapter.\n")

    out = src / "build" / "multi"
    args = [mmdoc, "My Manual", ".", "build"]
    subprocess.run(args, cwd=src, check=True)
    index = (out / "llms.txt").read_text()
    assert index.splitlines() == [
        "# My Manual", "", "[Full manual](llms-full.txt)", "", "## Chapters", "",
        "- [Welcome](index.md)", "- [Tips](guide/tips%20%231/index.md)",
    ], index

    root = (out / "index.md").read_text()
    tips_path = out / "guide" / "tips #1" / "index.md"
    tips = tips_path.read_text()
    full = (out / "llms-full.txt").read_text()
    assert "[Tips](guide/tips%20%231/index.md#warning)" in root, root
    assert "[Return](../../index.md#root-alias)" in tips, tips
    assert "[Return](#root-alias)" in full and "[Tips](#warning)" in full
    assert "![Diagram](../../images/diagram.png)" in tips
    assert "![Diagram](images/diagram.png)" in full
    for snippet in ("# `Tips` {#tip}", "[stay](#term)", "[`value`]{#term}",
                    "::: {.warning #warning}\nKeep a backup.\n:::",
                    "Run `help`.", "Term\n: Meaning", "| a | 42 |",
                    "[example](https://example.org/a#b)",
                    "printf '%s\\n' '[literal](#start)'"):
        assert snippet in tips and snippet in full, snippet
    assert "``` sh\n" in tips and "``` sh\n" in full
    assert all(tag not in full for tag in ("<nav", "<script", "<style", "<html"))
    assert "Unlisted chapter." not in full and not (out / "private/index.md").exists()
    assert full.count("# Welcome {#start}") == full.count("# `Tips` {#tip}") == 1
    assert full.index("# Welcome") < full.index("# `Tips`")
    assert (out / "index.html").is_file() and tips_path.with_suffix(".html").is_file()

    # Rebuilding into SRC must not rediscover previously generated chapters.
    subprocess.run(args, cwd=src, check=True)
    assert (out / "llms-full.txt").read_text() == full
    assert not (out / "build").exists()

    with (src / "intro.md").open("a") as intro:
        intro.write("\nNew content.\n")
    tips_path.unlink()
    tips_path.mkdir()
    assert subprocess.run(args, cwd=src).returncode != 0
    assert (out / "llms-full.txt").read_text() == full
    assert (out / "llms.txt").read_text() == index
    assert not list(out.glob(".mmdoc-*"))
