# Issue Workflow

## Statuses

Issues move between directories that represent their current status:

```
incoming/ → pending/ → inprogress/ → ready/ → inqa/ → verified/
    │          ↑                                  │
    ↓          └──────────── (QA reject) ──────────┘
 blocked/
```

| Directory     | Meaning                                                        |
|---------------|----------------------------------------------------------------|
| `incoming/`   | Newly created issue, awaiting Engineer feasibility analysis    |
| `pending/`    | Feasible, approved for development, waiting to be picked up    |
| `blocked/`    | Not yet feasible — missing dependencies or prerequisites       |
| `inprogress/` | Actively being worked on by a Developer                        |
| `ready/`      | Development complete, waiting for QA                           |
| `inqa/`       | QA review in progress                                          |
| `verified/`   | QA passed, issue closed                                        |

## Roles

- **Manager** (Claude): Ensures progress, coordinates team, finds gaps and conflicts
- **Engineer** (Claude): Analyzes issues and code together, decides feasibility
- **Developer** (Claude): Implements features, fixes bugs
- **QA** (Claude): Reviews code quality, runs checks, verifies acceptance

## Transitions

### incoming/ → pending/ (Engineer approves)
**Trigger:** Engineer analyzes the issue against the current codebase and determines
it is feasible — all dependencies are met.

### incoming/ → blocked/ (Engineer rejects)
**Trigger:** Engineer determines the issue is not yet feasible — dependencies are
missing or prerequisites are unmet.
**Requirements:**
- Add a `## Blocked` section to the issue file documenting:
  - Date
  - What is missing or blocking progress

Example:
```markdown
## Blocked — 2026-03-31
- Depends on P2-auth-key-exchange which is not yet in verified/
```

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

## Blocked — <date> (if applicable)
- Blocking reasons

## QA Reject — <date> (if applicable)
- Rejection reasons
```
