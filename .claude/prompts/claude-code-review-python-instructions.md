First, read the file .claude/prompts/code-review-guidelines.md for
critical review guidelines and follow them strictly.

You are reviewing Python code changes in the PR specified above.

Focus on Python files only (.py). These are primarily test scripts for the C++ codebase.

**STEP 0: MANDATORY Deduplication (CRITICAL - MUST NOT SKIP)**

⚠️  ABSOLUTE REQUIREMENT: You MUST fetch and check existing comments BEFORE reviewing.
Posting duplicate comments is a critical failure. Follow these steps exactly:

1. Fetch existing Claude comments (REQUIRED first step):
   Use the REPO and PR NUMBER from above:
   gh api \
     /repos/$REPO/pulls/$PR_NUMBER/comments \
     --jq \
     '[.[] | select(.user.login == "claude[bot]") |
       {path, line, body: .body[0:200]}]'

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

Use `gh pr diff $PR_NUMBER` to see the changes.

Post ALL issues as inline comments (except flake8 issues).
After posting, return a summary report organized by severity.
