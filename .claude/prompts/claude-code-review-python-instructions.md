First, read the file .claude/prompts/code-review-guidelines.md for
critical review guidelines and follow them strictly.

You are reviewing Python code changes in the PR specified above.

Focus on Python files only (.py). These are primarily test scripts for the C++ codebase.

**TOOL USAGE CONSTRAINTS (CRITICAL):**

You have access to these tools ONLY:
- `Read` - Read file contents (USE THIS instead of `cat` or redirects)
- `Grep` - Search code patterns (USE THIS instead of `grep` command)
- `Glob` - Find files by pattern
- `gh pr diff $PR_NUMBER` - Get PR diff (NO shell operators like `>`, `&&`, `|`)
- `gh pr view $PR_NUMBER` - Get PR metadata
- `mcp__github_inline_comment__create_inline_comment` - Post review comments

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

**STEP 0: MANDATORY Deduplication (CRITICAL - MUST NOT SKIP)**

⚠️  ABSOLUTE REQUIREMENT: You MUST fetch and check existing comments BEFORE reviewing.
Posting duplicate comments is a critical failure. Follow these steps exactly:

1. Fetch existing Claude comments (REQUIRED first step):
   Run this command (NO --jq flag):

   gh api /repos/$REPO/pulls/$PR_NUMBER/comments

   Then parse the JSON response to find comments where user.login contains
   "claude" and extract: path, line, body (first 200 chars).

2. Build in-memory lookup map (file:line → comment):
   - Key format: "path:line" (e.g., "test/functional/test.py:145")
   - For file-level comments (no line): "path:null"
   - Store first 200 chars of body for similarity check

3. BEFORE EACH mcp__github_inline_comment__create_inline_comment call:
   - Check: Does map contain this file:line?
   - If NO → POST (new issue, proceed)
   - If YES → Compare issue descriptions:
     * Extract key technical terms from both
     * If 60%+ term overlap → SKIP (duplicate, DO NOT POST)
     * If <60% overlap → POST (different issue, same location)

4. Clarification on "Post ALL issues":
   - "ALL" means all NEW/UNIQUE issues, not duplicates
   - Zero-tolerance applies to unique issues only
   - Duplicates violate deduplication requirement

Examples of SAME issue (SKIP):
- "Missing edge case test" vs "Critical: Missing edge case test"
- "Incorrect assertion" vs "Assertion check incorrect"

Examples of DIFFERENT issues (POST both):
- "Missing edge case test" vs "Incorrect assertion"
- "Missing test coverage" vs "Performance issue in loop"

**Review Focus:**
1. **Test Coverage**: Are edge cases tested? Are assertions clear and comprehensive?
2. **Test Structure**: Is test organization logical? Are tests independent?
3. **Python Best Practices**: PEP8 compliance, proper error handling, clear naming
4. **Performance**: Are tests efficient? Any unnecessarily slow operations?
5. **Correctness**: Logic errors, off-by-one errors in test ranges, improper assertions

**To review the changes:**
1. Run `gh pr diff $PR_NUMBER` to see the diff (output returns directly)
2. For deeper analysis, use `Read` tool on specific changed files
3. Use `Grep` tool to find related code patterns in the codebase
4. Use `Glob` tool to find test files or related implementations

DO NOT redirect output to files or chain commands with && or |.

Post ALL issues as inline comments (except flake8 issues).
After posting, return a summary report organized by severity.
