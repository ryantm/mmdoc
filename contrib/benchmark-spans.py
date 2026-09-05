#!/usr/bin/env python3
"""Compare two mmdoc executables on pages containing many anchored spans.

Usage: python3 contrib/benchmark-spans.py BEFORE AFTER [PARAGRAPHS ...]
Both executables should use the same compiler and optimization settings.
Reports the median of three warm runs and checks every generated file.
"""

import hashlib
import pathlib
import statistics
import subprocess
import sys
import tempfile
import time


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    executables = [str(pathlib.Path(arg).resolve()) for arg in sys.argv[1:3]]
    counts = [int(arg) for arg in sys.argv[3:]] or [1000, 2000, 4000]
    if any(count <= 0 for count in counts):
        raise SystemExit('Paragraph counts must be positive')
    with tempfile.TemporaryDirectory(prefix='mmdoc-benchmark-') as directory:
        root = pathlib.Path(directory)
        for count in counts:
            source = root / f'source-{count}'
            source.mkdir()
            (source / 'toc.md').write_text('[Page](#page)\n')
            (source / 'page.md').write_text('# Page {#page}\n\n' + ''.join(
                f'Paragraph {i} with [text]{{#span{i}}} and [`code`]{{#code{i}}}.\n\n'
                for i in range(count)))
            digests = []
            times = []
            for index, executable in enumerate(executables):
                output = root / f'output-{index}-{count}'
                samples = []
                for repeat in range(4):
                    start = time.perf_counter()
                    subprocess.run([executable, 'Benchmark', str(source), str(output)],
                                   check=True, stdout=subprocess.DEVNULL)
                    elapsed = time.perf_counter() - start
                    if repeat:
                        samples.append(elapsed)
                times.append(statistics.median(samples))
                digests.append({str(p.relative_to(output)):
                                hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in output.rglob('*') if p.is_file()})
            if digests[0] != digests[1]:
                raise SystemExit(f'Output differs for {count} paragraphs')
            print(f'{count} paragraphs: {times[0]:.4f}s -> {times[1]:.4f}s '
                  f'({times[0] / times[1]:.1f}x faster), identical output', flush=True)


if __name__ == '__main__':
    main()
