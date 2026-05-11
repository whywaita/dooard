#!/usr/bin/env bash
set -euo pipefail

workflow=".github/workflows/tagpr.yml"

if grep -q 'secrets.GITHUB_TOKEN' "$workflow"; then
  echo "$workflow must not use secrets.GITHUB_TOKEN"
  exit 1
fi

required_patterns=(
  'actions/create-github-app-token@'
  'app-id: ${{ secrets.APP_ID }}'
  'private-key: ${{ secrets.APP_PRIVATE_KEY }}'
  'id-token: write'
  'token: ${{ steps.app-token.outputs.token }}'
  'GITHUB_TOKEN: ${{ steps.app-token.outputs.token }}'
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$workflow"; then
    echo "$workflow is missing required pattern: $pattern"
    exit 1
  fi
done
