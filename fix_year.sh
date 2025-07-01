#!/bin/bash
set -e

# Export all commit info
git log --pretty=format:'%H|%ai|%s' --reverse > commits.txt

# Create a new orphan branch
git checkout --orphan temp-branch

# Get all files from first commit
FIRST_COMMIT=$(head -1 commits.txt | cut -d'|' -f1)
git checkout $FIRST_COMMIT -- .

# Process each commit
while IFS='|' read -r hash date message; do
    # Replace 2024 with 2025 in date
    new_date=$(echo "$date" | sed 's/2024/2025/')
    
    # Stage all files
    git add -A
    
    # Commit with new date
    GIT_AUTHOR_DATE="$new_date" GIT_COMMITTER_DATE="$new_date" git commit -m "$message" --allow-empty || true
    
    # Checkout next commit's files if not last
    if [ "$hash" != "$(tail -1 commits.txt | cut -d'|' -f1)" ]; then
        git checkout $hash -- . 2>/dev/null || true
    fi
done < commits.txt

echo "Done processing commits"
