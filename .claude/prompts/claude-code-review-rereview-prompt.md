⚠️  CRITICAL: This is a RE-REVIEW. Your task is to VERIFY previous issues, NOT do a fresh review.

**Step 1: Load Issues to Verify**

Use the `Read` tool to load issues from `/tmp/claude-actionable-issues.json`:
```
Read tool with file_path: /tmp/claude-actionable-issues.json
```

**IMPORTANT:** Do NOT use bash `cat` command - use the `Read` tool instead.

For each issue, note: issue_id, path, line, isResolved, issue description

**Step 2: Verify Each Previous Issue**

For EACH previous issue, you must determine if it was fixed or still persists.

Launch verification subagents (cpp-pro or general-purpose based on
file type) with this prompt template (use the PR NUMBER from
above and the list of issues you fetched):
```
VERIFICATION TASK for PR #$PR_NUMBER

You are verifying whether previous code review issues were fixed.

**Previous issues to verify:**
[List each issue with: file:line, issue description]

**Your task:**
1. Read the CURRENT code at each file:line location
2. For EACH previous issue, determine:
   - FIXED: The code change addresses the issue
     (e.g., null check added, memory leak fixed)
   - PERSISTS: The issue still exists in the current code
3. Return a verification report in this format:
   ```
   Issue ID: <issue_id>
   Location: <path>:<line>
   Status: FIXED | PERSISTS
   Evidence: [Brief explanation of what you found in the code]
   ```

**IMPORTANT:**
- DO NOT post any inline comments yourself
- DO NOT re-post existing comments
- ONLY analyze whether previous issues were addressed
- Check for new issues on NEW lines and report them in structured format

**OUTPUT FORMAT:**
Return verification results AND any new issues in structured format:

For previous issue verification:
```
VERIFICATION_START
issue_id: <issue_id>
path: <file_path>
line: <line_number>
status: FIXED | PERSISTS
evidence: <Brief explanation of what you found in the code>
VERIFICATION_END
```

For new issues found: Read `.claude/prompts/issue-format.md` for the complete ISSUE_START...ISSUE_END format.
```

**Step 3: Resolve/Unresolve Issues Based on Verification**

For each verified issue, take action based on verification result:

**If issue status is FIXED (verified by subagent):**

REQUIRED OUTCOME:
- Only resolve issues that Claude created
- Claude must never resolve issues from other reviewers (CODEOWNERs or developers)

IMPLEMENTATION:
1. Check issue.comments.nodes[0].author.login (first comment author)
   - If NOT "claude" or "github-actions": SKIP (cannot resolve other reviewers' issues)
   - If "claude" or "github-actions": Proceed to resolve using GraphQL
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
1. If issue resolved by non-CODEOWNER, unresolve it (catches bypass)
2. If latest comment from non-CODEOWNER developer, post reply explaining:
   * What evidence shows the issue still exists
   * What specific changes are needed to fix it
   * Why developer's response/fix is insufficient

IMPLEMENTATION:
1. Check issue.resolvedBy.login against CODEOWNER list
   - If CODEOWNER resolved: SKIP (do nothing, dismissal is final)
   - If non-CODEOWNER and isResolved==true: unresolve via GraphQL
2. Check issue.comments.nodes[-1].author.login (latest comment)
   - If from "claude" or "github-actions": SKIP (already responded)
   - If from CODEOWNER: SKIP (await their decision)
   - If from non-CODEOWNER developer: post reply using GraphQL:
     ```bash
     gh api graphql -f query='mutation {
       addPullRequestReviewThreadReply(input: {
         pullRequestReviewThreadId: "<thread_id>",
         body: "<reply_text>"
       }) {
         comment { id }
       }
     }'
     ```

**Step 4: Post New Issues (if any)**

After processing verification results from subagents:

1. Parse any ISSUE_START...ISSUE_END blocks from verification subagent responses
2. These are NEW issues found on NEW lines (not related to previous issues)
3. Use deduplication logic (same as Step 4 in initial review):
   - Fetch existing comments: `gh api /repos/$REPO/pulls/$PR_NUMBER/comments`
   - Build lookup map of existing comments
   - For each new issue, check if it's a duplicate
   - Post non-duplicate issues using `mcp__github_inline_comment__create_inline_comment`

**Step 5: Return Summary**

Return a brief summary covering:
- Total previous issues verified: N fixed, N persists
- New issues posted (count only)
- Short overall re-review assessment covering both positive and negative points

**Do NOT list individual issues or file:line locations in the summary.**
All specific feedback belongs in inline comments only. The summary should be
high-level so reviewers aren't reading the same information twice.

(Posted automatically via sticky_comment - do not use gh pr comment)

NOTE: Commit status check is set automatically by workflow.
