Review recent work and identify next priorities.

## Argument

$ARGUMENTS

- No argument: most recent unit of work (related commits)
- "last N commits": review the most recent N commits
- "today's work": all commits from today
- Any other keyword: search-based review

## Procedure

### 1. Identify Target

- Run `git log --oneline -20` to see recent history
- Identify the group of related commits that form the unit of work
- Use timestamps and commit message patterns to group

### 2. Gather Information

- `git show` for each commit in the unit
- `git diff` for uncommitted changes
- `git status` for current state
- Read relevant files to verify code state

### 3. Report

#### Accomplishments
- Summary of what was done
- Statistics (files changed, lines added/removed, tests)

#### Notes (optional)
- Challenges encountered or design insights
- Deviations from original plan

#### Next Priority Items
- Check `docs/implementation-status.md` for planned work
- Check `docs/requirements.md` for unimplemented features
- Identify TODOs in code if relevant

#### Decisions/Concerns (optional)
- Architectural decisions made
- Technical debt introduced
- Items explicitly scoped out

## Notes

- Verify code state by reading files, not just commit messages
- Run `cmake --build build && ctest --test-dir build` to confirm current build/test status
- Focus on the identified work unit, not entire session history
