#!/bin/bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Classify bot review threads and determine review mode.
#
# Outputs (stdout, key=value):
#   mode=initial    — no bot threads found (first review)
#   mode=rereview   — threads with developer replies needing verification
#
# Diagnostics go to stderr.
# Exits non-zero if any thread is unresolved with no developer response.
#
# Thread classification:
#   1. CODEOWNER resolved           → skip (dismissal is final)
#   2. Latest comment is bot        → unresolved, fail immediately
#   3. Latest comment is anyone else → needs verification by Claude

set -euo pipefail

: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"

readonly owner=${GITHUB_REPOSITORY%/*}
readonly repo=${GITHUB_REPOSITORY#*/}

# Bot logins that the code review action may post as:
#   "claude"          - when using the action's built-in GitHub App token
#   "github-actions"  - when using the workflow's GITHUB_TOKEN
readonly bot_logins=("claude" "github-actions")

function is_bot
{
  local user=$1
  for bot in "${bot_logins[@]}"; do
    if [[ $user == "$bot" ]]; then
      return 0
    fi
  done
  return 1
}

# Fetch all review threads
all_threads_json=$(gh api graphql -f query="
query {
  repository(owner: \"$owner\", name: \"$repo\") {
    pullRequest(number: $PR_NUMBER) {
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
}" -q '.data.repository.pullRequest.reviewThreads.nodes')

# Filter to threads started by the review bot
bot_logins_json=$(printf '%s\n' "${bot_logins[@]}" \
  | jq -R . | jq -sc .)
threads_json=$(jq -c --argjson bots "$bot_logins_json" '
  [.[] | select(
    .comments.nodes[0].author.login as $a
    | $bots | index($a)
  )]' <<< "$all_threads_json")
thread_count=$(jq 'length' <<< "$threads_json")

# No bot threads → initial review
if ((thread_count == 0)); then
  printf "mode=initial\n"
  exit 0
fi

source .github/workflows/code_owners.sh

needs_verification="[]"
resolved_count=0
unresolved_count=0
verify_count=0

while IFS= read -r thread; do
  resolved_by=$(jq -r '.resolvedBy.login // "null"' <<< "$thread")
  is_resolved=$(jq -r '.isResolved' <<< "$thread")
  comment_count=$(jq '.comments.nodes | length' <<< "$thread")
  latest_author=$(jq -r ".comments.nodes[$((comment_count - 1))].author.login" <<< "$thread")
  path=$(jq -r '.comments.nodes[0].path // "unknown"' <<< "$thread")
  line=$(jq -r '.comments.nodes[0].originalLine // "?"' <<< "$thread")

  # Rule 1: CODEOWNER resolved → skip
  if [[ $is_resolved == "true" ]] && is_codeowner "$resolved_by"; then
    ((++resolved_count))
    printf "Resolved (CODEOWNER): %s:%s\n" "$path" "$line" >&2
    continue
  fi

  # Rule 2: Latest comment is bot → unresolved
  if is_bot "$latest_author"; then
    ((++unresolved_count))
    printf "Unresolved: %s:%s\n" "$path" "$line" >&2
    continue
  fi

  # Rule 3: Latest comment is anyone else → needs verification
  ((++verify_count))
  printf "Needs verification: %s:%s\n" "$path" "$line" >&2
  needs_verification=$(jq --argjson thread "$thread" '. += [$thread]' <<< "$needs_verification")

done < <(jq -c '.[]' <<< "$threads_json")

printf "\nSummary: %d resolved, %d needs verification, %d unresolved\n" \
  "$resolved_count" "$verify_count" "$unresolved_count" >&2

if ((unresolved_count > 0)); then
  printf "FATAL: %d issue(s) have no developer response\n" "$unresolved_count" >&2
  exit 1
fi

actionable_count=$(jq 'length' <<< "$needs_verification")
printf "%s" "$needs_verification" > /tmp/claude-actionable-threads.json
printf "mode=rereview\nactionable_count=%d\ntotal_count=%d\n" \
  "$actionable_count" "$thread_count"
