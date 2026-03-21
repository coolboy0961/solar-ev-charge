#!/usr/bin/env bash
# Automatic release script
# Analyzes conventional commits since last tag, determines version bump,
# generates changelog grouped by module, creates tag and GitHub release.
#
# Usage: ./scripts/release.sh [--dry-run]

set -euo pipefail

DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=true
fi

# --- Get last tag ---
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
if [[ -z "$LAST_TAG" ]]; then
    echo "No previous tag found. Using initial commit as base."
    COMMIT_RANGE="HEAD"
else
    echo "Last tag: $LAST_TAG"
    COMMIT_RANGE="${LAST_TAG}..HEAD"
fi

# --- Collect commits ---
COMMITS=$(git log "$COMMIT_RANGE" --pretty=format:"%H %s" --no-merges 2>/dev/null || true)

if [[ -z "$COMMITS" ]]; then
    echo "No new commits since $LAST_TAG. Nothing to release."
    exit 0
fi

# --- Determine version bump ---
HAS_BREAKING=false
HAS_FEAT=false
HAS_FIX=false

while IFS= read -r line; do
    msg="${line#* }"  # strip hash
    if echo "$msg" | grep -qi "BREAKING CHANGE\|^[a-z]*!:"; then
        HAS_BREAKING=true
    elif echo "$msg" | grep -q "^feat"; then
        HAS_FEAT=true
    elif echo "$msg" | grep -q "^fix"; then
        HAS_FIX=true
    fi
done <<< "$COMMITS"

# Parse current version
if [[ -z "$LAST_TAG" ]]; then
    MAJOR=0; MINOR=0; PATCH=0
else
    VERSION="${LAST_TAG#v}"
    MAJOR=$(echo "$VERSION" | cut -d. -f1)
    MINOR=$(echo "$VERSION" | cut -d. -f2)
    PATCH=$(echo "$VERSION" | cut -d. -f3)
fi

if $HAS_BREAKING; then
    MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0
elif $HAS_FEAT; then
    MINOR=$((MINOR + 1)); PATCH=0
elif $HAS_FIX; then
    PATCH=$((PATCH + 1))
else
    # docs, chore, etc. — still release as patch
    PATCH=$((PATCH + 1))
fi

NEW_TAG="v${MAJOR}.${MINOR}.${PATCH}"
echo "Version bump: ${LAST_TAG:-none} → $NEW_TAG"

# --- Generate changelog grouped by module ---
MODULES=("wisun-smartmeter" "homeassistant" "esphome" "docker" "docs")
MODULE_LABELS=("Wi-SUN Smart Meter" "Home Assistant" "ESPHome" "Docker" "Documentation")

CHANGELOG="## ${NEW_TAG} ($(date +%Y-%m-%d))\n"
OTHER_CHANGES=""

for i in "${!MODULES[@]}"; do
    module="${MODULES[$i]}"
    label="${MODULE_LABELS[$i]}"
    section=""

    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        hash="${line%% *}"
        msg="${line#* }"

        # Check if commit touches this module
        files=$(git diff-tree --no-commit-id --name-only -r "$hash" 2>/dev/null || true)
        if echo "$files" | grep -q "^${module}/"; then
            # Clean up commit message for display
            clean_msg=$(echo "$msg" | sed 's/^[a-z]*(\([^)]*\)): /\1: /' | sed 's/^[a-z]*: //')
            prefix=$(echo "$msg" | grep -o '^[a-z]*' || echo "other")
            section="${section}\n- **${prefix}**: ${clean_msg}"
        fi
    done <<< "$COMMITS"

    if [[ -n "$section" ]]; then
        CHANGELOG="${CHANGELOG}\n### ${label}\n${section}\n"
    fi
done

# Collect commits that don't match any module (root-level changes)
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    hash="${line%% *}"
    msg="${line#* }"
    files=$(git diff-tree --no-commit-id --name-only -r "$hash" 2>/dev/null || true)

    matched=false
    for module in "${MODULES[@]}"; do
        if echo "$files" | grep -q "^${module}/"; then
            matched=true
            break
        fi
    done

    if ! $matched; then
        clean_msg=$(echo "$msg" | sed 's/^[a-z]*(\([^)]*\)): /\1: /' | sed 's/^[a-z]*: //')
        prefix=$(echo "$msg" | grep -o '^[a-z]*' || echo "other")
        OTHER_CHANGES="${OTHER_CHANGES}\n- **${prefix}**: ${clean_msg}"
    fi
done <<< "$COMMITS"

if [[ -n "$OTHER_CHANGES" ]]; then
    CHANGELOG="${CHANGELOG}\n### Other\n${OTHER_CHANGES}\n"
fi

echo ""
echo "=== Release Notes ==="
echo -e "$CHANGELOG"

if $DRY_RUN; then
    echo "[dry-run] Would create tag $NEW_TAG"
    exit 0
fi

# --- Create tag and release ---
git tag -a "$NEW_TAG" -m "Release $NEW_TAG"
git push origin "$NEW_TAG"

# Create GitHub release
echo -e "$CHANGELOG" | gh release create "$NEW_TAG" \
    --title "$NEW_TAG" \
    --notes-file -

echo ""
echo "Released $NEW_TAG"
