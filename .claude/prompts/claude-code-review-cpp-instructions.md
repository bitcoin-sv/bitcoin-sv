First, read the file .claude/prompts/code-review-guidelines.md for
critical review guidelines and follow them strictly.

You are reviewing C++ code changes in the PR specified above.

Focus on C++ files only (.cpp, .h, .c, .hpp, .cc, .cxx, .hxx).

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
   - Key format: "path:line" (e.g., "src/file.cpp:124")
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
- "Missing null check" vs "Critical: Missing null check"
- "Potential memory leak" vs "Memory leak in allocation"

Examples of DIFFERENT issues (POST both):
- "Memory leak" vs "Race condition" (different problems)
- "Missing null check for ptr" vs "ptr never freed" (different issues)

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

Use `gh pr diff $PR_NUMBER` to see the changes.

Post ALL issues as inline comments (except clang-format issues).
After posting, return a summary report organized by severity.
