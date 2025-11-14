#!/bin/bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Filter threads for verification - pass only actionable threads to Claude

set -euo pipefail

: "${GITHUB_OUTPUT:?Required}"
: "${GITHUB_REPOSITORY:?Required}"

readonly owner=${GITHUB_REPOSITORY%/*}

mapfile -t codeowners < <(gh api "/orgs/$owner/teams/svn-global-owners/members" --jq '.[].login' 2>/dev/null)
threads=$(cat /tmp/claude-threads.json)

actionable_threads="[]"

# Function to check if user is CODEOWNER
is_codeowner() {
  local user=$1
  for codeowner in "${codeowners[@]}"; do
    if [[ $user == "$codeowner" ]]; then
      return 0
    fi
  done
  return 1
}

# Filter threads
while IFS= read -r thread; do
  resolved_by=$(jq -r '.resolvedBy.login // "null"' <<< "$thread")
  comment_count=$(jq '.comments.nodes | length' <<< "$thread")
  latest_author=$(jq -r ".comments.nodes[$((comment_count - 1))].author.login" <<< "$thread")

  # Skip if: CODEOWNER resolved, Claude replied, or CODEOWNER has latest comment
  if [[ $resolved_by != "null" ]] && is_codeowner "$resolved_by"; then
    continue
  elif [[ $latest_author == "claude" ]]; then
    continue
  elif is_codeowner "$latest_author"; then
    continue
  fi

  actionable_threads=$(jq --argjson thread "$thread" '. += [$thread]' <<< "$actionable_threads")
done < <(jq -c '.[]' <<< "$threads")

# Output results
actionable_count=$(jq 'length' <<< "$actionable_threads")
total_count=$(jq 'length' <<< "$threads")
printf "%s" "$actionable_threads" > /tmp/claude-actionable-threads.json
printf "actionable_count=%d\ntotal_count=%d\n" "$actionable_count" "$total_count" >> "$GITHUB_OUTPUT"
