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

# Check that Claude review step completed successfully

printf "Checking Claude review execution status...\n"

# Check step outcome (built-in GitHub Actions variable)
if [[ "${CLAUDE_OUTCOME:-}" != "success" ]]; then
  printf "ERROR: Claude review step failed (outcome: %s)\n" \
    "${CLAUDE_OUTCOME:-unknown}"
  printf "The review execution failed or was cancelled.\n"
  printf "Check the action logs for errors or permission denials.\n"
  gh api "repos/$owner/$repo/statuses/$sha" \
    -f state=failure \
    -f context="Claude Code Review" \
    -f description="Review execution failed"
  exit 1
fi

# Check execution file for detailed error information
if [[ -n "${EXECUTION_FILE:-}" && -f "${EXECUTION_FILE}" ]]; then
  printf "Parsing execution file: %s\n" "$EXECUTION_FILE"

  # Extract result message from execution file
  is_error=$(jq -r 'map(select(.type == "result")) | last | .is_error | if . == null then "unknown" else tostring end' "$EXECUTION_FILE" 2>/dev/null || echo "unknown")
  denials=$(jq -r 'map(select(.type == "result")) | last | .permission_denials | length' "$EXECUTION_FILE" 2>/dev/null || echo "0")

  if [[ "$is_error" == "true" ]]; then
    printf "ERROR: Claude review completed with errors\n"
    gh api "repos/$owner/$repo/statuses/$sha" \
      -f state=failure \
      -f context="Claude Code Review" \
      -f description="Review completed with errors"
    exit 1
  fi

  if [[ "$denials" -gt 0 ]]; then
    printf "WARNING: Claude had %s permission denial(s)\n" "$denials"
  fi
else
  printf "Note: Execution file not found, skipping detailed error check\n"
fi

printf "✓ Claude execution completed successfully\n\n"

# ─────────────────────────────────────────────────────────────────
# Check for Claude's summary comment
# The workflow uses track_progress: true, so Claude always posts
# a summary on successful review. No summary = review failed.
# ─────────────────────────────────────────────────────────────────

printf "Checking for Claude summary comment...\n"

summary_count=$(gh api "/repos/$owner/$repo/issues/$pr/comments" \
  --jq '[.[] | select(.user.login == "claude[bot]" or .user.login == "github-actions[bot]")] | length')

if [[ "$summary_count" -eq 0 ]]; then
  printf "ERROR: No Claude summary comment found\n"
  printf "The review did not complete successfully.\n"
  printf "Check the action logs for permission denials or errors.\n"
  gh api "repos/$owner/$repo/statuses/$sha" \
    -f state=failure \
    -f context="Claude Code Review" \
    -f description="Review incomplete: no summary posted"
  exit 1
fi

printf "✓ Summary comment found (%d comment(s))\n\n" "$summary_count"

# Fetch review threads via GraphQL
printf "Fetching review threads...\n"
response=$(gh api graphql -f query="
query {
  repository(owner: \"$owner\", name: \"$repo\") {
    pullRequest(number: $pr) {
      reviewThreads(first: 100) {
        nodes {
          isResolved
          isOutdated
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

# Count unresolved Claude threads (excluding outdated)
unresolved=0
total=0
outdated_count=0
if [[ $response == "null" || $response == "[]" ]]; then
  printf "No review threads found\n"
else
  while IFS= read -r thread; do
    author=$(jq -r '.comments.nodes[0].author.login' <<< "$thread")
    is_resolved=$(jq -r '.isResolved' <<< "$thread")
    is_outdated=$(jq -r '.isOutdated' <<< "$thread")

    if [[ $author =~ ^(claude|github-actions)(\[bot\])?$ ]]; then
      ((++total))

      # Skip outdated comments - they refer to code that has changed
      if [[ $is_outdated == "true" ]]; then
        ((++outdated_count))
        continue
      fi

      if [[ $is_resolved == "false" ]]; then
        ((++unresolved))
        path=$(jq -r '.comments.nodes[0].path // "unknown"' <<< "$thread")
        line=$(jq -r '.comments.nodes[0].line // "?"' <<< "$thread")
        printf "Unresolved: %s:%s\n" "$path" "$line"
      fi
    fi
  done < <(jq -c '.[]' <<< "$response")
fi

resolved=$((total - unresolved - outdated_count))
printf "\nClaude issues: %d resolved out of %d" "$resolved" "$total"
if ((outdated_count > 0)); then
  printf " (skipped %d outdated)" "$outdated_count"
fi
printf "\n"

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
