# SPDX-FileCopyrightText: Copyright 2026 Adobe. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Each request served by one long-lived `souffle --serve` process (optionally with deferred
output and/or stratum memoisation) must produce byte-identical outputs to a separate
`souffle -F FACTS -D OUT` run, whatever requests came before: inputs that shrink, grow,
vanish and repeat; strings, records, ADTs, aggregates, negation and autoinc."""

import filecmp
import shutil
import subprocess
import sys
from pathlib import Path

souffle, src, work, variant = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
shutil.rmtree(work, ignore_errors=True)
program = src / "reuse.dl"
order = ["a", "b", "c", "d", "e", "a", "a", "b", "d", "d", "c", "a"]
flags = ["-j1"]
serve = [souffle, "--serve"]
if "deferred" in variant:
    serve.append("--serve-deferred-output")
if "memo" in variant:
    serve.append("--serve-memo")
outputs = ["path", "reach", "total", "biggest", "rec", "tree", "ids", "words", "live"]

requests = []
expect_replies = ["ready"]
for i, name in enumerate(order):
    facts = src / "reuse_facts" / name
    alone, served = work / f"alone{i}", work / f"served{i}"
    alone.mkdir(parents=True)
    served.mkdir(parents=True)
    subprocess.run([souffle, *flags, "-F", facts, "-D", alone, program], check=True)
    if "deferred" in variant:
        # alternate which outputs are written at once;
        # DUMP writes the remainder
        now = outputs[: i % 4 * 2]
        requests.append(f"{facts}\t{served}\tdefer={','.join(now)}\n")
        expect_replies.append("done")
        requests.append("DUMP\n")
        expect_replies.append("dumped")
    else:
        requests.append(f"{facts}\t{served}\n")
        expect_replies.append("done")

cp = subprocess.run(
    [*serve, *flags, program], input="".join(requests), text=True, capture_output=True, check=True
)
lines = cp.stdout.splitlines()
assert len(lines) == len(expect_replies), lines
for line, want in zip(lines, expect_replies):
    assert line.startswith("souffle-serve: " + want), (line, want)
for i in range(len(order)):
    alone, served = work / f"alone{i}", work / f"served{i}"
    names = sorted(p.name for p in alone.iterdir())
    assert names == sorted(p.name for p in served.iterdir()), (names, i)
    match, mismatch, errors = filecmp.cmpfiles(alone, served, names, shallow=False)
    assert not mismatch and not errors, (i, mismatch, errors)
print("ok")
