#!/bin/bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Classify bot review issues and determine review mode.
#
# Outputs (stdout, key=value):
#   mode=review    — no actionable issues (full review)
#   mode=rereview   — issues with developer replies needing verification
#
# Diagnostics go to stderr.
# Exits non-zero if any issue is unresolved with no developer response.
#
# Issue classification:
#   1. CODEOWNER resolved           → skip (dismissal is final)
#   2. Latest comment is bot        → unresolved, fail immediately
#   3. Latest comment is anyone else → needs verification by Claude

set -euo pipefail

: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"

readonly owner=${GITHUB_REPOSITORY%/*}
readonly repo=${GITHUB_REPOSITORY#*/}

source .github/workflows/bot_logins.sh

# Fetch all review issues
all_issues_json=$(gh api graphql \
  -f owner="$owner" \
  -f repo="$repo" \
  -F pr_number="$PR_NUMBER" \
  -f query='
query($owner: String!, $repo: String!, $pr_number: Int!) {
  repository(owner: $owner, name: $repo) {
    pullRequest(number: $pr_number) {
      reviewThreads(first: 100) {
        nodes {
          id
          isResolved
          resolvedBy { login }
          comments(first: 10) {
            nodes {
              id
              author { login }
              body
              path
              originalLine
              createdAt
            }
          }
        }
      }
    }
  }
}' -q '.data.repository.pullRequest.reviewThreads.nodes')

total_count=$(jq 'length' <<< "$all_issues_json")

# Filter to issues started by the review bot
bot_logins_json=$(printf '%s\n' "${bot_logins[@]}" \
  | jq -R . | jq -sc .)
issues_json=$(jq -c --argjson bots "$bot_logins_json" '
  [.[] | select(
    .comments.nodes[0].author.login as $a
    | $bots | index($a)
  )]' <<< "$all_issues_json")
issue_count=$(jq 'length' <<< "$issues_json")

printf "PR #%s: %d review issues (%d from bot)\n" \
  "$PR_NUMBER" "$total_count" "$issue_count" >&2

# No bot issues → initial review
if ((issue_count == 0)); then
  printf "blocking_count=0\n"
  printf "mode=review\n"
  exit 0
fi

source .github/workflows/code_owners.sh

needs_verification="[]"
resolved_count=0
raised_count=0
disputed_count=0
addressed_count=0
issues=()

while IFS= read -r issue; do
  IFS=$'\t' read -r resolved_by is_resolved comment_count \
    latest_author path line < <(
    jq -r '[
      (.resolvedBy.login // "null"),
      .isResolved,
      (.comments.nodes | length),
      (.comments.nodes[-1].author.login // "null"),
      (.comments.nodes[0].path // "unknown"),
      (.comments.nodes[0].originalLine // "?")
    ] | join("\t")' <<< "$issue"
  )
  if ((comment_count == 0)); then
    continue
  fi

  # Rule 1: Resolved → skip
  if [[ $is_resolved == "true" ]]; then
    ((++resolved_count))
    issues+=("$(printf "%s:%s resolved" "$path" "$line")")
    continue
  fi

  # Rule 2: Latest comment is bot
  if is_bot "$latest_author"; then
    if ((comment_count == 1)); then
      ((++raised_count))
      issues+=("$(printf "%s:%s ** RAISED **" "$path" "$line")")
    else
      ((++disputed_count))
      issues+=("$(printf "%s:%s ** DISPUTED **" "$path" "$line")")
    fi
    continue
  fi

  # Rule 3: Latest comment is anyone else → needs re-review
  ((++addressed_count))
  issues+=("$(printf "%s:%s needs AI re-review" "$path" "$line")")
  needs_verification=$(jq --argjson issue "$issue" \
    '. += [$issue]' <<< "$needs_verification")

done < <(jq -c '.[]' <<< "$issues_json")

printf "%s\n" "${issues[@]}" | sort >&2

printf "Summary: %d raised, %d addressed, %d disputed, %d resolved\n" \
  "$raised_count" "$addressed_count" "$disputed_count" "$resolved_count" >&2

# Output existing bot issue locations for full review deduplication
existing_locations=$(jq -r '
  .[] | (.comments.nodes[0].path // "unknown")
      + ":" + ((.comments.nodes[0].originalLine // 0) | tostring)
' <<< "$issues_json" | sort -u)
printf "existing_issues<<EOF\n"
printf "%s\n" "$existing_locations"
printf "EOF\n"

# Output blocking count for workflow to gate full review
blocking_count=$((raised_count + disputed_count))
printf "blocking_count=%d\n" "$blocking_count"

if ((addressed_count > 0)); then
  printf "%s" "$needs_verification" > /tmp/claude-actionable-issues.json
  printf "mode=rereview\nactionable_count=%d\ntotal_count=%d\n" \
    "$addressed_count" "$issue_count"
elif ((blocking_count > 0)); then
  printf "mode=unaddressed_issues_only\n"
else
  printf "mode=review\n"
fi
