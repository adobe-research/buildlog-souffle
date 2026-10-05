"""Check `souffle --serve --serve-deferred-output`: deferred outputs written by DUMP must equal
a standalone run, the legacy protocol must be unchanged, and misuse must fail explicitly."""

import filecmp
import shutil
import subprocess
import sys
from pathlib import Path

souffle, src, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
program = src / "deferred.dl"
facts = src / "deferred_facts"
ALL = "b,c,d,p,q,rec,adt,last"


def mkdir(name):
    path = work / name
    path.mkdir()
    return path


def serve(requests, *flags, prog=program):
    return subprocess.run([souffle, "--serve", *flags, "-j1", prog], input=requests, text=True,
                          capture_output=True)


def req(out, extra=""):
    return f"{facts}\t{out}{extra}\n"


def same(a, b, what):
    names = sorted(p.name for p in a.iterdir())
    assert names == sorted(p.name for p in b.iterdir()), (what, names, sorted(p.name for p in b.iterdir()))
    match, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
    assert not mismatch and not errors, (what, mismatch, errors)


def fails(cp, message, what):
    assert cp.returncode != 0 and message in cp.stderr, (what, cp)


ref = mkdir("ref")
subprocess.run([souffle, "-F", facts, "-D", ref, "-j1", program], check=True)
assert len(list(ref.iterdir())) == 9, sorted(p.name for p in ref.iterdir())  # p2.csv, empty outputs
# Mixed directives: p2.csv uses another delimiter.
assert ";" in (ref / "p2.csv").read_text()

# The capability is advertised and the legacy protocol (flag absent) is unchanged.
help_text = subprocess.run([souffle, "--help"], text=True, capture_output=True).stdout
assert "--serve-deferred-output" in help_text
legacy = mkdir("legacy")
cp = serve(req(legacy))
assert cp.returncode == 0 and len(cp.stdout.splitlines()) == 2, cp
same(ref, legacy, "legacy")
for bad in (req(legacy, "\tdefer=c"), "DUMP\n"):
    fails(serve(bad), "expected FACT-DIR<TAB>OUTPUT-DIR, got", "feature disabled")

# The option needs --serve.
cp = subprocess.run([souffle, "--serve-deferred-output", "-D", "-", program], input="", text=True,
                    capture_output=True)
fails(cp, "--serve-deferred-output requires --serve", "no --serve")

# Enabled: a plain request is a full evaluation; replies are unchanged.
full = mkdir("full")
cp = serve(req(full), "--serve-deferred-output")
assert cp.returncode == 0 and cp.stdout.splitlines()[0] == "souffle-serve: ready", cp
assert cp.stdout.splitlines()[1].startswith("souffle-serve: done "), cp
same(ref, full, "enabled full")

# Deferred outputs: nothing but the listed relations is written before DUMP, and DUMP completes the
# directory to the standalone result, whichever relations are deferred.
for i, now in enumerate(["", "c", "p", "b,d", "rec,adt,q", ALL, "c,c"]):
    out = mkdir(f"d{i}")
    cp = serve(req(out, f"\tdefer={now}") + "DUMP\n", "--serve-deferred-output")
    assert cp.returncode == 0, (now, cp)
    lines = cp.stdout.splitlines()
    assert lines[2].startswith("souffle-serve: dumped "), lines
    same(ref, out, f"defer={now}")
    pre = mkdir(f"pre{i}")
    cp = serve(req(pre, f"\tdefer={now}"), "--serve-deferred-output")
    assert cp.returncode == 0, cp
    written = {p.name for p in pre.iterdir()}
    expected = {n for n in (now.split(",") if now else [])}
    expected = {f"{n}.csv" for n in expected} | ({"p2.csv"} if "p" in expected else set())
    assert written == expected, (now, written, expected)
    for name in written:
        assert (pre / name).read_bytes() == (ref / name).read_bytes(), name

# Successive deferred requests replace the state; a normal request discards it.
a, b, c = mkdir("succ_a"), mkdir("succ_b"), mkdir("succ_c")
cp = serve(req(a, "\tdefer=c") + req(b, "\tdefer=d") + "DUMP\n", "--serve-deferred-output")
assert cp.returncode == 0, cp
same(ref, b, "second deferred request")
assert {p.name for p in a.iterdir()} == {"c.csv"}, "first deferred state was dumped"
cp = serve(req(a, "\tdefer=c") + req(c) + "DUMP\n", "--serve-deferred-output")
fails(cp, "DUMP without deferred outputs", "dump after normal request")
same(ref, c, "normal request after deferred")
# Evaluations after a DUMP are fine and independent.
e, f = mkdir("after_a"), mkdir("after_b")
cp = serve(req(e, "\tdefer=") + "DUMP\n" + req(f, "\tdefer=b") + "DUMP\n", "--serve-deferred-output")
assert cp.returncode == 0, cp
same(ref, e, "first of two cycles")
same(ref, f, "second of two cycles")

# Protocol failures are explicit and nonzero.
bad = mkdir("bad")
flag = "--serve-deferred-output"
fails(serve("DUMP\n", flag), "DUMP without deferred outputs", "no state")
fails(serve(req(bad, "\tdefer=c") + "DUMP\nDUMP\n", flag), "already dumped", "repeated DUMP")
fails(serve(req(bad, "\tdefer=nope"), flag), "unknown output relation: nope", "unknown name")
fails(serve(req(bad, "\tdefer=a"), flag), "unknown output relation: a", "input-only name")
fails(serve(req(bad, "\tdefer=c,,d"), flag), "malformed relation list", "empty element")
fails(serve(req(bad, "\tdefer=c,"), flag), "malformed relation list", "trailing comma")
fails(serve(req(bad, "\tfilter=c"), flag), "unknown request field", "unknown field")
fails(serve(req(bad, "\tdefer=c\tx"), flag), "expected FACT-DIR", "four fields")
fails(serve("nofields\n", flag), "expected FACT-DIR", "one field")
fails(serve(f"{facts}\t{work / 'missing'}\tdefer=c\n", flag), "does not exists", "missing dir")

# A failing writer is reported at DUMP, not swallowed, and no `dumped` reply is sent.
bad_prog = src / "deferred_badwriter.dl"
out = mkdir("badwriter")
cp = serve(req(out, "\tdefer=f") + "DUMP\n", flag, prog=bad_prog)
assert cp.returncode != 0 and "cannot contain" in cp.stderr and "dumped" not in cp.stdout, cp
assert cp.stdout.splitlines()[1].startswith("souffle-serve: done "), cp
assert [p.name for p in out.iterdir()] == ["f.csv"], list(out.iterdir())
# Without deferral the same error surfaces during the evaluation.
cp = serve(req(out, "\tdefer=f,bad"), flag, prog=bad_prog)
assert cp.returncode != 0 and "cannot contain" in cp.stderr, cp

# JSON and compressed file outputs have no completion checks; they cannot be deferred.
out = mkdir("unsupported")
for name in ("jsonfile", "compress"):
    prog = src / f"deferred_{name}.dl"
    cp = serve(req(out, "\tdefer=f"), flag, prog=prog)
    fails(cp, "cannot defer output relation g", name)
    assert not any(out.iterdir())

# A destination that became unusable before DUMP must fail DUMP, never reply `dumped`.
def dump_after_sabotage(out, sabotage):
    proc = subprocess.Popen([souffle, "--serve", flag, "-j1", program], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    proc.stdin.write(req(out, "\tdefer=c"))
    proc.stdin.flush()
    assert proc.stdout.readline() == "souffle-serve: ready\n"
    assert proc.stdout.readline().startswith("souffle-serve: done ")
    sabotage(out)
    proc.stdin.write("DUMP\n")
    proc.stdin.close()
    err, rest = proc.stderr.read(), proc.stdout.read()
    assert proc.wait() != 0 and "Cannot open output file" in err and "dumped" not in rest, (err, rest)


gone = mkdir("gone")
dump_after_sabotage(gone, shutil.rmtree)
blocked = mkdir("blocked")
dump_after_sabotage(blocked, lambda out: (out / "b.csv").mkdir())
# The same applies without the server.
cp = subprocess.run([souffle, "-F", facts, "-D", blocked, program], text=True, capture_output=True)
assert cp.returncode != 0 and "Cannot open output file" in cp.stderr, cp

# Subsumption erases tuples of output relations that later strata consume.
sub_prog = src / "deferred_subsumption.dl"
sub_ref = mkdir("sub_ref")
subprocess.run([souffle, "-F", facts, "-D", sub_ref, "-j1", sub_prog], check=True)
for i, now in enumerate(["", "cnt", "dist", "small,far"]):
    out = mkdir(f"sub{i}")
    cp = serve(req(out, f"\tdefer={now}") + "DUMP\n", flag, prog=sub_prog)
    assert cp.returncode == 0, cp
    same(sub_ref, out, f"subsumption defer={now}")

# Outputs that are not files cannot be deferred; this is refused before evaluating.
stdout_prog = src / "deferred_stdout.dl"
out = mkdir("nonfile")
cp = serve(req(out, "\tdefer=f"), flag, prog=stdout_prog)
fails(cp, "cannot defer output relation s", "stdout output")
assert not any(out.iterdir())
cp = serve(req(out, "\tdefer=s") + "DUMP\n", flag, prog=stdout_prog)
assert cp.returncode == 0 and (out / "f.csv").read_text() == "1\n2\n3\n4\n", cp
print("ok")
