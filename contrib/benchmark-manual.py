#!/usr/bin/env python3
"""Benchmark two mmdoc executables on a prepared manual, checking all output.

Usage: python3 contrib/benchmark-manual.py BEFORE AFTER SOURCE [RUNS]
Use identical compiler settings for both binaries. One warm-up precedes RUNS
(default 7) alternating timed runs, each writing to a fresh output directory.
Source must include toc.md and any generated documentation.
"""

import hashlib
import pathlib
import statistics
import subprocess
import sys
import tempfile
import time


def digest(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit(__doc__)
    executables = [str(pathlib.Path(p).resolve()) for p in sys.argv[1:3]]
    source = str(pathlib.Path(sys.argv[3]).resolve())
    runs = int(sys.argv[4]) if len(sys.argv) == 5 else 7
    if runs < 1:
        raise SystemExit('RUNS must be positive')
    samples = [[], []]
    reference = None
    with tempfile.TemporaryDirectory(prefix='mmdoc-manual-benchmark-') as directory:
        root = pathlib.Path(directory)
        for repeat in range(runs + 1):
            for index in (range(2) if repeat % 2 == 0 else reversed(range(2))):
                output = root / f'{repeat}-{index}'
                start = time.perf_counter()
                result = subprocess.run(
                    [executables[index], 'nixpkgs', source, str(output)],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                elapsed = time.perf_counter() - start
                if result.returncode:
                    raise SystemExit(result.stdout.decode(errors='replace'))
                if repeat:
                    samples[index].append(elapsed)
                current = digest(output)
                if reference is None:
                    reference = current
                elif current != reference:
                    raise SystemExit(f'Output differs: repeat {repeat}, binary {index}')
    for label, values in zip(('before', 'after'), samples):
        print(f'{label}: median {statistics.median(values):.6f}s; '
              f'runs: {", ".join(f"{v:.6f}" for v in values)}')
    before, after = map(statistics.median, samples)
    print(f'{before / after:.2f}x faster ({100 * (1 - after / before):.1f}% less time); '
          f'{len(reference)} output files identical on every run')


if __name__ == '__main__':
    main()
