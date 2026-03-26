#!/bin/bash
# Copyright (c) 2026 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Bot logins used by the code review workflow.
#
# Source this file to get:
#   - bot_logins: array of bot login names
#   - is_bot <user>: returns 0 if user is a bot

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
