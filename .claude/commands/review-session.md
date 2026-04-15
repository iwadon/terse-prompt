---
description: Review recent work and identify next priorities
argument-hint: <optional: review target in natural language>
---

Review recent work and identify next priorities.

## Target Specification

$ARGUMENTS

**Examples:**
- (no argument) → Review the most recent unit of work (related commit group)
- `last 3 commits` → Review recent 3 commits
- `today's work` → All commits from today
- Any other keyword → Search and review related commits

## Procedure

### 1. Identify Target

- Run `git log --oneline -20` to see recent history
- Identify the most recent "unit of work" — a group of related commits
- Use commit messages and timestamps to determine where the current work begins

### 2. Gather Information

- `git show` for each commit in the unit
- `git diff` for uncommitted changes
- `git status` for current state
- Read relevant files to verify code state

### 3. Documentation Check

Read `docs/implementation-status.md` and check:
- Did the work resolve any known limitations? → Remove from the document
- Did the work introduce new limitations? → Add to the document
- Were any items scoped out? → Add to roadmap or limitations as appropriate
- Are the remaining tasks still accurate?

If updates are needed, make them and include in the report.

### 4. Report

#### Accomplishments
- Summary of what was done
- Statistics (files changed, lines added/removed, tests)

#### Notes (optional)
- Challenges encountered or design insights
- Deviations from original plan

#### Next Priority Items
- Check `docs/implementation-status.md` for remaining work and roadmap
- Check `docs/requirements.md` for unimplemented features
- Identify TODOs in code if relevant

#### Decisions/Concerns (optional)
- Architectural decisions made
- Technical debt introduced
- Scope-out items (record in `docs/implementation-status.md` if any)

## Notes

- Verify code state by reading files, not just commit messages
- Run `cmake --build build && ctest --test-dir build` to confirm current build/test status
- Focus on the identified work unit, not entire session history
