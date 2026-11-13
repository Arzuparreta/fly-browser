#!/usr/bin/env bash
# Steps of .github/workflows/engine-update.yml, on the trusted engine host.
#
#   engine-update-ci.sh prepare    commit a newer Stable pin to the update
#                                  branch and open or update its pull request
#   engine-update-ci.sh validate   build and test that branch in the candidate
#                                  engine root; record the result on the commit
#   engine-update-ci.sh merge      merge once every check passed, revalidating
#                                  after main moves
#   engine-update-ci.sh release    cut the release that ships it
#   engine-update-ci.sh beta       repair Beta context drift in an isolated
#                                  worktree, validate and merge without shipping
#
# Pushes, pull requests and merges use the host's `gh` login, not
# GITHUB_TOKEN: a push made with GITHUB_TOKEN starts no workflows, so the
# pull request would never receive its checks. docs/chromium/engine-updates.md
# describes the process.
set -euo pipefail

BRANCH=${FLY_ENGINE_UPDATE_BRANCH:-engine/chromium-stable}
CONTEXT=engine-validation
REPO=${GITHUB_REPOSITORY:?}
STATE=${RUNNER_TEMP:-/tmp}/engine-update-state
LOGS=${RUNNER_TEMP:-/tmp}/engine-update-logs
mkdir -p "$LOGS"
cd "$(git rev-parse --show-toplevel)"

note() { printf '%s\n' "$*"; }
fail() { printf '::error::%s\n' "$*"; exit 1; }
output() { [ -n "${GITHUB_OUTPUT:-}" ] && printf '%s=%s\n' "$1" "$2" >>"$GITHUB_OUTPUT"; true; }
saved() { sed -n "s/^$1=//p" "$STATE" 2>/dev/null | tail -1; }
save() { printf '%s=%s\n' "$1" "$2" >>"$STATE"; }
pinned() { python3 -c 'import json; print(json.load(open("chromium/upstream.lock.json"))["engine_version"])'; }
open_pr() { gh pr list --repo "$REPO" --head "$BRANCH" --state open --json number --jq '.[0].number // empty'; }

credentials() {
    gh auth status >/dev/null 2>&1 || fail "The engine host has no gh login; run gh auth login as the runner's user."
    git config user.name >/dev/null || fail "The engine host has no git user.name."
    git remote set-url origin "https://github.com/$REPO.git"
    git config --local --unset-all http.https://github.com/.extraheader 2>/dev/null || true
    # prepare and release both call this in the same checkout. Replace the
    # helpers instead of overwriting one value: after the first call there
    # are two, and `git config credential.helper ''` refuses to overwrite
    # several values (exit 5), which stopped every automatic release.
    git config --local --unset-all credential.helper 2>/dev/null || true
    git config --local --add credential.helper ''
    git config --local --add credential.helper '!gh auth git-credential'
}

status_of() {
    gh api "repos/$REPO/commits/$1/statuses" \
        --jq "[.[] | select(.context == \"$CONTEXT\")][0].state // \"\""
}

set_status() {
    gh api --silent "repos/$REPO/statuses/$1" -f state="$2" -f context="$CONTEXT" \
        -f description="$3" -f target_url="${GITHUB_SERVER_URL:-https://github.com}/$REPO/actions/runs/${GITHUB_RUN_ID:-0}"
}

body() {
    local version=$1
    cat <<TEXT
Moves Fly to Chromium \`$version\`, the latest official Stable release for Linux, with the security fixes Google published for it.

Opened by \`.github/workflows/engine-update.yml\`. The \`$CONTEXT\` status is the native validation on the engine host: the candidate engine root moved to this pin, the unmodified Chromium control and Fly were built, and Fly's unit and browser tests ran. The workflow merges this pull request once every check passes and then cuts a release. If a patch or the build fails, the failure is commented here: fix it on \`$BRANCH\` and run the workflow again (\`gh workflow run engine-update.yml\`).

Chromium release notes: https://chromereleases.googleblog.com/search/label/Stable%20updates

## What's New

### Chromium $version

Fly now runs Chromium $version, with the security fixes Google published for it.
TEXT
}

prepare() {
    credentials
    : >"$STATE"
    git fetch --quiet origin main
    local pr newer
    pr=$(open_pr)
    newer=$(python3 scripts/chromium/engine_update.py status | python3 -c 'import json,sys; print(json.load(sys.stdin)["newer"])')
    if [ -n "$pr" ]; then
        git fetch --quiet origin "$BRANCH"
        git switch --quiet --force-create "$BRANCH" "origin/$BRANCH"
        if ! git merge-base --is-ancestor origin/main HEAD; then
            git merge --quiet --no-edit origin/main ||
                fail "origin/main does not merge cleanly into $BRANCH; resolve it on the branch."
        fi
    elif [ "$newer" = True ]; then
        git switch --quiet --force-create "$BRANCH" origin/main
    else
        note "Fly already pins the latest Stable release ($(pinned)); no update pull request is open."
        output active false
        return
    fi
    python3 scripts/chromium/engine_update.py pin
    if [ -n "$pr" ] && git diff --quiet origin/main HEAD; then
        gh pr close "$pr" --repo "$REPO" --delete-branch \
            --comment "main already pins Chromium $(pinned); nothing left to update." >/dev/null
        note "Closed #$pr: main already has this pin."
        output active false
        return
    fi
    local version
    version=$(pinned)
    if ! git diff --quiet; then
        git commit --quiet --all --message "Update Chromium to $version"
    fi
    if [ -n "$pr" ]; then
        git push --quiet origin "HEAD:refs/heads/$BRANCH"
    else
        git push --quiet --force origin "HEAD:refs/heads/$BRANCH"
    fi
    body "$version" >"$LOGS/body.md"
    if [ -n "$pr" ]; then
        # Keep a description written for fixes on the branch; rewrite it only
        # when the pull request moves to another Chromium version.
        if [ "$(gh pr view "$pr" --repo "$REPO" --json title --jq .title)" != "Update Chromium to $version" ]; then
            gh pr edit "$pr" --repo "$REPO" --title "Update Chromium to $version" --body-file "$LOGS/body.md" >/dev/null
        fi
    else
        gh pr create --repo "$REPO" --base main --head "$BRANCH" --title "Update Chromium to $version" \
            --body-file "$LOGS/body.md" --label impact:patch >/dev/null
        pr=$(open_pr)
    fi
    note "Pull request #$pr: Chromium $version at $(git rev-parse --short HEAD)."
    save pr "$pr"
    save version "$version"
    output active true
}

comment_failure() {
    local pr=$1 log=$2 what=$3
    {
        printf '%s failed on `%s`.\n\n<details><summary>Last lines of the log</summary>\n\n```\n' "$what" "$(git rev-parse --short HEAD)"
        tail -n 80 "$log" | sed 's/```/` ` `/g'
        printf '```\n\n</details>\n\nFix it on `%s` and run the workflow again: `gh workflow run engine-update.yml`.\n' "$BRANCH"
    } >"$LOGS/comment.md"
    gh pr comment "$pr" --repo "$REPO" --body-file "$LOGS/comment.md" >/dev/null || true
}

validate() {
    local pr version sha
    pr=$(saved pr) version=$(saved version) sha=$(git rev-parse HEAD)
    [ -n "$pr" ] || fail "No update pull request was prepared."
    if [ "$(saved kind)" = beta ]; then
        validate_beta
        return
    fi
    if [ "$(status_of "$sha")" = success ]; then
        note "$sha already passed native validation."
        return
    fi
    : "${FLY_ENGINE_CANDIDATE_ROOT:?the runner service must set FLY_ENGINE_CANDIDATE_ROOT}"
    : "${FLY_ENGINE_ROOT:?the runner service must set FLY_ENGINE_ROOT}"
    set_status "$sha" pending "Checking that every patch applies to Chromium $version"
    if ! python3 scripts/chromium/engine_update.py patch-check 2>&1 | tee "$LOGS/patch-check.log"; then
        local reference
        reference=$(git show origin/main:chromium/upstream.lock.json | python3 -c \
            'import json,sys; print(json.load(sys.stdin)["chromium"]["revision"])')
        if ! python3 scripts/chromium/engine_update.py patch-repair --reference "$reference" \
                >"$LOGS/repair.log" 2>&1; then
            set_status "$sha" failure "Patch conflict needs a semantic repair"
            comment_failure "$pr" "$LOGS/repair.log" "Automatic context repair"
            fail "Safe automatic repair stopped; the update stays open."
        fi
        npm test >"$LOGS/npm-test.log" 2>&1
        git diff --check
        if ! git diff --quiet; then
            git add chromium/patches
            git commit --quiet -m "Reanchor Fly patches for Chromium $version"
            git push --quiet origin "HEAD:refs/heads/$BRANCH"
            set_status "$sha" failure "Superseded by the automatically repaired head"
            sha=$(git rev-parse HEAD)
        fi
    fi
    set_status "$sha" pending "Building Chromium $version and Fly, then running Fly's tests"
    if ! python3 scripts/chromium/engine_update.py validate --root "$FLY_ENGINE_CANDIDATE_ROOT" \
            --test-tools "$FLY_ENGINE_ROOT/test-tools" --jobs "$(nproc)" >"$LOGS/validate.log" 2>&1; then
        tail -n 200 "$LOGS/validate.log"
        set_status "$sha" failure "Native validation failed on Chromium $version"
        comment_failure "$pr" "$LOGS/validate.log" "Native validation"
        fail "Native validation failed; see the pull request."
    fi
    tail -n 20 "$LOGS/validate.log"
    set_status "$sha" success "Chromium $version: control and Fly built, Fly's tests passed"
}

checks_passed() {
    local pr=$1 deadline=$(( $(date +%s) + 3600 )) checks
    while [ "$(date +%s)" -lt "$deadline" ]; do
        checks=$(gh pr checks "$pr" --repo "$REPO" --json name,bucket 2>/dev/null || echo '[]')
        if jq -e 'any(.[]; .bucket == "fail" or .bucket == "cancel")' >/dev/null <<<"$checks"; then
            note "$checks"
            return 1
        fi
        if jq -e '(["verify", "impact_label", "engine-validation"] - [.[] | select(.bucket == "pass") | .name] | length == 0)
                and all(.[]; .bucket == "pass" or .bucket == "skipping")' >/dev/null <<<"$checks"; then
            return 0
        fi
        sleep 30
    done
    note "Timed out waiting for the checks of #$pr."
    return 1
}

merge() {
    local pr sha
    pr=$(saved pr)
    [ -n "$pr" ] || fail "No update pull request was prepared."
    for _ in 1 2 3 4 5; do
        git fetch --quiet origin main
        if ! git merge-base --is-ancestor origin/main HEAD; then
            # main moved while this was built: bring it in and validate the
            # combination (an incremental build).
            git merge --quiet --no-edit origin/main ||
                fail "origin/main does not merge cleanly into $BRANCH; resolve it on the branch."
            npm test >"$LOGS/merged-npm-test.log" 2>&1
            git diff --check
            git push --quiet origin "HEAD:refs/heads/$BRANCH"
            validate
            continue
        fi
        sha=$(git rev-parse HEAD)
        checks_passed "$pr" || fail "Checks of #$pr did not pass; it stays open."
        # Checks can take an hour. Recheck the base AFTER waiting as well.
        git fetch --quiet origin main
        git merge-base --is-ancestor origin/main HEAD || continue
        [ "$(gh pr view "$pr" --repo "$REPO" --json reviewDecision --jq .reviewDecision)" != CHANGES_REQUESTED ] ||
            fail "#$pr has a review requesting changes; it stays open."
        if gh pr merge "$pr" --repo "$REPO" --squash --delete-branch --match-head-commit "$sha"; then
            note "Merged #$pr."
            save merged true
            return
        fi
    done
    fail "main kept moving; #$pr stays open for the next run."
}

release() {
    [ "$(saved merged)" = true ] || { note "Nothing was merged; no release."; return; }
    credentials
    git fetch --quiet --tags origin main
    git switch --quiet --detach origin/main
    local hold=()
    if [ "${FLY_ENGINE_RELEASE:-}" = hold ]; then
        hold=(--hold)
    fi
    node scripts/release.js prepare "${hold[@]}"
}

validate_beta() {
    local sha pr
    sha=$(git rev-parse HEAD) pr=$(saved pr)
    set_status "$sha" pending "Proving unchanged Stable output and replaying both channels"
    if ! {
        python3 scripts/chromium/engine_update.py patch-verify --base origin/main &&
        python3 scripts/chromium/engine_update.py patch-check &&
        python3 scripts/chromium/engine_update.py patch-check --channel Beta
    } >"$LOGS/beta-validation.log" 2>&1; then
        set_status "$sha" failure "Beta repair proof or strict patch replay failed"
        comment_failure "$pr" "$LOGS/beta-validation.log" "Beta repair validation"
        return 1
    fi
    set_status "$sha" success "Context only; Stable output identical, Stable and Beta patches apply"
}

beta() (
    # This job only runs main's trusted tooling. Repair files live in a new
    # task worktree, never in the active/candidate engines or the runner's
    # primary checkout. Stable work is prepared separately afterwards.
    credentials
    git fetch --quiet origin main
    local directory local_branch pr unexpected
    directory=$(mktemp -d "${RUNNER_TEMP:-/tmp}/fly-beta-repair.XXXXXX")
    local_branch="automation/beta-${GITHUB_RUN_ID:-local}-$(basename "$directory")"
    git worktree add --quiet -b "$local_branch" "$directory/repo" origin/main
    trap 'cd "$directory/.."; git -C "$SOURCE_CHECKOUT" worktree remove --force "$directory/repo"; git -C "$SOURCE_CHECKOUT" branch -D "$local_branch" >/dev/null; rmdir "$directory"' EXIT
    SOURCE_CHECKOUT=$(git rev-parse --show-toplevel)
    cd "$directory/repo"
    BRANCH=engine/chromium-beta
    : >"$STATE"
    save kind beta
    pr=$(open_pr)
    if [ -n "$pr" ] || [ -n "$(git ls-remote --heads origin "$BRANCH")" ]; then
        git fetch --quiet origin "$BRANCH"
        # Never load scripts, hooks or configuration from an automation PR.
        # Its only allowed changes are existing plain-text patch files.
        unexpected=$(git diff --name-only "origin/main...origin/$BRANCH" | \
            sed -E '/^chromium\/patches\/[a-zA-Z0-9._-]+\.patch$/d')
        [ -z "$unexpected" ] || fail "Unexpected Beta branch changes; preserving it: $unexpected"
        git merge --quiet --no-edit "origin/$BRANCH" || fail "Beta branch conflicts with main; preserving it."
    fi
    if ! python3 scripts/chromium/engine_update.py patch-repair --channel Beta \
            >"$LOGS/beta-repair.log" 2>&1; then
        tail -n 60 "$LOGS/beta-repair.log"
        beta_failure
        note "::warning::Safe Beta repair stopped; the warning issue remains open. Stable tracking continues."
        return 1
    fi
    if ! git diff --quiet; then
        git diff --check
        git add chromium/patches
        git commit --quiet -m "Reanchor Fly integration patches for Chromium Beta"
    fi
    if git diff --quiet origin/main HEAD; then
        if [ -n "$pr" ]; then
            gh pr close "$pr" --repo "$REPO" --delete-branch \
                --comment "main already contains this preparation; no Beta patch changes remain."
        fi
        note "No Beta context repair is needed."
        return
    fi
    # Reject unexpected edits on a reused automation PR BEFORE pushing it.
    python3 scripts/chromium/engine_update.py patch-verify --base origin/main
    npm test >"$LOGS/beta-npm-test.log" 2>&1
    git diff --check origin/main HEAD
    git fetch --quiet origin main
    git merge-base --is-ancestor origin/main HEAD || {
        note "main moved during Beta preparation; retrying on the next scheduled run."
        return 1
    }
    git push --quiet origin "HEAD:refs/heads/$BRANCH"
    if [ -z "$pr" ]; then
        cat >"$LOGS/beta-body.md" <<'TEXT'
Reanchors Fly's existing integration edits where Chromium changed the surrounding lines. All added and removed lines remain identical, every repaired anchor is unique in both versions, and the full patched Stable sources are byte-for-byte identical to main.

Validation: local npm tests, strict Stable and Beta patch replay, and the `engine-validation` proof against current main. Checks are repeated if main moves. No engine checkout is changed; Beta compilation and runtime compatibility remain unverified until the normal Stable update builds and tests that engine. Semantic/API conflicts are refused and leave the warning issue open. This preparation does not cut a release.
TEXT
        gh pr create --repo "$REPO" --base main --head "$BRANCH" \
            --title "Prepare Fly patches for Chromium Beta" --label impact:none \
            --body-file "$LOGS/beta-body.md" >/dev/null
        pr=$(open_pr)
    fi
    save pr "$pr"
    validate_beta
    merge
    git fetch --quiet origin main
    git switch --quiet --detach origin/main
    # Close only after a new strict check of MERGED main. A green PR alone
    # never clears the issue, and a network failure leaves it open.
    python3 scripts/chromium/engine_update.py patch-check --channel Beta
    local issue
    issue=$(gh issue list --repo "$REPO" --state open \
        --search 'in:title "Fly patches do not apply to Chromium Beta"' \
        --json number,title --jq '.[] | select(.title == "Fly patches do not apply to Chromium Beta") | .number' | head -1)
    if [ -n "$issue" ]; then
        gh issue close "$issue" --repo "$REPO" \
            --comment "Automatic context repair merged in #$pr. Every patch applies to Chromium Beta on main again."
    fi
)

beta_failure() {
    local issue marker
    issue=$(gh issue list --repo "$REPO" --state open \
        --search 'in:title "Fly patches do not apply to Chromium Beta"' \
        --json number,title --jq '.[] | select(.title == "Fly patches do not apply to Chromium Beta") | .number' | head -1)
    [ -n "$issue" ] || return 0
    marker="<!-- fly-beta-repair:$(sha256sum "$LOGS/beta-repair.log" | cut -d' ' -f1) -->"
    # One explanation per distinct blocker, not a comment every six hours.
    if gh issue view "$issue" --repo "$REPO" --json comments --jq '.comments[].body' | \
            grep -F -- "$marker" >/dev/null; then
        return
    fi
    {
        printf '%s\n\nAutomatic context repair stopped without changing any patches. A developer must resolve this semantic or ambiguous conflict; Stable tracking continues.\n\n' "$marker"
        printf '<details><summary>Repair log</summary>\n\n```\n'
        tail -n 30 "$LOGS/beta-repair.log" | sed 's/```/` ` `/g'
        printf '```\n\n</details>\n\nRun: %s/%s/actions/runs/%s\n' \
            "${GITHUB_SERVER_URL:-https://github.com}" "$REPO" "${GITHUB_RUN_ID:-0}"
    } >"$LOGS/beta-blocker.md"
    gh issue comment "$issue" --repo "$REPO" --body-file "$LOGS/beta-blocker.md" >/dev/null
}

case "${1:-}" in
    prepare|validate|merge|release|beta) "$1" ;;
    *) fail "usage: engine-update-ci.sh prepare|validate|merge|release|beta" ;;
esac
