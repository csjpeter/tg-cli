---
name: Commit Discipline — Stage Every Changed File
description: Always verify uncommitted files before pushing; headers are silently left out, breaking CI on a clean checkout.
type: feedback
---

Always run `git diff --name-only HEAD` before pushing. Stage every changed file by name.

**Why:** In past sessions, files had never been committed: headers with struct/signature changes, integration files, etc. Local builds passed because the files existed on disk. CI checked out the repo fresh and failed — a clean-room failure invisible locally.

**How to apply:** After any multi-file change, run `git diff --name-only HEAD` and `git status` before pushing. Be especially suspicious of: `.h` headers (struct/signature changes), test files, and any file touched in a previous session that "seemed done." Never use `git add -A` — stage files by name to stay aware of what's going in.
