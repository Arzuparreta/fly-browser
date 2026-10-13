// tests/search-aliases.test.js
// Regression test suite for built-in and user-customisable search aliases

const assert = require('assert');
const { 
    DEFAULT_SEARCH_ALIASES, 
    getEffectiveSearchAliases, 
    resolveSearchAliasUrl 
} = require('../modules/overlays/searchEngine.js');
const keybinds = require('../keybinds.js');

console.log('Running Mise search aliases and custom bangs regression tests...');

// 1. Verify default built-in search aliases
{
    const expectedDefaults = ['g', 'ddg', 'yt', 'gh', 'a', 'aur', 'pkg', 'so', 'r', 'sp', 'b', 'k'];
    expectedDefaults.forEach(alias => {
        assert(DEFAULT_SEARCH_ALIASES[alias], `Default alias '${alias}' must be defined`);
        assert(DEFAULT_SEARCH_ALIASES[alias].url, `Default alias '${alias}' must have a target URL`);
        assert(DEFAULT_SEARCH_ALIASES[alias].name, `Default alias '${alias}' must have a descriptive name`);
    });
    console.log('✓ Default built-in search aliases verified');
}

// 2. Verify query resolution with %s placeholder
{
    const googleUrl = resolveSearchAliasUrl('g', 'mise browser');
    assert.strictEqual(googleUrl, 'https://www.google.com/search?q=mise%20browser', 'Google alias must resolve query with %s');

    const aurUrl = resolveSearchAliasUrl('aur', 'hyprland-git');
    assert.strictEqual(aurUrl, 'https://aur.archlinux.org/packages?K=hyprland-git', 'AUR alias must resolve query with %s');

    const ghUrl = resolveSearchAliasUrl('gh', 'antigravity cli');
    assert.strictEqual(ghUrl, 'https://github.com/search?q=antigravity%20cli', 'GitHub alias must resolve query with %s');

    console.log('✓ Standard %s placeholder query resolution verified');
}

// 3. Verify special character encoding in search queries
{
    const specialUrl = resolveSearchAliasUrl('gh', 'c++ & rust / web');
    assert.strictEqual(specialUrl, 'https://github.com/search?q=c%2B%2B%20%26%20rust%20%2F%20web', 'Special characters must be percent-encoded');

    console.log('✓ Special character query encoding verified');
}

// 4. Verify case-insensitivity of aliases
{
    const upperG = resolveSearchAliasUrl('G', 'linux');
    assert.strictEqual(upperG, 'https://www.google.com/search?q=linux', 'Uppercase alias must resolve identically');

    const upperAur = resolveSearchAliasUrl('AUR', 'neovim');
    assert.strictEqual(upperAur, 'https://aur.archlinux.org/packages?K=neovim', 'Uppercase AUR alias must resolve identically');

    console.log('✓ Case-insensitive alias matching verified');
}

// 5. Verify custom alias addition and resolution
{
    const customConfig = {
        nix: 'https://search.nixos.org/packages?query=%s',
        mdn: 'https://developer.mozilla.org/en-US/search?q=%s',
        w: { name: 'Wikipedia', url: 'https://en.wikipedia.org/wiki/Special:Search?search=%s' }
    };

    const effective = getEffectiveSearchAliases(customConfig);
    assert(effective.nix, 'Custom alias nix must exist in effective aliases');
    assert(effective.mdn, 'Custom alias mdn must exist in effective aliases');
    assert(effective.w, 'Custom alias w must exist in effective aliases');
    assert.strictEqual(effective.w.name, 'Wikipedia', 'Custom alias object name must be preserved');

    const nixUrl = resolveSearchAliasUrl('nix', 'ripgrep', customConfig);
    assert.strictEqual(nixUrl, 'https://search.nixos.org/packages?query=ripgrep', 'Custom alias nix must resolve query');

    const mdnUrl = resolveSearchAliasUrl('mdn', 'fetch api', customConfig);
    assert.strictEqual(mdnUrl, 'https://developer.mozilla.org/en-US/search?q=fetch%20api', 'Custom alias mdn must resolve query');

    console.log('✓ Custom search alias additions verified');
}

// 6. Verify custom alias override of built-in alias
{
    const overrideConfig = {
        g: 'https://google.co.uk/search?q=%s'
    };

    const overriddenUrl = resolveSearchAliasUrl('g', 'weather london', overrideConfig);
    assert.strictEqual(overriddenUrl, 'https://google.co.uk/search?q=weather%20london', 'User custom alias must override built-in alias');

    console.log('✓ User custom alias override of built-in verified');
}

// 7. Verify fallback when template omits %s placeholder
{
    const appendOnlyConfig = {
        custom: 'https://example.com/search?query='
    };

    const resolved = resolveSearchAliasUrl('custom', 'testing term', appendOnlyConfig);
    assert.strictEqual(resolved, 'https://example.com/search?query=testing%20term', 'Must append encoded query if %s omitted');

    console.log('✓ Fallback query string append verified');
}

// 8. Verify invalid or empty alias inputs return null
{
    assert.strictEqual(resolveSearchAliasUrl('', 'query'), null, 'Empty alias must return null');
    assert.strictEqual(resolveSearchAliasUrl('unknownalias123', 'query'), null, 'Unregistered alias must return null');
    assert.strictEqual(resolveSearchAliasUrl('g', ''), null, 'Empty query must return null');
    assert.strictEqual(resolveSearchAliasUrl(null, 'query'), null, 'Null alias must return null');

    console.log('✓ Input validation and null returns for invalid inputs verified');
}

// 9. Keybinding verification (Strictly no Alt modifier)
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    for (const [action, keys] of Object.entries(defaults)) {
        const list = Array.isArray(keys) ? keys : [keys];
        for (const key of list) {
            assert(!key.includes('Alt'), `Keybinding '${key}' for action '${action}' must not contain Alt modifier`);
        }
    }
    console.log('✓ Verified no default keybindings use Alt modifier');
}

console.log('\nAll search aliases and custom bangs regression tests passed successfully!');
