#!/usr/bin/env node
// Keep the native catalogue/defaults and cross-language grammar cases derived
// from the current product contract. No browser or engine checkout is needed.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const keyboard = require('../../keyboard');
const keybinds = require('../../keybinds');
const root = path.resolve(__dirname, '../..');
const defaults = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA);
assert(defaults.ok);
const normalize = ['control+shift+a', 'Meta+Ctrl+return', 'Ctrl++', 'Ctrl+Shift+Plus',
    'Ctrl+A+B', 'Ctrl+Ctrl+A', 'Ctrl+ñ', 'ñ', 'Shift+ñ', 'Cmd+X', 'Super+F24',
    'F25', 'F01', ' ', '  Alt + PageDown ', 'Ctrl+\\', 'Shift+/', 'AltGraph+A', '',
    'Ctrl+', 'Shift+Escape', 'Ctrl+💡', 'Shift+ß', '   '];
const configs = [{name:'defaults', input: defaults.config}];
const edit = (name, mutate) => {
    const input = structuredClone(defaults.config);
    mutate(input);
    configs.push({name, input});
};
edit('explicitly disabled reload', c => { c.shortcuts['reload-active-tab'] = []; });
edit('custom tab modifier', c => { c.tabs = 'shift+ctrl'; });
edit('no numbered tabs', c => { c.tabs = ''; });
edit('duplicate binding', c => { c.shortcuts['spawn-tab'] = ['Ctrl+T', 'control+t']; });
edit('shortcut conflict', c => { c.shortcuts['spawn-tab'] = 'Ctrl+W'; });
edit('prefix conflict', c => { c.normal['scroll-up'] = 'G'; });
edit('page shadowed by global', c => { c.normal['spawn-tab'] = 'Ctrl+T'; });
edit('bare global', c => { c.shortcuts['spawn-tab'] = 'A'; });
edit('reserved Escape', c => { c.normal['scroll-down'] = 'Escape'; });
edit('reserved Tab', c => { c.normal['scroll-down'] = 'Shift+Tab'; });
edit('unknown action', c => { c.shortcuts.fake = 'Ctrl+A'; });
edit('numeric binding', c => { c.shortcuts['spawn-tab'] = [42]; });
edit('invalid tab modifier', c => { c.tabs = 'Shift'; });
edit('tab key included', c => { c.tabs = 'Alt+5'; });
edit('no passthrough escape', c => { c.shortcuts['toggle-passthrough'] = []; });
edit('tab selection collision', c => { c.normal['scroll-down'] = ['Alt+1']; });
edit('too long a sequence', c => { c.normal['page-top'] = 'G G G'; });
edit('too many bindings', c => { c.shortcuts['spawn-tab'] = Array(9).fill('Ctrl+T'); });
edit('Unicode layout binding', c => { c.shortcuts['spawn-tab'] = 'Ctrl+ñ'; });
const cases = {
    normalize: normalize.map(input => ({input, output: keyboard.normalise(input)})),
    configs: configs.map(({name, input}) => {
        const result = keyboard.validateConfig(input, keybinds.ACTION_METADATA);
        return {name, input, ok: result.ok, ...(result.ok ? {output: result.config} : {})};
    })
};
// Electron keybinds.json files as the import must read them: values earlier
// versions wrote as defaults are not user choices (keybinds.js storedDefault).
const storedDefaults = JSON.parse(fs.readFileSync(path.join(root, 'stored-keybind-defaults.json'), 'utf8'));
const legacyFiles = [
    {name: 'unversioned former default', input: {'focus-sidebar': 'Ctrl+S'}},
    {name: 'unversioned choice', input: {'focus-sidebar': 'Ctrl+Q', 'spawn-tab': 'Ctrl+Y'}},
    {name: 'unversioned disabled action', input: {'reload-active-tab': ''}},
    {name: 'unversioned retired action', input: {'toggle-notes': 'Ctrl+J', 'jump-to-tab-1': 'Ctrl+1'}},
    {name: 'saved defaults without changesOnly', input: {version: 2, shortcuts: structuredClone(defaults.config.shortcuts),
        normal: structuredClone(defaults.config.normal), tabs: defaults.config.tabs}},
    {name: 'saved file with one change', input: {version: 2, shortcuts: {...structuredClone(defaults.config.shortcuts),
        'spawn-tab': ['Ctrl+Y']}, normal: {'scroll-down': ['N']}}},
    {name: 'changes only', input: {version: 2, changesOnly: true, shortcuts: {'spawn-tab': ['Ctrl+Y']}, normal: {}}},
    {name: 'changes only keeps a former default', input: {version: 2, changesOnly: true,
        shortcuts: {'focus-sidebar': ['Ctrl+S']}, normal: {}}},
    {name: 'choice takes a default chord', input: {version: 2, changesOnly: true,
        shortcuts: {'spawn-tab': ['Ctrl+W']}, normal: {}}},
    {name: 'choice takes the tab chords', input: {version: 2, changesOnly: true,
        shortcuts: {'spawn-tab': ['Alt+1']}, normal: {}}},
    {name: 'page choice takes a default key', input: {version: 2, changesOnly: true,
        shortcuts: {}, normal: {'scroll-up': ['J']}}},
    {name: 'page choice shadows a default sequence', input: {version: 2, changesOnly: true,
        shortcuts: {}, normal: {'scroll-down': ['G']}}},
    {name: 'unsupported version', input: {version: 3, shortcuts: {}, normal: {}}},
];
const legacyKeyboardCases = legacyFiles.map(({name, input}) => {
    const result = keybinds.resolveKeybinds(structuredClone(input));
    return {name, input, ok: result.ok, ...(result.ok ? {output: result.config} : {})};
});
const hinter = fs.readFileSync(path.join(root, 'hinter.js'), 'utf8');
const charsetSource = hinter.match(/const charset = (\[[^;]+\]);/);
const generatorSource = hinter.match(/function generateHintCodes\(count, alphabet\) \{[\s\S]*?return \{ codes, codeLength: len \};\s*\}/);
assert(charsetSource && generatorSource, 'Baseline hint contract must remain discoverable');
const hintAlphabet = vm.runInNewContext(charsetSource[1], Object.create(null), {timeout: 1000}).join('');
assert(/^[a-z]+$/.test(hintAlphabet) && new Set(hintAlphabet).size === hintAlphabet.length && hintAlphabet.length >= 2);
const hintCases = [1, 12, 13, 144, 145, 500].map(count => ({count, ...JSON.parse(JSON.stringify(
    vm.runInNewContext('(' + generatorSource[0] + ')(' + count + ',' + JSON.stringify([...hintAlphabet]) + ')',
        Object.create(null), {timeout: 1000})))}));
const websiteThemeSource = fs.readFileSync(path.join(root, 'main.js'), 'utf8')
    .match(/function websiteTheme\(cfg\) \{[\s\S]*?\n\}/);
assert(websiteThemeSource, 'Baseline website theme contract must remain discoverable');
const websiteThemeCases = ['dark', 'light'].flatMap(theme => [true, false].map(sync => {
    const input = {theme, sync_website_theme: sync};
    return {input, expected: vm.runInNewContext('(' + websiteThemeSource[0] + ')(' + JSON.stringify(input) + ')',
        Object.create(null), {timeout: 1000})};
}));
const paletteEntries = JSON.parse(fs.readFileSync(path.join(root, 'chromium/baseline-contracts.json'), 'utf8')).paletteEntries;
assert(paletteEntries.every(item => keybinds.ACTION_METADATA[item.actionId]));
const literal = value => `R"fly_json(${JSON.stringify(value)})fly_json"`;
const result = `// Generated by scripts/chromium/generate-commands.js. Do not edit.
#ifndef FLY_COMMANDS_GENERATED_CONTRACT_H_
#define FLY_COMMANDS_GENERATED_CONTRACT_H_
#include <string_view>
namespace fly {
inline constexpr std::string_view kActionMetadataJSON = ${literal(keybinds.ACTION_METADATA)};
inline constexpr std::string_view kDefaultKeyboardJSON = ${literal(defaults.config)};
inline constexpr std::string_view kKeyboardCasesJSON = ${literal(cases)};
inline constexpr std::string_view kStoredKeyboardDefaultsJSON = ${literal(storedDefaults)};
inline constexpr std::string_view kLegacyKeyboardCasesJSON = ${literal(legacyKeyboardCases)};
inline constexpr std::string_view kWebsiteThemeCasesJSON = ${literal(websiteThemeCases)};
inline constexpr std::string_view kPaletteEntriesJSON = ${literal(paletteEntries)};
inline constexpr std::string_view kHintAlphabet = ${JSON.stringify(hintAlphabet)};
inline constexpr std::string_view kHintCasesJSON = ${literal(hintCases)};
}  // namespace fly
#endif  // FLY_COMMANDS_GENERATED_CONTRACT_H_
`;
const output = path.join(root, 'chromium/overlay/fly/commands/generated_contract.h');
if (process.argv.includes('--check')) {
    if (!fs.existsSync(output) || fs.readFileSync(output, 'utf8') !== result) {
        console.error('Native command contracts are stale. Run node scripts/chromium/generate-commands.js');
        process.exitCode = 1;
    }
} else fs.writeFileSync(output, result);
