#!/usr/bin/env node
// Cuts a Fly release without anyone having to choose a number.
//
// The impact of a change is decided once, by whoever writes it, as an
// `impact:` label on its pull request. This reads the labels of everything
// merged since the last tag and derives the next version from them, so
// releasing is never a judgement call made weeks later by someone reading a
// diff.
//
// Three steps, deliberately separate so a half-finished release is always in
// an obvious state:
//
//   plan      what would be released, and as what number
//   prepare   open the version-bump pull request, wait for its checks, merge it
//   finish    tag the merge commit, if release-tag.yml has not already
//
// Merging the bump pull request is the release: release-tag.yml tags the merge
// commit and starts the package builds. `finish` is the manual fallback for
// when that workflow did not run.
//
// `prepare` runs from a checkout rather than from Actions because a push made
// with GITHUB_TOKEN starts no workflows: a bot-authored bump would never
// receive its checks.
'use strict';
const {spawnSync} = require('node:child_process');
const {declaredVersion, setDeclaredVersion, sync, validate, fail} = require('./version-sync');

// Ordered by how much they move the number. `none` is not the absence of a
// label: it is the explicit claim that a change ships nothing a user would notice.
const IMPACTS = ['none', 'patch', 'minor', 'major'];
const LABEL_PREFIX = 'impact:';

// A pull request merged before the labels existed, or merged without one.
// Counted as a patch so history never blocks a release, and always listed by
// name so the claim can be checked.
const UNLABELLED = 'patch';

const VERSION_RE = /^(\d+)\.(\d+)\.(\d+)(?:-rc\.(\d+))?$/;

// Merges this long before the tag's commit cannot be after it, whatever the
// clocks say, and are skipped without asking git about them.
const CLOCK_SLACK_MS = 60 * 60 * 1000;

const CHECK_TIMEOUT_MS = 45 * 60 * 1000;
const CHECK_INTERVAL_MS = 15 * 1000;

function run(command, args, {check = true} = {}) {
    const result = spawnSync(command, args, {encoding: 'utf8'});
    if (result.error) fail(`$ ${command} ${args.join(' ')}\n${result.error.message}`);
    if (check && result.status !== 0) {
        fail(`$ ${command} ${args.join(' ')}\n${result.stdout}${result.stderr}`.trimEnd());
    }
    return {status: result.status, stdout: result.stdout.trim(), stderr: result.stderr.trim()};
}

const git = (...args) => run('git', args).stdout;
const gh = (...args) => run('gh', args).stdout;

function sleep(ms) {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
}

// The most recent vX.Y.Z tag on main, or null if nothing was ever released.
function lastTag() {
    const tags = git('tag', '--list', 'v*', '--sort=-v:refname', '--merged', 'origin/main');
    return tags ? tags.split('\n')[0] : null;
}

function isAncestor(commit, tag) {
    const {status} = run('git', ['merge-base', '--is-ancestor', commit, `refs/tags/${tag}`], {check: false});
    return status === 0 || status === 1 ? status === 0 : null;
}

// Whether the pull request merged as `commit` already shipped in `tag`.
//
// Asked of the history where it matters: GitHub's merge time and the tag's
// commit time come from different clocks, and the bump pull request itself is
// recorded as merged a second after the commit the tag points at, so a time
// comparison alone misplaces it. Only merges near or after the tag are checked
// against the history; when the commit is missing locally, the instants decide.
function inRelease(commit, mergedAt, tag, taggedAt, ancestor = isAncestor) {
    const merged = new Date(mergedAt).getTime();
    if (merged < taggedAt.getTime() - CLOCK_SLACK_MS) return true;
    if (commit) {
        const answer = ancestor(commit, tag);
        if (answer !== null) return answer;
    }
    return merged <= taggedAt.getTime();
}

function classify(entry) {
    const labels = new Set(entry.labels
        .map(label => label.name)
        .filter(name => name.startsWith(LABEL_PREFIX))
        .map(name => name.slice(LABEL_PREFIX.length)));
    const known = IMPACTS.filter(name => labels.has(name));
    return {
        number: entry.number,
        title: entry.title,
        impact: known.length ? known[known.length - 1] : UNLABELLED,
        labelled: known.length > 0
    };
}

// Pull requests merged into main after `tag` was cut.
function mergedSince(tag) {
    const taggedAt = tag ? new Date(git('log', '-1', '--format=%cI', `refs/tags/${tag}`)) : null;
    const entries = JSON.parse(gh('pr', 'list', '--state', 'merged', '--base', 'main', '--limit', '500',
        '--json', 'number,title,labels,mergedAt,mergeCommit') || '[]');
    return entries
        .filter(entry => !tag || !inRelease(entry.mergeCommit?.oid, entry.mergedAt, tag, taggedAt))
        .map(classify)
        .sort((a, b) => a.number - b.number);
}

// Applies `impact` to `current`.
//
// Below 1.0 a breaking change moves the minor, because that is what 0.x
// already promises: no stability guarantee across minors. Reaching 1.0 is a
// deliberate act, not something a label can trigger: pass --version for it.
function nextVersion(current, impact, {rc = false} = {}) {
    const match = VERSION_RE.exec(current);
    if (!match) fail(`Cannot reason about the current version ${JSON.stringify(current)}.`);
    let [major, minor, patch] = match.slice(1, 4).map(Number);
    const inProgressRc = match[4];

    // Already stabilising this number: the next candidate, not a new line.
    if (inProgressRc && rc) return `${major}.${minor}.${patch}-rc.${Number(inProgressRc) + 1}`;
    // A candidate becoming final keeps its number and drops the suffix.
    if (inProgressRc) return `${major}.${minor}.${patch}`;

    if (impact === 'major' && major >= 1) [major, minor, patch] = [major + 1, 0, 0];
    else if (impact === 'major' || impact === 'minor') [minor, patch] = [minor + 1, 0];
    else if (impact === 'patch') patch += 1;
    else fail('Nothing to release.');

    const base = `${major}.${minor}.${patch}`;
    return rc ? `${base}-rc.1` : base;
}

function highestImpact(pulls) {
    return pulls.reduce((highest, pull) => IMPACTS.indexOf(pull.impact) > IMPACTS.indexOf(highest) ? pull.impact : highest, 'none');
}

// The body of the bump pull request: what is going out, and why this number.
function summarise(pulls, tag) {
    const lines = [`Everything merged into \`main\` ${tag ? `since ${tag}` : 'since the repository began'}.`, ''];
    for (const impact of [...IMPACTS].reverse()) {
        const batch = pulls.filter(pull => pull.impact === impact && pull.labelled);
        if (!batch.length) continue;
        lines.push(`### impact:${impact}`, '', ...batch.map(pull => `- #${pull.number} ${pull.title}`), '');
    }
    const unlabelled = pulls.filter(pull => !pull.labelled);
    if (unlabelled.length) {
        lines.push(`### No impact label, counted as \`${UNLABELLED}\``, '', ...unlabelled.map(pull => `- #${pull.number} ${pull.title}`), '');
    }
    return lines.join('\n');
}

// Returns the version to release, the impact that chose it, and the changes.
function resolve(options) {
    git('fetch', '--quiet', '--tags', 'origin', 'main');
    const tag = lastTag();
    const pulls = mergedSince(tag);
    if (options.version) return {version: validate(options.version), impact: 'explicit', pulls, tag};
    const impact = highestImpact(pulls);
    if (impact === 'none') fail(`Nothing merged since ${tag || 'the beginning'} claims any user-visible impact.`);
    return {version: nextVersion(declaredVersion(), impact, {rc: options.rc}), impact, pulls, tag};
}

// Waits until every check on the pull request has finished. True if all passed.
function waitForChecks(url) {
    const deadline = Date.now() + CHECK_TIMEOUT_MS;
    let reported = '';
    while (Date.now() < deadline) {
        const result = run('gh', ['pr', 'checks', url, '--json', 'name,bucket'], {check: false});
        let checks = [];
        try { checks = JSON.parse(result.stdout || '[]'); } catch {}
        const pending = checks.filter(check => check.bucket === 'pending');
        const failed = checks.filter(check => check.bucket === 'fail' || check.bucket === 'cancel');
        const summary = checks.map(check => `${check.name}: ${check.bucket}`).join(', ');
        if (summary && summary !== reported) {
            console.log(`  ${summary}`);
            reported = summary;
        }
        if (failed.length) return false;
        // Checks register a few seconds after the push; none yet is not success.
        if (checks.length && !pending.length) return true;
        sleep(CHECK_INTERVAL_MS);
    }
    console.error('  Timed out waiting for checks.');
    return false;
}

function plan(options) {
    const {version, impact, pulls, tag} = resolve(options);
    console.log(`${declaredVersion()} → ${version}  (${impact})\n`);
    console.log(summarise(pulls, tag));
}

function prepare(options) {
    if (git('status', '--porcelain')) fail('Working tree is dirty; commit or stash first.');

    const {version, impact, pulls, tag} = resolve(options);
    const branch = `release/v${version}`;
    console.log(`Releasing ${declaredVersion()} → ${version} (${impact})`);

    git('switch', '--quiet', '--create', branch, 'origin/main');
    setDeclaredVersion(version);
    sync(version, {check: false});
    git('commit', '--quiet', '--all', '--message', `Version ${version}`);
    git('push', '--quiet', '--set-upstream', 'origin', branch);

    const body = [
        `Bump to \`${version}\`, derived from the impact labels below.`,
        '',
        summarise(pulls, tag),
        `Merging this tags \`v${version}\` and publishes the Linux, macOS and Windows packages. Nothing in this pull request is hand-written: \`scripts/version-sync.js\` produced every changed line.`,
        ''
    ].join('\n');
    const url = gh('pr', 'create', '--base', 'main', '--head', branch, '--title', `Version ${version}`,
        '--body', body, '--label', 'impact:none').split('\n').pop();
    console.log(`\n${url}`);

    if (options.hold) {
        console.log('Held. Merging it tags the release and builds it.');
        return;
    }
    console.log('Waiting for its checks.');
    if (!waitForChecks(url)) {
        console.error(`Checks did not pass; ${url} stays open. Fix the cause, or merge it by hand once they pass.`);
        process.exitCode = 1;
        return;
    }
    gh('pr', 'merge', url, '--squash', '--delete-branch');
    console.log(`Merged. release-tag.yml now tags v${version} and starts the builds:`);
    console.log('  gh run list --workflow release-tag.yml --limit 1');
}

function finish() {
    git('fetch', '--quiet', '--tags', 'origin', 'main');
    git('switch', '--quiet', 'main');
    git('pull', '--quiet', '--ff-only');

    const version = declaredVersion();
    const tag = `v${version}`;
    if (git('tag', '--list', tag)) fail(`${tag} already exists; this release is already out.`);

    const head = git('rev-parse', 'HEAD');
    if (head !== git('rev-parse', 'origin/main')) fail('Local main is not what origin/main is; refusing to tag.');
    if (sync(version, {check: true}).length) fail('Copies disagree with package.json; not tagging.');

    git('tag', '--annotate', tag, '--message', `Fly ${version}`);
    git('push', 'origin', tag);
    console.log(`${tag} → ${head.slice(0, 7)}. The release workflow is building it.`);
}

function parseOptions(argv) {
    const options = {rc: argv.includes('--rc'), hold: argv.includes('--hold'), version: null};
    const index = argv.indexOf('--version');
    if (index !== -1) options.version = argv[index + 1] || fail('--version needs a value');
    return options;
}

const COMMANDS = {plan, prepare, finish};
const USAGE = `Usage: node scripts/release.js <plan|prepare|finish> [--rc] [--version X.Y.Z] [--hold]

  plan      show what would be released and as what number
  prepare   open the version-bump pull request, wait for its checks and merge it
  finish    tag the merged bump commit (fallback when release-tag.yml did not run)

  --rc            cut a release candidate instead of a stable release
  --version X.Y.Z release this exact version instead of the derived one
  --hold          prepare only: open the pull request but do not merge it`;

if (require.main === module) {
    const [command, ...rest] = process.argv.slice(2);
    if (!COMMANDS[command]) {
        console.error(USAGE);
        process.exitCode = 2;
    } else {
        try {
            COMMANDS[command](parseOptions(rest));
        } catch (error) {
            if (!error.releaseTooling) throw error;
            console.error(error.message);
            process.exitCode = 1;
        }
    }
}

module.exports = {IMPACTS, UNLABELLED, classify, inRelease, nextVersion, highestImpact, summarise, parseOptions};
