"""Check that `souffle --serve` evaluates each request exactly as a separate
`souffle -F FACTS -D OUT` run would, including when requests repeat."""

import filecmp
import shutil
import subprocess
import sys
from pathlib import Path

souffle, src, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
shutil.rmtree(work, ignore_errors=True)
program = src / "serve.dl"
order = ["facts1", "facts2", "facts1"]
requests = []
for i, facts in enumerate(order):
    alone = work / f"alone{i}"
    served = work / f"served{i}"
    alone.mkdir(parents=True)
    served.mkdir(parents=True)
    subprocess.run([souffle, "-F", src / facts, "-D", alone, program], check=True)
    requests.append(f"{src / facts}\t{served}\n")
# The startup directories must not be the ones evaluated.
startup = work / "startup"
startup.mkdir()
cp = subprocess.run(
    [souffle, "--serve", "-F", src / "facts2", "-D", startup, program],
    input="".join(requests), text=True, capture_output=True, check=True,
)
lines = cp.stdout.splitlines()
assert lines[0] == "souffle-serve: ready", lines
assert len(lines) == 1 + len(order) and all(l.startswith("souffle-serve: done ") for l in lines[1:]), lines
assert not any(startup.iterdir()), "wrote to the startup output directory"
for i in range(len(order)):
    alone, served = work / f"alone{i}", work / f"served{i}"
    names = sorted(p.name for p in alone.iterdir())
    assert names == sorted(p.name for p in served.iterdir()), (names, i)
    match, mismatch, errors = filecmp.cmpfiles(alone, served, names, shallow=False)
    assert not mismatch and not errors, (i, mismatch, errors)
bad = subprocess.run([souffle, "--serve", "-D", "-", program], input="", text=True, capture_output=True)
assert bad.returncode != 0 and "--serve" in bad.stderr, bad
print("ok")
