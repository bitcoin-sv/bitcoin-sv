#!/bin/bash
# Copyright (c) 2026 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Fetch CODEOWNERS team members and provide is_codeowner lookup.
#
# Source this file to get:
#   - codeowners: newline-separated string of CODEOWNER logins
#   - is_codeowner <user>: returns 0 if user is a CODEOWNER

: "${GITHUB_REPOSITORY:?Required}"

codeowners=$(gh api "/orgs/${GITHUB_REPOSITORY%/*}/teams/svn-global-owners/members" --jq '.[].login') || {
  echo "FATAL: Failed to fetch CODEOWNERS team members (check organization:read permission)" >&2
  exit 1
}

if [[ -z $codeowners ]]; then
  echo "FATAL: CODEOWNERS team has no members" >&2
  exit 1
fi

function is_codeowner
{
  local user=$1
  local codeowner
  while IFS= read -r codeowner; do
    if [[ $user == "$codeowner" ]]; then
      return 0
    fi
  done <<< "$codeowners"
  return 1
}
