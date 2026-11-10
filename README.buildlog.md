<!--
SPDX-FileCopyrightText: Copyright 2026 Adobe. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# Adobe Research Souffle fork

This fork supports [buildlog](https://github.com/adobe-research/buildlog), a
Datalog-based meta build system. Adobe's patches and fork-specific files are
licensed under the Apache License, Version 2.0. Upstream Souffle remains under
the Universal Permissive License (UPL-1.0); its copyright and license notices
are preserved.

The production patch series consists of 23 linear commits above upstream
`a1303be3c0166400dee3d1f36f0d96abe03e6901`:
provenance and structured-value fixes, JSON ADT output, interpreter overhead
reductions, a persistent `--serve` interpreter, deferred outputs and engine reuse.
Experimental join scheduling, wrong-arity explanation and stratum-memoisation
branches are not included.

## Build

See the upstream README for build dependencies, including a C++ compiler,
CMake, Bison, Flex and the required libraries. For buildlog, enable 64-bit domains:

```sh
cmake -S . -B build -DSOUFFLE_DOMAIN_64BIT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
export BUILDLOG_SOUFFLE="$PWD/build/src/souffle"
```

The `tests/provenance`, `tests/semantic/jsonfile_adt` and `tests/serve`
directories contain regression cases for the production patches. Configure
with `-DSOUFFLE_ENABLE_TESTING=ON` to run the test suite with CTest.

## Notices

Copyright 2026 Adobe. All rights reserved, in Adobe's modifications.
Existing upstream copyright and license notices are preserved.
Adobe modification notices are included in changed source files; `REUSE.toml`
covers line-sensitive regression inputs and expected outputs without changing
their contents. Mixed files use `UPL-1.0 AND Apache-2.0`: the UPL covers the
upstream portions and Apache-2.0 covers Adobe's modifications, not a choice
between licenses.

See `licenses/Apache-2.0.txt` for Adobe's terms, `NOTICE` for attribution,
and `LICENSE` and `licenses/SOUFFLE-UPL.txt` for upstream terms.
