**Step 1: Analyze Changed Files**
Use `gh pr diff --name-only $PR_NUMBER` to get the list of changed files.

**IMPORTANT FOR SUBAGENTS:**
Subagents inherit these tools: Read, Grep, Glob, Task, and limited gh commands.
- Use `Read` tool to examine files (NOT cat or bash redirects)
- Use `Grep` tool to search code (NOT grep command)
- Do NOT use shell operators: `>`, `&&`, `|`, `--jq`

**Step 2: Launch Specialized Subagents in Parallel**
Based on file extensions, launch appropriate subagents:

**For C++/C files** (.cpp, .h, .c, .hpp, .cc, .cxx, .hxx):
Launch cpp-pro subagent with the prompt from `.claude/prompts/claude-code-review-cpp-instructions.md`.

**For Python files** (.py):
Launch general-purpose subagent with the prompt from `.claude/prompts/claude-code-review-python-instructions.md`.

**Step 3: Parse Subagent Issues**
Once all subagents complete:
1. Parse each subagent's response to extract all ISSUE_START...ISSUE_END blocks
2. Build a list of all issues with their structured data:
   - file, start_line, end_line, severity, title, body

**Step 4: Fetch Existing Comments for Deduplication**
Before posting any comments, check for duplicates:

1. Run: `gh api /repos/$REPO/pulls/$PR_NUMBER/comments`
2. Parse JSON to find existing comments where user.login contains "claude"
3. Extract: path, line (or start_line), body (first 200 chars)
4. Build lookup map: "file:line" → comment body snippet

**Step 5: Post Inline Comments**
For each issue from subagents:

1. Check deduplication:
   - Build key: `"{file}:{end_line}"` (use end_line for lookup)
   - If key exists in lookup map, compare issue descriptions:
     * Extract key technical terms from both
     * If 60%+ term overlap → SKIP (duplicate)
     * If <60% overlap → POST (different issue at same location)
   - If key doesn't exist → POST (new issue)

2. For non-duplicate issues, use `mcp__github_inline_comment__create_inline_comment`:
   - repo: $REPO
   - pull_number: $PR_NUMBER
   - path: file
   - body: "{title}\n\n{body}"
   - start_line: start_line (if not null)
   - line: end_line (if not null - GitHub calls this "line" for end of range)
   - side: "RIGHT"

**Step 6: Return Unified Summary**
Return your final response covering:
- C++ review summary (if applicable)
- Python review summary (if applicable)
- Overall assessment
- Any positive aspects worth noting
- Total count of issues found and posted

(Posted automatically via sticky_comment - do not use gh pr comment)

NOTE: Commit status check is set automatically by workflow.
