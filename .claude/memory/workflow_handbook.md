---
name: Workflow Handbook
description: Step-by-step checklist for Claude to follow when completing any task in this project — build, test, verify CI, and document.
type: feedback
---

# Workflow Handbook

This handbook defines the mandatory steps to complete any non-trivial change in this project.
Follow these in order. Never skip CI verification.

---

## 1. Local Verification (before committing)

Run these in order. Fix every failure before moving on.

```bash
./manage.sh build      # Release build must be clean (no warnings)
./manage.sh test       # Unit tests must pass, 0 ASAN errors
./manage.sh valgrind   # 0 Valgrind errors, 0 leaks
./manage.sh coverage   # core+infra coverage must stay >90%
```

**Why:** -Werror makes any warning a build failure. ASAN and Valgrind catch
different classes of bugs. Coverage protects core/infra quality.

---

## 2. Check for Uncommitted Changes

```bash
git diff --name-only HEAD
git status
```

**Why:** Previous sessions showed that file edits often remain unstaged.
Headers (.h files) and new source files are easily forgotten. Always stage
every changed file explicitly by name — never use `git add -A`.

Files to always check:
- `src/**/*.h` — headers with struct/signature changes
- `tests/**` — test files, CMakeLists

---

## 3. Commit

- Stage only relevant files by name
- Write a clear commit message: one subject line + bullet points for each file changed
- Always add `Co-Authored-By: Claude <noreply@anthropic.com>`

```bash
git add <specific files>
git commit -m "$(cat <<'EOF'
Subject line (imperative, <70 chars)

- file1: what changed and why
- file2: what changed and why

Co-Authored-By: Claude <noreply@anthropic.com>
EOF
)"
```

---

## 4. Push and Verify CI

```bash
git push
sleep 15
gh run list --limit 3
```

All workflows must be `completed success`.

If any workflow fails:
```bash
gh run view <run-id> --log-failed
```
Fix the error, commit the fix, push again. Repeat until green.

---

## 5. Quality Gates (never regress these)

| Check | Target | How to measure |
|-------|--------|----------------|
| Unit tests | All pass | `./manage.sh test` |
| ASAN | 0 errors | `./manage.sh test` |
| Valgrind | 0 errors, 0 leaks | `./manage.sh valgrind` |
| Core+Infra coverage | >90% lines | `./manage.sh coverage` |
| CI workflows | All green | `gh run list --limit 3` |

---

## 6. Common Pitfalls

| Pitfall | Fix |
|---------|-----|
| `Config has no member X` | Uncommitted header — stage it |
| `libgcov: overwriting profile data` | `find build -name "*.gcda" -delete` |
| Valgrind `uninit value` | Use `= {0}` for stack-allocated structs |
| CI fails but local passes | Missing committed file — check `git diff --name-only HEAD` |
