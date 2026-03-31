# Issue Workflow

## Statuses

Issues move between directories that represent their current status:

```
incoming/ → pending/ → inprogress/ → ready/ → inreview/ → reviewed/ → inqa/ → verified/
               ↑                                   │                      │
               └──────────── (review reject) ──────┘                      │
               └──────────────────────────── (QA reject) ─────────────────┘
```

| Directory     | Meaning                                                        |
|---------------|----------------------------------------------------------------|
| `incoming/`   | Newly created issue, awaiting Engineer feasibility analysis    |
| `pending/`    | Dependency-ordered queue, ready to be picked up by Developer   |
| `inprogress/` | Actively being worked on by a Developer                        |
| `ready/`      | Development complete, waiting for Reviewer                     |
| `inreview/`   | Code review in progress                                        |
| `reviewed/`   | Review passed, waiting for QA                                  |
| `inqa/`       | QA verification in progress                                    |
| `verified/`   | QA passed, issue closed                                        |

## Roles

### Engineer

**Trigger:** There are issues in `incoming/`.

**Responsibility:** Analyze each incoming issue against the current codebase.
Build the dependency graph across all issues. Place issues into `pending/` in
dependency order — guaranteeing that by the time any issue reaches the front
of the queue, all its prerequisites are already in `verified/`. Issues whose
time has not yet come remain in `incoming/`.

**Input:** `incoming/` issues + full codebase
**Output:** Ordered issues moved to `pending/`

### Developer

**Trigger:** `inprogress/` is empty and there are issues in `pending/`.

**Responsibility:** Pick up the first issue from `pending/`. Implement the
feature or fix. Move to `ready/` only when all quality gates pass.

**Input:** First issue in `pending/`
**Output:** Implemented code committed, issue moved to `ready/`

**Quality gates before moving to `ready/`:**
- `./manage.sh build` — 0 warnings
- `./manage.sh test` — 0 ASAN errors
- `./manage.sh valgrind` — 0 leaks
- `./manage.sh coverage` — core+infra >90%
- All new/changed files committed

**Rules:**
- Only one issue in `inprogress/` at a time
- All dependencies must be in `verified/` before starting

### Reviewer

**Trigger:** There are issues in `ready/`.

**Responsibility:** Code review of the implementation. Verify correctness,
architecture compliance, and code quality. Move to `reviewed/` if acceptable,
or back to `pending/` with documented reasons if not.

**Input:** Issue from `ready/` + changed code
**Output:** Issue moved to `reviewed/` or back to `pending/`

**Review checklist:**
- [ ] Follows layered architecture — no circular dependencies (CLAUDE.md)
- [ ] No `#ifdef` in non-platform code
- [ ] System IO goes through DIP wrappers (`crypto.h`, `socket.h`)
- [ ] RAII macros used — no manual `free()`
- [ ] Public functions have Doxygen comments
- [ ] No obvious logic errors or security issues
- [ ] Implementation matches the issue description

**On reject:** Add a `## Review Reject` section to the issue file, then move to `pending/`.

### QA

**Trigger:** There are issues in `reviewed/`.

**Responsibility:** Verify the implementation end-to-end: build, tests, coverage,
and deeper security/correctness checks. Move to `verified/` if all checks pass,
or back to `pending/` with documented reasons if not.

**Input:** Issue from `reviewed/` + full test suite
**Output:** Issue moved to `verified/` or back to `pending/`

**QA checklist:**
- [ ] `./manage.sh test` — build and test suite green (0 ASAN errors)
- [ ] `./manage.sh valgrind` — 0 leaks, 0 errors
- [ ] `./manage.sh coverage` — core+infra >90%
- [ ] Edge cases and error paths have tests
- [ ] No regressions in existing tests
- [ ] Security review: no injection, no UB, no uninitialized data

**On reject:** Add a `## QA Reject` section to the issue file, then move to `pending/`.

### Manager

**Trigger:** Periodically, or when the pipeline appears stalled.

**Responsibility:** Ensure overall progress. Identify stalled issues, gaps in
coverage, and conflicts between team members. Escalate or reassign as needed.
Does not evaluate technical content — coordinates flow only.

---

## Issue File Format

```markdown
# <Title> [optional ✅ if verified]

## Description
What needs to be done and why.

## Steps
1. Implementation steps

## API
Relevant Telegram API methods (if applicable).

## Estimate
Approximate scope in lines of code.

## Dependencies
List of prerequisite issues (e.g., P3-01).

## Review Reject — <date> (if applicable)
- Rejection reasons

## QA Reject — <date> (if applicable)
- Rejection reasons
```

## File Naming Convention

```
P<phase>-<sequence>-<short-name>.md
```

Examples: `P1-aes-ige.md`, `P3-02-auth-send-code.md`

Phase number determines priority ordering. Sequence number is optional
(used when a phase has multiple issues with dependencies).
