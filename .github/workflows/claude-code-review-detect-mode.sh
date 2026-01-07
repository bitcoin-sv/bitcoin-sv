#!/bin/bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Detect review mode (initial vs re-review) for Claude Code Review

set -euo pipefail
: "${GITHUB_REPOSITORY:?Required}"
: "${PR_NUMBER:?Required}"
: "${GITHUB_OUTPUT:?Required}"

threads_json=$(gh api graphql -f query="
query {
  repository(owner: \"${GITHUB_REPOSITORY%/*}\", name: \"${GITHUB_REPOSITORY#*/}\") {
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
}" -q '[.data.repository.pullRequest.reviewThreads.nodes[] | select(.comments.nodes[0].author.login == "claude")]')

thread_count=$(jq 'length' <<< "$threads_json")

if ((thread_count == 0)); then
  printf "mode=initial\nthread_count=0\n" >> "$GITHUB_OUTPUT"
else
  printf "mode=rereview\nthread_count=%d\n" "$thread_count" >> "$GITHUB_OUTPUT"
  printf "%s" "$threads_json" > /tmp/claude-threads.json
fi
