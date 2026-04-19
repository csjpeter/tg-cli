# DOC-20 — No SPDX/license headers in project source files

## Category
Documentation / Legal

## Severity
Low

## Finding
The project is GPLv3 (per `README.md` and the root `LICENSE` file), but source
files in `src/` carry no license header or SPDX identifier.  The only exception
is `src/core/telegram_server_key.h` which has a comment pointing to the TDLib
BSL-1.0 origin.

Without per-file SPDX headers, automated compliance tools (REUSE, FOSSA,
licensee) cannot correctly attribute the license, and the BSL-1.0 snippet in
`telegram_server_key.h` is not clearly distinguished from the GPL body of the
project.

## Evidence
- `grep -r "SPDX\|copyright\|license" src/ --include="*.c" --include="*.h"` —
  only one hit (`telegram_server_key.h:5`)
- `/home/csjpeter/ai-projects/tg-cli/README.md:322` — `[GPL v3](LICENSE)`

## Fix
1. Add `// SPDX-License-Identifier: GPL-3.0-or-later` to every `.c`/`.h`
   under `src/` except vendor.
2. For `src/vendor/tinf/*`: add `// SPDX-License-Identifier: Zlib` (matches
   tinf's existing LICENSE file).
3. For `src/core/telegram_server_key.h`: add `// SPDX-License-Identifier: BSL-1.0`
   and a `// Copyright (c) TDLib contributors` line.
