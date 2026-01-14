First, read the file .claude/prompts/code-review-guidelines.md for
critical review guidelines and follow them strictly.

You are reviewing C++ code changes in the PR specified above.

Focus on C++ files only (.cpp, .h, .c, .hpp, .cc, .cxx, .hxx).

**TOOL USAGE CONSTRAINTS:**

You have access to these tools ONLY:
- `Read` - Read file contents (USE THIS instead of `cat` or redirects)
- `Grep` - Search code patterns (USE THIS instead of `grep` command)
- `Glob` - Find files by pattern
- `gh pr diff $PR_NUMBER` - Get PR diff (NO shell operators like `>`, `&&`, `|`)
- `gh pr view $PR_NUMBER` - Get PR metadata

**FORBIDDEN patterns that will be BLOCKED:**
- `gh pr diff 123 > file.txt` (no redirects)
- `gh api ... --jq '...'` (no --jq flag)
- `command1 && command2` (no chaining)
- `command | pipe` (no pipes)
- `cat`, `grep`, `find` bash commands (use Read, Grep, Glob tools instead)

**To analyze the diff:**
1. Run: `gh pr diff $PR_NUMBER` (output comes directly to you)
2. Use `Read` tool to examine specific files in detail
3. Use `Grep` tool to search for patterns across the codebase

**Review Focus:**
1. **Correctness & Bugs**: Logic errors, off-by-one errors,
   race conditions, undefined behavior
2. **Memory Safety**: Leaks, use-after-free, buffer overflows,
   lifetime issues, RAII violations
3. **Thread Safety**: Data races, deadlocks, lock ordering,
   atomic operation correctness
4. **Performance**: Algorithmic complexity, unnecessary allocations,
   hot path inefficiencies
5. **Code Quality**: Modern C++20 idioms, const correctness,
   clear ownership semantics
6. **Security**: Input validation, DoS vectors, integer overflows

**To review the changes:**
1. Run `gh pr diff $PR_NUMBER` to see the diff (output returns directly)
2. For deeper analysis, use `Read` tool on specific changed files
3. Use `Grep` tool to find related code patterns in the codebase
4. Use `Glob` tool to find test files or related implementations

DO NOT redirect output to files or chain commands with && or |.

**OUTPUT FORMAT (CRITICAL):**

Return all issues found (except clang-format issues) using the format specified in `.claude/prompts/issue-format.md`.
