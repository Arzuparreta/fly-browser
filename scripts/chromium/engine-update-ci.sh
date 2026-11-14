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
# Keep replay output ahead of a later exception in captured Python logs, so
# failure comments retain the diagnostic instead of a buffered success list.
export PYTHONUNBUFFERED=1

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

agent_propose() {
    local channel=$1 feedback=$2
    python3 scripts/chromium/agent_repair.py propose --channel "$channel" \
        --logs "$LOGS/agent-$channel" --failure-log "$feedback"
}

commit_repair() {
    local message=$1
    npm test >"$LOGS/repair-npm-test.log" 2>&1
    git diff --check
    if ! git diff --quiet; then
        git add chromium/patches chromium/overlay/fly
        git diff --cached --check
        git commit --quiet -m "$message"
        git push --quiet origin "HEAD:refs/heads/$BRANCH"
    fi
}

validate() {
    local pr version sha reference kind attempt deferred result
    pr=$(saved pr) version=$(saved version)
    kind=$(saved kind)
    [ -n "$pr" ] || fail "No update pull request was prepared."
    if [ "$kind" = beta ] || [ "$kind" = beta-semantic ]; then
        validate_beta
        return
    fi
    : "${FLY_ENGINE_CANDIDATE_ROOT:?the runner service must set FLY_ENGINE_CANDIDATE_ROOT}"
    : "${FLY_ENGINE_ROOT:?the runner service must set FLY_ENGINE_ROOT}"
    for attempt in 1 2; do
        deferred=false
        # A job's state file is ephemeral. Derive semantic validation from
        # the actual PR diff on EVERY run, including rejected/failed retries.
        kind=$(python3 scripts/chromium/agent_repair.py kind --channel Stable --base origin/main) || {
            fail "The update branch contains changes outside automatic repair scope; preserve it for review."
        }
        if [ "$kind" = semantic ]; then
            save kind stable-semantic
        fi
        sha=$(git rev-parse HEAD)
        if [ "$kind" != semantic ] && [ "$(status_of "$sha")" = success ]; then
            note "$sha already passed native validation."
            return
        fi
        set_status "$sha" pending "Checking patches and native compatibility on Chromium $version"
        if ! python3 scripts/chromium/engine_update.py patch-check >"$LOGS/patch-check.log" 2>&1; then
            reference=$(git show origin/main:chromium/upstream.lock.json | python3 -c \
                'import json,sys; print(json.load(sys.stdin)["chromium"]["revision"])')
            if ! python3 scripts/chromium/engine_update.py patch-repair --reference "$reference" \
                    >"$LOGS/repair.log" 2>&1; then
                if ! agent_propose Stable "$LOGS/repair.log" >"$LOGS/agent-propose.log" 2>&1; then
                    set_status "$sha" failure "Automatic patch repair stopped safely"
                    comment_failure "$pr" "$LOGS/agent-propose.log" "Automatic coding-agent patch repair"
                    return 1
                fi
                save kind stable-semantic
            fi
            commit_repair "Repair Fly integration for Chromium $version"
            if [ "$sha" != "$(git rev-parse HEAD)" ]; then
                set_status "$sha" failure "Superseded by the automatically repaired head"
                sha=$(git rev-parse HEAD)
            fi
        fi
        set_status "$sha" pending "Building Chromium $version and Fly, then running Fly's tests"
        if [ "$(saved kind)" = stable-semantic ]; then
            if python3 scripts/chromium/agent_repair.py validate --channel Stable \
                    --logs "$LOGS/agent-Stable" >"$LOGS/validate.log" 2>&1; then
                set_status "$sha" success "Independent review, control/Fly builds and native tests passed"
                return
            else
                result=$?
                [ "$result" != 2 ] || deferred=true
            fi
        elif python3 scripts/chromium/engine_update.py validate --root "$FLY_ENGINE_CANDIDATE_ROOT" \
                --test-tools "$FLY_ENGINE_ROOT/test-tools" --jobs "$(nproc)" >"$LOGS/validate.log" 2>&1; then
            set_status "$sha" success "Chromium $version: control and Fly built, Fly's tests passed"
            return
        fi
        tail -n 80 "$LOGS/validate.log"
        set_status "$sha" failure "Native validation failed on Chromium $version"
        comment_failure "$pr" "$LOGS/validate.log" "Native validation"
        if [ "$deferred" = true ] || [ "$attempt" = 2 ] || ! agent_propose Stable "$LOGS/validate.log" >"$LOGS/agent-propose.log" 2>&1; then
            note "Safe repair did not pass; #$pr remains open."
            return 1
        fi
        save kind stable-semantic
        commit_repair "Adapt Fly to Chromium $version APIs"
    done
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

beta_target_current() {
    local sha=$1 pr=$2
    if python3 scripts/chromium/agent_repair.py target-current --channel Beta \
            --logs "$LOGS/agent-Beta" >"$LOGS/beta-target.log" 2>&1; then
        return
    fi
    set_status "$sha" failure "Beta or active Stable changed; revalidation is required"
    comment_failure "$pr" "$LOGS/beta-target.log" "Beta target freshness check"
    # Target drift is an operational deferral, not a code failure for the
    # proposer to repair. Never leave a stale green engine-validation status.
    return 2
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
        if [ "$(saved kind)" = beta-semantic ]; then
            beta_target_current "$sha" "$pr" || fail "Beta or Stable advanced; preserve #$pr for revalidation."
        fi
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
    local sha pr kind result
    sha=$(git rev-parse HEAD) pr=$(saved pr)
    kind=$(python3 scripts/chromium/agent_repair.py kind --base origin/main)
    if [ "$kind" = semantic ]; then
        save kind beta-semantic
        set_status "$sha" pending "Independent review and Stable/Beta native compilation/tests"
        if python3 scripts/chromium/agent_repair.py validate --channel Beta \
                --logs "$LOGS/agent-Beta" >"$LOGS/beta-validation.log" 2>&1; then
            :
        else
            result=$?
            set_status "$sha" failure "coding-agent review or Stable/Beta native validation failed"
            comment_failure "$pr" "$LOGS/beta-validation.log" "Semantic Beta repair validation"
            return "$result"
        fi
        beta_target_current "$sha" "$pr" || return $?
        set_status "$sha" success "Independent review, Stable/Beta control/Fly builds and native tests passed"
        return
    fi
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

publish_beta() {
    # Caller owns pr and the worktree. Every new head receives local tests and
    # then remote checks; an existing branch is updated without force-pushing.
    npm test >"$LOGS/beta-npm-test.log" 2>&1
    git diff --check
    git add chromium/patches chromium/overlay/fly
    git diff --cached --check
    if ! git diff --cached --quiet; then
        git commit --quiet -m "Repair Fly compatibility with Chromium Beta"
    fi
    python3 scripts/chromium/agent_repair.py scope --base origin/main
    git fetch --quiet origin main
    git merge-base --is-ancestor origin/main HEAD || {
        note "main moved during Beta preparation; retrying on the next run."
        return 1
    }
    git push --quiet origin "HEAD:refs/heads/$BRANCH"
    if [ -z "$pr" ]; then
        if [ "$(python3 scripts/chromium/agent_repair.py kind --base origin/main)" = semantic ]; then
            cat >"$LOGS/beta-body.md" <<'TEXT'
Adapts Fly's integration to Chromium Beta while preserving current Stable behavior. Proposed by the authenticated host coding-agent in a restricted, separate worktree; an independent read-only agent execution reviewed the actual diff and upstream callers. Scripts, workflows, existing tests, command catalogs, version data, and the shipping pin cannot be changed by the proposer.

Local validation: strict complete-series replay on Stable and the frozen Beta plus npm tests. The `engine-validation` status additionally requires independent review and full control/Fly builds and unit/browser tests for both pins, in isolated non-production engine roots with no host credentials or real user profiles. Native validation remains pending until that status passes. The target is checked again before merge; main moving invalidates the old validation.

All ordinary latest-head PR checks must pass. Beta is never published and this preparation does not cut a release. An incomplete repair stays open with its logs; the warning closes only after merged main passes replay again.
TEXT
        else
            cat >"$LOGS/beta-body.md" <<'TEXT'
Reanchors Fly's existing integration edits where Chromium changed the surrounding lines. Added and removed lines remain identical; every repaired anchor is unique in both versions and the full patched Stable sources are byte-identical to main.

Validation: local npm tests, strict Stable/Beta replay and the `engine-validation` proof against current main. Checks are repeated if main moves. No engine checkout changes for a context-only repair. Beta compilation/runtime remain unverified until that engine undergoes native validation. No release is cut.
TEXT
        fi
        gh pr create --repo "$REPO" --base main --head "$BRANCH" \
            --title "Prepare Fly patches for Chromium Beta" --label impact:none \
            --body-file "$LOGS/beta-body.md" >/dev/null
        pr=$(open_pr)
    fi
    save pr "$pr"
}

beta() (
    credentials
    git fetch --quiet origin main
    local directory local_branch pr unexpected attempt result ready=false
    # Do not hold the only engine runner while a security release waits for it.
    if gh run list --repo "$REPO" --workflow release.yml --limit 30 \
            --json status --jq 'any(.[]; .status != "completed")' | grep -Fxq true; then
        note "A release owns or is waiting for the engine host; defer Beta repair to the next run."
        return
    fi
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
        # Reject executable/configuration changes before loading any branch
        # file. The trusted controller then enforces the narrower file rules.
        unexpected=$(git diff --name-only "origin/main...origin/$BRANCH" | sed -E \
            -e '/^chromium\/patches\/[a-zA-Z0-9._-]+\.patch$/d' \
            -e '/^chromium\/overlay\/fly\/.*\.(cc|h|mm)$/d' \
            -e '/^chromium\/overlay\/fly\/integration\.gni$/d')
        [ -z "$unexpected" ] || fail "Unexpected Beta branch changes; preserving it: $unexpected"
        python3 scripts/chromium/agent_repair.py scope --base origin/main --head "origin/$BRANCH"
        git merge --quiet --no-edit "origin/$BRANCH" || fail "Beta branch conflicts with main; preserving it."
    fi
    if ! python3 scripts/chromium/engine_update.py patch-repair --channel Beta \
            >"$LOGS/beta-repair.log" 2>&1; then
        if ! agent_propose Beta "$LOGS/beta-repair.log" >>"$LOGS/beta-repair.log" 2>&1; then
            tail -n 50 "$LOGS/beta-repair.log"
            beta_failure
            return 1
        fi
    fi
    if git diff --quiet origin/main HEAD && git diff --quiet && \
            [ -z "$(git ls-files --others --exclude-standard)" ]; then
        if [ -n "$pr" ]; then
            gh pr close "$pr" --repo "$REPO" --delete-branch \
                --comment "main already contains this preparation; no Beta changes remain."
        fi
        note "No Beta repair is needed."
        return
    fi
    for attempt in 1 2; do
        publish_beta
        if validate_beta; then
            ready=true
            break
        else
            result=$?
            if [ "$result" = 2 ]; then
                cp "$LOGS/beta-validation.log" "$LOGS/beta-repair.log"
                beta_failure
                return 1
            fi
        fi
        if [ "$attempt" = 2 ] || ! agent_propose Beta "$LOGS/beta-validation.log" >"$LOGS/beta-repair.log" 2>&1; then
            beta_failure
            return 1
        fi
    done
    [ "$ready" = true ] || return 1
    merge
    git fetch --quiet origin main
    git switch --quiet --detach origin/main
    python3 scripts/chromium/engine_update.py patch-check --channel Beta
    local issue
    issue=$(gh issue list --repo "$REPO" --state open \
        --search 'in:title "Fly patches do not apply to Chromium Beta"' \
        --json number,title --jq '.[] | select(.title == "Fly patches do not apply to Chromium Beta") | .number' | head -1)
    if [ -n "$issue" ]; then
        gh issue close "$issue" --repo "$REPO" \
            --comment "Automatic repair merged in #$pr. Every patch applies to Chromium Beta on main again; required validation passed."
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
        printf '%s\n\nAutomatic repair could not complete safely. See the bounded coding-agent attempt and validation logs below. The warning remains open; Stable tracking continues.\n\n' "$marker"
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
