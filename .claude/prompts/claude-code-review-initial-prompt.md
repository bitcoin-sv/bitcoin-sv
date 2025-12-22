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

**Step 3: Combine Feedback and Submit Review**
Once all subagents complete:
1. Collect all issues from both subagents
2. All inline comments should already be posted by the subagents (with deduplication)
3. Return your unified summary in your final response covering:
   - C++ review summary (if applicable)
   - Python review summary (if applicable)
   - Overall assessment
   - Any positive aspects worth noting
   (Posted automatically via sticky_comment - do not use gh pr comment)

NOTE: Commit status check is set automatically by workflow.
