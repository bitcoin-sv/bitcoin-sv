#!/usr/bin/env bash
# cjg copyright
#
# resolve-review-issue.sh — resolve, unresolve, or reply to a GitHub PR review thread.
#
# Usage:
#   resolve-review-issue.sh resolve   <THREAD_ID>
#   resolve-review-issue.sh unresolve <THREAD_ID>
#   resolve-review-issue.sh reply     <THREAD_ID> <BODY>
#
# Requires: gh (GitHub CLI) authenticated with a token that has pull-request write access.

set -euo pipefail

usage() {
    echo "Usage:" >&2
    echo "  $0 resolve   <THREAD_ID>" >&2
    echo "  $0 unresolve <THREAD_ID>" >&2
    echo "  $0 reply     <THREAD_ID> <BODY>" >&2
    exit 1
}

ACTION="${1:-}"
THREAD_ID="${2:-}"

if [[ -z "$ACTION" || -z "$THREAD_ID" ]]; then
    usage
fi

case "$ACTION" in
    resolve)
        echo "Resolving thread $THREAD_ID ..." >&2
        gh api graphql \
            -f query='mutation($threadId: ID!) {
                resolveReviewThread(input: {threadId: $threadId}) {
                    thread { id }
                }
            }' \
            -f threadId="$THREAD_ID"
        echo "Thread $THREAD_ID resolved." >&2
        ;;

    unresolve)
        echo "Unresolving thread $THREAD_ID ..." >&2
        gh api graphql \
            -f query='mutation($threadId: ID!) {
                unresolveReviewThread(input: {threadId: $threadId}) {
                    thread { id }
                }
            }' \
            -f threadId="$THREAD_ID"
        echo "Thread $THREAD_ID unresolved." >&2
        ;;

    reply)
        BODY="${3:-}"
        if [[ -z "$BODY" ]]; then
            echo "Error: reply requires a BODY argument." >&2
            usage
        fi
        echo "Replying to thread $THREAD_ID ..." >&2
        gh api graphql \
            -f query='mutation($threadId: ID!, $body: String!) {
                addPullRequestReviewThreadReply(input: {
                    pullRequestReviewThreadId: $threadId,
                    body: $body
                }) {
                    comment { id }
                }
            }' \
            -f threadId="$THREAD_ID" \
            -f body="$BODY"
        echo "Reply posted to thread $THREAD_ID." >&2
        ;;

    *)
        echo "Error: unknown action '$ACTION'" >&2
        usage
        ;;
esac
