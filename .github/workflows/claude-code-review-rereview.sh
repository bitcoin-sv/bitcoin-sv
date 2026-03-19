#!/bin/bash
# Copyright (c) 2026 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Script-driven re-review of code review issues.
#
# For each issue in /tmp/claude-actionable-issues.json:
#   1. Read source code context around the issue location
#   2. Call Claude Code CLI for a RESOLVED/UNRESOLVED verdict
#   3. Call resolve-review-issue.sh to update the thread state on GitHub

set -euo pipefail

: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"
: "${ANTHROPIC_API_KEY:?Required}"
: "${GH_TOKEN:?Required}"

readonly resolve_script=".github/workflows/resolve-review-issue.sh"
readonly issues_file="/tmp/claude-actionable-issues.json"
readonly model="claude-sonnet-4-6"

readonly owner=${GITHUB_REPOSITORY%/*}
readonly repo=${GITHUB_REPOSITORY#*/}

source .github/workflows/bot_logins.sh

# Map file extension to review agent name.
# Returns empty string for unsupported file types (no agent).
function agent_for_file
{
  local file=$1
  case "${file##*.}" in
    cpp|h|c|hpp|cc|cxx|hxx) echo "cpp-pro" ;;
    py)                      echo "python-pro" ;;
    *)                       echo "" ;;
  esac
}

# Install Claude Code CLI
if ! command -v claude &>/dev/null; then
  printf "Installing Claude Code CLI...\n"
  npm install -g @anthropic-ai/claude-code 2>&1
  printf "Claude Code CLI installed.\n\n"
fi

function call_claude
{
  local prompt=$1
  local agent=${2:-}

  local -a cli_args=(-p --model "$model")
  if [[ -n $agent ]]; then
    cli_args+=(--agent "$agent")
  fi

  local response stderr_output
  stderr_output=$(mktemp)
  if ! response=$(printf '%s' "$prompt" | claude "${cli_args[@]}" \
    2>"$stderr_output"); then
    printf "  Claude CLI exited with error:\n" >&2
    cat "$stderr_output" >&2
    rm -f "$stderr_output"
    return 1
  fi
  rm -f "$stderr_output"

  if [[ -z $response ]]; then
    printf "  Empty response from Claude CLI\n" >&2
    return 1
  fi

  echo "$response"
}

function get_source_context
{
  local file=$1
  local line=$2

  local -r context_lines=50
  local start=$((line - context_lines))
  ((start < 1)) && start=1
  local end=$((line + context_lines))

  sed -n "${start},${end}p" "$file"
}

if [[ ! -f $issues_file ]]; then
  printf "FATAL: %s not found\n" "$issues_file" >&2
  exit 1
fi

issue_count=$(jq 'length' "$issues_file")
if ((issue_count == 0)); then
  printf "FATAL: %s is empty\n" "$issues_file" >&2
  exit 1
fi

printf "Re-reviewing %d issue(s)...\n\n" "$issue_count"

resolved_count=0
unresolved_count=0
error_count=0
results=()  # array of "ISSUE_ID|VERDICT|PATH|LINE|REASON"

while IFS= read -r issue; do
  # Extract fields from the GraphQL review thread JSON
  thread_id=$(jq -r '.id' <<< "$issue")
  is_resolved=$(jq -r '.isResolved' <<< "$issue")
  path=$(jq -r '.comments.nodes[0].path // "unknown"' <<< "$issue")
  line=$(jq -r '.comments.nodes[0].originalLine // 0' <<< "$issue")
  issue_body=$(jq -r '.comments.nodes[0].body // ""' <<< "$issue")
  latest_author=$(jq -r '.comments.nodes[-1].author.login // "unknown"' <<< "$issue")
  latest_comment=$(jq -r '.comments.nodes[-1].body // ""' <<< "$issue")

  printf "Issue: %s:%s (thread %s)\n" "$path" "$line" "$thread_id"

  # File deleted/renamed since initial review — issue is moot
  if [[ ! -f $path ]]; then
    printf "  File no longer exists, auto-resolving\n"
    "$resolve_script" resolve "$thread_id" 2>&1 || true
    ((++resolved_count))
    results+=("${thread_id}|RESOLVED|${path}|${line}|File no longer exists")
    printf "\n"
    continue
  fi

  source_context=$(get_source_context "$path" "$line")

  # Build the prompt as plain text
  prompt="You are reviewing whether a code review issue has been resolved.

ISSUE LOCATION: ${path}:${line}

ORIGINAL ISSUE:
${issue_body}

DEVELOPER RESPONSE:
${latest_comment}

CURRENT SOURCE CODE (around line ${line}):
${source_context}

Evaluate whether this issue should remain open. Consider THREE possibilities:

1. The code was changed to address the issue.
2. The developer's response demonstrates the original issue was incorrect
   or inapplicable (e.g. the suggestion was wrong, based on a misunderstanding,
   or the existing code is actually correct).
3. The issue remains valid and unaddressed.

Be willing to concede if the developer makes a sound technical argument.
The original review can be wrong.

Respond with exactly one of:
  RESOLVED: <brief reason why the code fix addresses the issue>
  WITHDRAWN: <brief reason why the original issue was incorrect>
  UNRESOLVED: <brief reason why the issue still needs attention>

Your response must start with RESOLVED, WITHDRAWN, or UNRESOLVED."

  agent=$(agent_for_file "$path")
  if [[ -n $agent ]]; then
    printf "  Using agent: %s\n" "$agent"
  fi

  if ! verdict_text=$(call_claude "$prompt" "$agent"); then
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|Claude CLI call failed")
    continue
  fi

  # Extract verdict (first line starting with RESOLVED, WITHDRAWN, or UNRESOLVED)
  verdict_line=$(grep -m1 -Ei '^(RESOLVED|WITHDRAWN|UNRESOLVED):' <<< "$verdict_text" || true)
  if [[ -z $verdict_line ]]; then
    printf "  SKIP: Could not parse verdict from response\n" >&2
    printf "  Response: %s\n" "$verdict_text" >&2
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|Unparseable response")
    continue
  fi

  verdict=$(echo "$verdict_line" | grep -oEi '^(RESOLVED|WITHDRAWN|UNRESOLVED)' | tr '[:lower:]' '[:upper:]')
  reason=$(echo "$verdict_line" | sed 's/^[^:]*: *//')

  printf "  Verdict: %s -- %s\n" "$verdict" "$reason"

  # Act on the verdict
  if [[ $verdict == "RESOLVED" || $verdict == "WITHDRAWN" ]]; then
    failed=false

    # Post concession reply for withdrawn issues
    if [[ $verdict == "WITHDRAWN" ]]; then
      reply_body="Withdrawn: ${reason}"
      if "$resolve_script" reply "$thread_id" "$reply_body" 2>&1; then
        printf "  Withdrawal reply posted\n"
      else
        printf "  WARNING: reply failed for %s\n" "$thread_id" >&2
        failed=true
      fi
    fi

    if [[ $failed == "false" ]] && "$resolve_script" resolve "$thread_id" 2>&1; then
      printf "  Thread resolved (%s)\n" "$verdict"
      ((++resolved_count))
      results+=("${thread_id}|${verdict}|${path}|${line}|${reason}")
    elif [[ $failed == "false" ]]; then
      printf "  WARNING: resolve-review-issue.sh failed for %s\n" "$thread_id" >&2
      ((++error_count))
      results+=("${thread_id}|ERROR|${path}|${line}|Resolve call failed")
    else
      ((++error_count))
      results+=("${thread_id}|ERROR|${path}|${line}|Reply/resolve call failed")
    fi

  elif [[ $verdict == "UNRESOLVED" ]]; then
    failed=false

    # If thread was prematurely resolved, unresolve it
    if [[ $is_resolved == "true" ]]; then
      if "$resolve_script" unresolve "$thread_id" 2>&1; then
        printf "  Thread unresolved (was prematurely resolved)\n"
      else
        printf "  WARNING: unresolve failed for %s\n" "$thread_id" >&2
        failed=true
      fi
    fi

    # If developer commented, reply with the verdict
    if ! is_bot "$latest_author"; then
      reply_body="Issue persists: ${reason}"
      if "$resolve_script" reply "$thread_id" "$reply_body" 2>&1; then
        printf "  Reply posted\n"
      else
        printf "  WARNING: reply failed for %s\n" "$thread_id" >&2
        failed=true
      fi
    fi

    if [[ $failed == "true" ]]; then
      ((++error_count))
      results+=("${thread_id}|ERROR|${path}|${line}|GitHub API call failed")
    else
      ((++unresolved_count))
      results+=("${thread_id}|UNRESOLVED|${path}|${line}|${reason}")
    fi

  else
    printf "  SKIP: unexpected verdict '%s'\n" "$verdict" >&2
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|Unexpected verdict")
  fi

  printf "\n"

done < <(jq -c '.[]' "$issues_file")

printf "Re-review complete: %d resolved, %d unresolved, %d errors\n" \
  "$resolved_count" "$unresolved_count" "$error_count"

if ((error_count > 0)); then
  printf "FATAL: %d re-review(s) failed due to API or processing errors.\n" "$error_count" >&2
  exit 1
fi

# Post/update summary comment on the PR

# Build results table
results_table="| File | Line | Verdict | Reason |\n|------|------|---------|--------|\n"
for r in "${results[@]}"; do
  IFS='|' read -r _id verdict rpath rline rreason <<< "$r"
  results_table+="| \`${rpath}\` | ${rline} | ${verdict} | ${rreason} |\n"
done

summary_body="## Re-review Results

**${resolved_count} resolved** | **${unresolved_count} unresolved** | **${error_count} errors**

<details>
<summary>Details</summary>

$(printf "%b" "$results_table")

</details>

---
*Automated re-review via [claude-code-review-rereview.sh](https://github.com/${GITHUB_REPOSITORY}/blob/main/.github/workflows/claude-code-review-rereview.sh)*"

# Find existing re-review summary comment to update (or create new)
existing_comment_id=$(gh api "/repos/${owner}/${repo}/issues/${PR_NUMBER}/comments" \
  --jq '[.[] | select(
    (.user.login == "github-actions[bot]" or .user.login == "claude[bot]")
    and (.body | contains("Re-review Results"))
  ) | .id] | last // empty')

if [[ -n $existing_comment_id ]]; then
  printf "Updating existing summary comment %s...\n" "$existing_comment_id"
  gh api "/repos/${owner}/${repo}/issues/comments/${existing_comment_id}" \
    -X PATCH -f body="$summary_body" > /dev/null
else
  printf "Posting new summary comment...\n"
  gh api "/repos/${owner}/${repo}/issues/${PR_NUMBER}/comments" \
    -f body="$summary_body" > /dev/null
fi

printf "Summary comment posted.\n"

# Output outstanding count for workflow to gate subsequent steps
outstanding=$((unresolved_count + error_count))
printf "outstanding_count=%d\n" "$outstanding" >> "$GITHUB_OUTPUT"
