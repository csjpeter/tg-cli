# Issue Workflow

## Statuses

Issues move between directories that represent their current status:

```
new/ → pending/ → inprogress/ → ready/ → inqa/ → verified/
        ↑                                   │
        └──────────── (QA reject) ──────────┘
```

| Directory     | Meaning                                              |
|---------------|------------------------------------------------------|
| `new/`        | Newly created issue, awaiting manager approval       |
| `pending/`    | Approved for development, waiting to be picked up    |
| `inprogress/` | Actively being worked on by a developer              |
| `ready/`      | Development complete, waiting for QA                 |
| `inqa/`       | QA review in progress                                |
| `verified/`   | QA passed, issue closed                              |

## Roles

- **Manager** (human user): Approves issues, prioritizes work
- **Developer** (Claude): Implements features, fixes bugs
- **QA** (Claude): Reviews code quality, runs checks, verifies acceptance

## Transitions

### new/ → pending/ (manager approval)
**Trigger:** Manager reviews and approves the issue for development.
Only the manager (human user) can authorize this move.

### pending/ → inprogress/ (developer picks up)
**Trigger:** Developer picks up the issue.
**Rules:**
- All dependencies listed in the issue must already be in `verified/`
- Only one issue should be `inprogress/` at a time
- No file content changes required — just move the file

### inprogress/ → ready/ (development complete)
**Trigger:** Developer considers implementation complete.
**Requirements before move:**
- Code compiles: `./manage.sh build` (0 warnings)
- Tests pass: `./manage.sh test` (0 ASAN errors)
- Valgrind clean: `./manage.sh valgrind` (0 leaks)
- Coverage maintained: `./manage.sh coverage` (core+infra >90%)
- All new/changed files committed

### ready/ → inqa/ (QA picks up)
**Trigger:** QA picks up the issue for review.
No file content changes required — just move the file.

### inqa/ → verified/ (QA passed)
**Trigger:** All QA checks passed.
**QA checklist:**
- [ ] Build & test suite green (`./manage.sh test`)
- [ ] Valgrind clean (`./manage.sh valgrind`)
- [ ] Coverage target met (`./manage.sh coverage`)
- [ ] Code review: no security issues, follows architecture rules (CLAUDE.md)
- [ ] Public functions have Doxygen comments
- [ ] No `#ifdef` in non-platform code
- [ ] System IO goes through DIP wrappers (crypto.h, socket.h)
- [ ] RAII macros used for resource management (no manual free)
- [ ] Edge cases and error paths tested
- [ ] No regressions in existing tests

### inqa/ → pending/ (QA reject)
**Trigger:** QA review failed.
**Requirements:**
- Add a `## QA Reject` section to the issue file documenting:
  - Date
  - What failed
  - What needs to be fixed

Example:
```markdown
## QA Reject — 2026-03-31
- Missing NULL check in `foo_init()` (crash on OOM)
- No unit test for error path in `bar_send()`
```

The issue returns to `pending/` for the developer to fix.

## File Naming Convention

```
P<phase>-<sequence>-<short-name>.md
```

Examples: `P1-aes-ige.md`, `P3-02-auth-send-code.md`

Phase number determines priority ordering. Sequence number is optional
(used when a phase has multiple issues with dependencies).

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

## QA Reject — <date> (if applicable)
- Rejection reasons
```
