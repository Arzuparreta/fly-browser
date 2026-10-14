const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../renderer.js'), 'utf8');
const start = source.indexOf('        // Recover a trusted key misrouted');
const recovery = source.slice(start, source.indexOf("        if (document.querySelector('dialog[open]')", start));
for (const [key, extra, expected] of [
    ['f', {}, ['keyDown:F', 'char:f', 'keyUp:F']],
    ['Enter', {}, ['keyDown:Enter', 'keyUp:Enter']],
    ['Backspace', {}, ['keyDown:Backspace', 'keyUp:Backspace']],
    ['ArrowLeft', {shiftKey: true}, ['keyDown:Left', 'keyUp:Left']],
    ['l', {ctrlKey: true}, ['keyDown:L', 'keyUp:L']],
    ['f', {targetView: true}, ['keyDown:F', 'char:f', 'keyUp:F']],
    ['Dead', {}, []], ['a', {getModifierState: () => true}, []],
    ['a', {isComposing: true}, []], ['a', {isTrusted: false}, []]
]) {
    const sent = [], calls = [];
    const body = {};
    const view = {tagName: 'WEBVIEW', blur() {calls.push('blur');}, focus() {calls.push('focus');}, sendInputEvent(input) {sent.push(input); return Promise.resolve();}};
    const event = {isTrusted: true, key, target: body, preventDefault() {calls.push('prevent');}, stopImmediatePropagation() {}, ...extra};
    if (extra.targetView) event.target = view;
    vm.runInNewContext('(function () {' + recovery + '})()', {event, document: {body, activeElement: view}});
    assert.deepEqual(sent.map(input => input.type + ':' + input.keyCode), expected);
    assert.equal(calls.includes('prevent'), !!expected.length);
    if (extra.shiftKey) assert(sent.every(input => input.modifiers.includes('shift')));
    if (extra.ctrlKey) assert(sent.every(input => input.modifiers.includes('control')));
}
console.log('Misrouted native key recovery preserves characters, control keys, modifiers and composition guards.');
