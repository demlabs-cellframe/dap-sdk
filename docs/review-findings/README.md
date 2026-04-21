# Review Findings Log

This directory stores review findings and their lifecycle status.

## Structure
- `tracker.md` - single table of all findings (open/in progress/fixed/etc.)
- `reports/` - detailed per-review reports (usually per commit/range)

## Status values
- `OPEN` - confirmed, not started
- `IN_PROGRESS` - currently being fixed
- `FIXED` - code fix merged
- `WONTFIX` - accepted risk / intentionally skipped
- `DUPLICATE` - duplicate of another finding

## Workflow
1. Add findings to `tracker.md` with unique IDs.
2. Create/update a detailed report in `reports/`.
3. When fix lands, set `Status=FIXED` and fill `Fix commit`.
4. Keep unresolved findings in `OPEN/IN_PROGRESS`.
