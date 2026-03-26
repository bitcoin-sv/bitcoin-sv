#!/bin/bash
# Copyright (c) 2026 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Parse CODEOWNERS file and provide is_codeowner lookup.
#
# Source this file to get:
#   - codeowners: newline-separated string of CODEOWNER logins
#   - is_codeowner <user>: returns 0 if user is a CODEOWNER

readonly codeowners_file=".github/CODEOWNERS"

if [[ ! -f $codeowners_file ]]; then
  echo "FATAL: $codeowners_file not found" >&2
  exit 1
fi

# Strip comments, extract @user entries
codeowners=$(awk '{
  gsub(/#.*/, "")
  for(i = 1; i <= NF; i++)
    if($i ~ /^@[^/]+$/)
      print substr($i, 2)
}' "$codeowners_file" | sort -u)

if [[ -z $codeowners ]]; then
  echo "FATAL: No individual code owners found in $codeowners_file" >&2
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
