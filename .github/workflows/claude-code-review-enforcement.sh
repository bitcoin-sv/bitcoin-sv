#!/bin/bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Enforce Zero-Tolerance Policy for Claude Code Review

set -euo pipefail

: "${COMMIT_SHA:?Required}"
: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"

readonly sha=$COMMIT_SHA
readonly pr=$PR_NUMBER
readonly owner=${GITHUB_REPOSITORY%/*}
readonly repo=${GITHUB_REPOSITORY#*/}

# Fetch review threads via GraphQL
printf "Fetching review threads...\n"
response=$(gh api graphql -f query="
query {
  repository(owner: \"$owner\", name: \"$repo\") {
    pullRequest(number: $pr) {
      reviewThreads(first: 100) {
        nodes {
          isResolved
          comments(first: 1) {
            nodes {
              author { login }
              path
              line
            }
          }
        }
      }
    }
  }
}" -q '.data.repository.pullRequest.reviewThreads.nodes')

# Count unresolved Claude threads
unresolved=0
total=0
if [[ $response == "null" || $response == "[]" ]]; then
  printf "No review threads found\n"
else
  while IFS= read -r thread; do
    author=$(jq -r '.comments.nodes[0].author.login' <<< "$thread")
    is_resolved=$(jq -r '.isResolved' <<< "$thread")

    if [[ $author =~ ^(claude|github-actions)(\[bot\])?$ ]]; then
      ((++total))
      if [[ $is_resolved == "false" ]]; then
        ((++unresolved))
        path=$(jq -r '.comments.nodes[0].path // "unknown"' <<< "$thread")
        line=$(jq -r '.comments.nodes[0].line // "?"' <<< "$thread")
        printf "Unresolved: %s:%s\n" "$path" "$line"
      fi
    fi
  done < <(jq -c '.[]' <<< "$response")
fi

resolved=$((total - unresolved))
printf "\nClaude issues: %d resolved out of %d\n" "$resolved" "$total"

# Set commit status and exit
if ((unresolved > 0)); then
  printf "\n%d unresolved issue(s) - BLOCKING\n" "$unresolved"
  gh api "repos/$owner/$repo/statuses/$sha" \
    -f state=failure \
    -f context="Claude Code Review" \
    -f description="$unresolved unresolved issue(s) block merge"
  exit 1
fi

printf "All issues resolved - PASSING\n"
gh api "repos/$owner/$repo/statuses/$sha" \
  -f state=success \
  -f context="Claude Code Review" \
  -f description="All issues resolved"
