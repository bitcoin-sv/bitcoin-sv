#!/bin/bash
# Copyright (c) 2026 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Script-driven re-review of code review issues.
#
# For each issue in /tmp/claude-actionable-issues.json:
#   1. Read source code context around the issue location
#   2. Call Anthropic Messages API for a RESOLVED/UNRESOLVED verdict
#   3. Call resolve-review-issue.sh to update the thread state on GitHub

set -euo pipefail

: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"
: "${ANTHROPIC_API_KEY:?Required}"
: "${GH_TOKEN:?Required}"

readonly resolve_script=".github/workflows/resolve-review-issue.sh"
readonly issues_file="/tmp/claude-actionable-issues.json"
readonly model="claude-sonnet-4-20250514"
readonly max_tokens=512
readonly api_url="https://api.anthropic.com/v1/messages"

readonly owner=${GITHUB_REPOSITORY%/*}
readonly repo=${GITHUB_REPOSITORY#*/}

source .github/workflows/bot_logins.sh

function call_anthropic
{
  local body=$1
  local response=$(curl -s --connect-timeout 10 --max-time 120 \
    -w "\n%{http_code}" \
    "$api_url" \
    -H "content-type: application/json" \
    -H "x-api-key: $ANTHROPIC_API_KEY" \
    -H "anthropic-version: 2023-06-01" \
    -d "$body")

  local http_code=$(tail -1 <<< "$response")
  response=$(sed '$d' <<< "$response")

  local -r success=200
  if [[ $http_code == $success ]]; then
    echo "$response"
    return 0
  fi

  printf "  API error: HTTP %s\n%s\n" "$http_code" "$response" >&2
  return 1
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

  # Build the API request body in a single jq call.
  # jq --arg safely handles JSON-escaping of all user-supplied strings
  # (source code, issue bodies, developer responses) in one pass.
  request_body=$(jq -n \
    --arg model "$model" \
    --argjson max_tokens "$max_tokens" \
    --arg path "$path" \
    --arg line "$line" \
    --arg issue_body "$issue_body" \
    --arg dev_response "$latest_comment" \
    --arg source_code "$source_context" \
    '{
      model: $model,
      max_tokens: $max_tokens,
      messages: [{
        role: "user",
        content: (
          "You are reviewing whether a code review issue has been resolved.\n\n"
          + "ISSUE LOCATION: " + $path + ":" + $line + "\n\n"
          + "ORIGINAL ISSUE:\n" + $issue_body + "\n\n"
          + "DEVELOPER RESPONSE:\n" + $dev_response + "\n\n"
          + "CURRENT SOURCE CODE (around line " + $line + "):\n" + $source_code + "\n\n"
          + "Has this issue been resolved? Look at the current source code carefully.\n"
          + "Respond with exactly one of:\n"
          + "  RESOLVED: <brief reason>\n"
          + "  UNRESOLVED: <brief reason>\n\n"
          + "Your response must start with RESOLVED or UNRESOLVED."
        )
      }]
    }')

  if ! api_response=$(call_anthropic "$request_body"); then
    printf "  SKIP: API call failed\n" >&2
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|API call failed")
    continue
  fi

  # Parse the verdict from the response text
  verdict_text=$(jq -r '.content[0].text // ""' <<< "$api_response")
  if [[ -z $verdict_text ]]; then
    printf "  SKIP: Empty API response\n" >&2
    printf "  Full response: %s\n" "$api_response" >&2
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|Empty API response")
    continue
  fi

  # Extract verdict (first line starting with RESOLVED or UNRESOLVED)
  verdict_line=$(grep -m1 -Ei '^(RESOLVED|UNRESOLVED):' <<< "$verdict_text" || true)
  if [[ -z $verdict_line ]]; then
    printf "  SKIP: Could not parse verdict from response\n" >&2
    printf "  Response: %s\n" "$verdict_text" >&2
    ((++error_count))
    results+=("${thread_id}|ERROR|${path}|${line}|Unparseable response")
    continue
  fi

  verdict=$(echo "$verdict_line" | grep -oEi '^(RESOLVED|UNRESOLVED)' | tr '[:lower:]' '[:upper:]')
  reason=$(echo "$verdict_line" | sed 's/^[^:]*: *//')

  printf "  Verdict: %s -- %s\n" "$verdict" "$reason"

  # Act on the verdict
  if [[ $verdict == "RESOLVED" ]]; then
    if "$resolve_script" resolve "$thread_id" 2>&1; then
      printf "  Thread resolved\n"
      ((++resolved_count))
      results+=("${thread_id}|RESOLVED|${path}|${line}|${reason}")
    else
      printf "  WARNING: resolve-review-issue.sh failed for %s\n" "$thread_id" >&2
      ((++error_count))
      results+=("${thread_id}|ERROR|${path}|${line}|Resolve call failed")
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
