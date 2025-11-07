#!/bin/bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE
# Enforce Zero-Tolerance Policy for Claude Code Review

set -euo pipefail

: "${COMMIT_SHA:?Required}"

readonly sha=$COMMIT_SHA pr=$(gh pr view --json number -q .number)

# Fetch review threads via GraphQL
response=$(gh api graphql -f query='
query($pr: Int!) {
  repository(owner: "{owner}", name: "{repo}") {
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
}' -F pr="$pr" -q '.data.repository.pullRequest.reviewThreads.nodes')

# Count unresolved Claude threads
unresolved=0
while IFS= read -r thread; do
  author=$(jq -r '.comments[0].author.login' <<< "$thread")
  is_resolved=$(jq -r '.isResolved' <<< "$thread")

  if [[ $author =~ ^(claude|github-actions)(\[bot\])?$ && $is_resolved == false ]]; then
    ((++unresolved))
    path=$(jq -r '.comments[0].path // "unknown"' <<< "$thread")
    line=$(jq -r '.comments[0].line // "?"' <<< "$thread")
    printf "Unresolved: %s:%s\n" "$path" "$line"
  fi
done < <(jq -c '.[]' <<< "$response")

# Set commit status and exit
if ((unresolved > 0)); then
  printf "\n%d unresolved issue(s) - BLOCKING\n" "$unresolved"
  gh api "repos/{owner}/{repo}/statuses/$sha" \
    -f state=failure \
    -f context="Claude Code Review" \
    -f description="$unresolved unresolved issue(s) block merge"
  exit 1
fi

printf "All issues resolved - PASSING\n"
gh api "repos/{owner}/{repo}/statuses/$sha" \
  -f state=success \
  -f context="Claude Code Review" \
  -f description="All issues resolved"
