# Issue Report Format

When reporting code review issues, use this structured format so the coordinator agent can parse and post them as inline comments.

```
ISSUE_START
file: <relative path from repo root>
start_line: <line number>
end_line: <line number>
severity: <Critical|Important|Suggestion>
title: <Brief one-line description>
body: <Detailed explanation with specific file:line references>
ISSUE_END
```

**Line number rules (inclusive range):**
- Single-line: `start_line: 307, end_line: 307`
- Multi-line: `start_line: 85, end_line: 93`
- File-level: `start_line: null, end_line: null`

**Severity levels:**
- `Critical`: Memory safety, correctness bugs, security issues
- `Important`: Performance problems, maintainability issues
- `Suggestion`: Style improvements, optimizations

## Examples

Single-line:
```
ISSUE_START
file: src/dbwrapper.h
start_line: 307
end_line: 307
severity: Important
title: Narrowing conversion in GetKeySize()
body: The method returns `unsigned int` but the size is `size_t`, which can truncate values for large keys on 64-bit systems. Consider returning `size_t` instead.
ISSUE_END
```

Multi-line:
```
ISSUE_START
file: src/dbtraits.h
start_line: 85
end_line: 93
severity: Critical
title: Buffer overflow in CBitcoinLevelDBLogger::Logv()
body: Lines 85-93 contain multiple buffer safety issues. The code accesses `p[-1]` at line 88 when `p == base`, causing out-of-bounds access. Line 89 writes to `*p++` even when `p == limit`, causing buffer overflow.
ISSUE_END
```
