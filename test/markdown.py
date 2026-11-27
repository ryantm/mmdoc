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
        "Multi\n\n:   Lead paragraph.\n\n    ```nix\n    { ok = true; }\n    ```\n\n"
        "    Back to [](#start).\n\n"
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
    assert "``` nix\n{ ok = true; }\n```" in tips and "``` nix\n{ ok = true; }\n```" in full
    assert "Back to [Welcome](../../index.md#start)." in tips, tips
    assert "Back to [Welcome](#start)." in full, full
    tips_html = tips_path.with_suffix(".html").read_text()
    assert '<code class="language-nix">{ ok = true; }' in tips_html, tips_html
    assert "```nix" not in tips_html, tips_html
    assert all(tag not in full for tag in ("<nav", "<script", "<style", "<html"))
    assert "Unlisted chapter." not in full and not (out / "private/index.md").exists()
    assert full.count("# Welcome {#start}") == full.count("# `Tips` {#tip}") == 1
    assert full.index("# Welcome") < full.index("# `Tips`")
    assert (out / "index.html").is_file() and tips_path.with_suffix(".html").is_file()
    assert not (out / "private").exists()

    # HTML links and search index percent-encode special characters in directory names.
    root_html = (out / "index.html").read_text()
    assert "guide/tips%20%231/#warning" in root_html, root_html
    assert "guide/tips #1/" not in root_html, root_html
    search_files = list(out.glob("search_index.*.js"))
    assert len(search_files) == 1
    search_text = search_files[0].read_text()
    assert '"url": "guide/tips%20%231/"' in search_text, search_text
    assert "Welcome\\n\\nRoot" in search_text, search_text

    # Single-page output deduplicates chapters, fills empty cross-ref titles,
    # and uses local #anchor heading links.
    single_html = (src / "build" / "single" / "index.html").read_text()
    assert single_html.count("<h1 id='start'>") == 1
    assert single_html.count("<h1 id='tip'>") == 1
    assert '<a href="#start">Welcome</a>' in single_html
    assert '<a href="#tip">Tips</a>' in single_html
    assert "<h1 id='start'><a href='#start'>" in single_html
    assert "href='/#start'" not in single_html

    # CLI help/version flags and trailing slashes work cleanly.
    assert subprocess.run([mmdoc, "--help"], stdout=subprocess.DEVNULL).returncode == 0
    assert subprocess.run([mmdoc, "-v"], stdout=subprocess.DEVNULL).returncode == 0
    subprocess.run([mmdoc, "My Manual", "./", "build/"], cwd=src, check=True)
    assert (out / "llms-full.txt").read_text() == full

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
