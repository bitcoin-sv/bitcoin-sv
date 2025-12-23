⚠️  CRITICAL: This is a RE-REVIEW. Your task is to VERIFY previous issues, NOT do a fresh review.

**Step 1: Load Threads to Verify**

Use the `Read` tool to load threads from `/tmp/claude-actionable-threads.json`:
```
Read tool with file_path: /tmp/claude-actionable-threads.json
```

**IMPORTANT:** Do NOT use bash `cat` command - use the `Read` tool instead.

For each thread, note: thread_id, path, line, isResolved, issue description

**Step 2: Verify Each Previous Issue**

For EACH previous thread, you must determine if the issue was fixed or still persists.

Launch verification subagents (cpp-pro or general-purpose based on
file type) with this prompt template (use the PR NUMBER from
above and the list of issues from the threads you fetched):
```
VERIFICATION TASK for PR #$PR_NUMBER

You are verifying whether previous code review issues were fixed.

**Previous issues to verify:**
[List each thread with: file:line, issue description from thread]

**Your task:**
1. Read the CURRENT code at each file:line location
2. For EACH previous issue, determine:
   - FIXED: The code change addresses the issue
     (e.g., null check added, memory leak fixed)
   - PERSISTS: The issue still exists in the current code
3. Return a verification report in this format:
   ```
   Thread ID: <thread_id>
   Location: <path>:<line>
   Status: FIXED | PERSISTS
   Evidence: [Brief explanation of what you found in the code]
   ```

**IMPORTANT:**
- DO NOT post new inline comments
- DO NOT re-post existing comments
- ONLY analyze whether previous issues were addressed
- Check for new issues on NEW lines and report separately
```

**Step 3: Resolve/Unresolve Threads Based on Verification**

For each verified thread, take action based on verification result:

**If issue status is FIXED (verified by subagent):**

REQUIRED OUTCOME:
- Only resolve threads that Claude created
- Claude must never resolve threads from other reviewers (CODEOWNERs or developers)

IMPLEMENTATION:
1. Check thread.comments.nodes[0].author.login (first comment author)
   - If NOT "claude": SKIP (cannot resolve other reviewers' threads)
   - If "claude": Proceed to resolve using GraphQL
2. Resolve via GraphQL mutation:
   ```bash
   gh api graphql -f query='mutation {
     resolveReviewThread(input: {threadId: "<thread_id>"}) {
       thread { id }
     }
   }'
   ```
3. NO comment needed (resolution itself indicates Claude verified the fix)

**If issue status is PERSISTS (verified by subagent):**

REQUIRED OUTCOME:
1. If thread resolved by non-CODEOWNER, unresolve it (catches bypass)
2. If latest comment from non-CODEOWNER developer, post reply explaining:
   * What evidence shows the issue still exists
   * What specific changes are needed to fix it
   * Why developer's response/fix is insufficient

IMPLEMENTATION:
1. Check thread.resolvedBy.login against CODEOWNER list
   - If CODEOWNER resolved: SKIP (do nothing, dismissal is final)
   - If non-CODEOWNER and isResolved==true: unresolve via GraphQL
2. Check thread.comments.nodes[-1].author.login (latest comment)
   - If from "claude": SKIP (already responded)
   - If from CODEOWNER: SKIP (await their decision)
   - If from non-CODEOWNER developer: post reply via gh api POST

**Step 4: Check for New Issues on New Lines**

After verifying previous threads:
- Scan the diff for any NEW lines that were NOT previously commented
- If you find NEW issues on NEW lines (not near previous comments), post NEW inline comments
- Before posting, check existing comments to avoid duplicates

**Step 5: Return Summary**

Return a summary covering:
- Total previous threads verified
- How many were FIXED vs PERSISTS
- Any new issues found on new lines
- Overall re-review assessment

(Posted automatically via sticky_comment - do not use gh pr comment)

NOTE: Commit status check is set automatically by workflow.
