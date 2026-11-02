#!/usr/bin/env bash
set -euo pipefail
assets=$(realpath "$1")
repositories=$(realpath "$2")
tag=$3
repository=Arzuparreta/fly-browser
site_repository=Arzuparreta/fly-browser.github.io
test -n "${SITE_DEPLOY_KEY:-}"
test -f "$assets/SHA256SUMS.txt.asc"
test -f "$repositories/flat/InRelease"
test -f "$repositories/site/packages/rpm/repodata/repomd.xml.asc"

gh release upload "$tag" "$assets/"* --repo "$repository" --clobber
if ! gh release view linux-repository --repo "$repository" >/dev/null 2>&1; then
    gh release create linux-repository --repo "$repository" --target "$(git rev-parse HEAD)" \
        --prerelease --latest=false --title 'Official Linux package repository' \
        --notes 'Signed APT and pacman repository. Install using the official Fly instructions. This is repository infrastructure, not a browser release.'
fi
# Keep older, versioned package files available to clients holding an older
# signed index. Upload package bodies before switching the signed indexes.
for file in "$repositories/flat/"*.deb "$repositories/flat/"*.rpm "$repositories/flat/"*.pkg.tar.zst "$repositories/flat/"*.deb.sig "$repositories/flat/"*.rpm.sig "$repositories/flat/"*.pkg.tar.zst.sig; do
    gh release upload linux-repository "$file" --repo "$repository" --clobber
done
for name in Packages Packages.gz Release Release.gpg InRelease fly-browser.db fly-browser.db.sig fly-browser.files fly-browser.files.sig; do
    gh release upload linux-repository "$repositories/flat/$name" --repo "$repository" --clobber
done

temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
printf '%s\n' "$SITE_DEPLOY_KEY" > "$temp/deploy-key"
chmod 600 "$temp/deploy-key"
# GitHub's SSH host keys delivered through its authenticated HTTPS API.
gh api meta --jq '.ssh_keys[] | "github.com " + .' > "$temp/known_hosts"
export GIT_SSH_COMMAND="ssh -i $temp/deploy-key -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$temp/known_hosts"
if git ls-remote --exit-code --heads "git@github.com:$site_repository.git" gh-pages >/dev/null; then
    git clone --depth 1 --branch gh-pages "git@github.com:$site_repository.git" "$temp/site"
else
    git init -b gh-pages "$temp/site"
    git -C "$temp/site" remote add origin "git@github.com:$site_repository.git"
fi
# Only this directory belongs to distribution; a future landing is preserved.
rm -rf "$temp/site/packages"
cp -a "$repositories/site/packages" "$temp/site/packages"
mkdir -p "$temp/site/.github/workflows"
cp scripts/distribution/pages.yml "$temp/site/.github/workflows/fly-packages-pages.yml"
cp scripts/distribution/pages-build.py "$temp/site/.github/fly-packages-build.py"
touch "$temp/site/.nojekyll"
if [ ! -f "$temp/site/index.html" ] || grep -q '<!-- fly-packages-bootstrap -->' "$temp/site/index.html"; then
    printf '%s\n' '<!doctype html><html lang="en"><meta charset="utf-8"><title>Fly Browser</title><h1>Fly Browser</h1><p><a href="packages/">Official Linux downloads and installation</a></p></html>' > "$temp/site/index.html"
fi
if [ ! -f "$temp/site/README.md" ]; then cp scripts/distribution/site-README.md "$temp/site/README.md"; fi
git -C "$temp/site" add packages .nojekyll index.html README.md .github/workflows/fly-packages-pages.yml .github/fly-packages-build.py
if ! git -C "$temp/site" diff --cached --quiet; then
    git -C "$temp/site" -c user.name='Fly packages' -c user.email='41898282+github-actions[bot]@users.noreply.github.com' commit -m "Publish official packages for $tag"
    git -C "$temp/site" push origin gh-pages
fi

# Pages must finish deploying the matching metadata and RPM bodies before the
# browser release job is considered delivered. The previous deployment stays
# usable throughout a failed or in-progress build.
site=$(python3 -c 'import json; print(json.load(open("distribution/linux.json"))["site"])')
revision=$(sha256sum "$repositories/site/packages/rpm/repodata/repomd.xml" | awk '{print $1}')
rpm=$(find "$repositories/flat" -maxdepth 1 -name '*.rpm' -printf '%f\n' | head -n1)
for attempt in $(seq 1 60); do
    if curl --fail --silent --location "$site/packages/rpm/repodata/repomd.xml?ref=$revision&attempt=$attempt" -o "$temp/public-repomd.xml" &&
       cmp -s "$temp/public-repomd.xml" "$repositories/site/packages/rpm/repodata/repomd.xml" &&
       curl --fail --silent --head --location "$site/packages/rpm/$rpm" >/dev/null; then
        echo 'Official Pages RPM repository is deployed.'
        exit 0
    fi
    echo "Waiting for the Pages repository deployment ($attempt/60)."
    sleep 10
done
echo 'Pages did not deploy the matching RPM repository; inspect its deployment workflow.' >&2
exit 1
