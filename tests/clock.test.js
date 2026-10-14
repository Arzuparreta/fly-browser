const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

let now = new Date(2026, 9, 14, 10, 20, 30).getTime();
let labelWrites = 0;
const elements = {};
for (const prefix of ['StatusBar', 'Sidebar']) {
    elements[`${prefix}Clock`] = {style: {}};
    for (const suffix of ['Date', 'Time']) {
        let text = '';
        elements[`${prefix}Clock${suffix}`] = {style: {}, get textContent() {return text;}, set textContent(value) {text = value; labelWrites++;}};
    }
}
const context = {
    Date: class extends Date {constructor() {super(now);}},
    document: {getElementById: id => elements[id]}, window: {},
    setTimeout() {}, setInterval() {}, clearInterval() {}
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../modules/clock.js'), 'utf8').replace(/export /g, ''), context);
context.updateClockUI();
const initialWrites = labelWrites;
now += 1000; context.updateClockUI();
assert.equal(labelWrites, initialWrites, 'A seconds tick must not replace unchanged minute/date text nodes');
assert.match(elements.StatusBarClock.title, /10:20:31/);
now += 60000; context.updateClockUI();
assert.equal(labelWrites, initialWrites + 2, 'A new minute updates both visible times');
context.cycleClockFormat();
assert.equal(elements.SidebarClockDate.style.display, 'none');
context.cycleClockFormat();
const secondsWrites = labelWrites;
now += 1000; context.updateClockUI();
assert.equal(labelWrites, secondsWrites + 2, 'Seconds format keeps its one-second updates');
context.setClockVisibility(false);
const hiddenWrites = labelWrites;
now += 120000; context.updateClockUI();
assert.equal(labelWrites, hiddenWrites, 'Disabled clocks must not update hidden labels');
context.setClockVisibility(true);
assert.match(elements.SidebarClockTime.textContent, /^10:23:/, 'Re-enabling the clock updates it immediately');
console.log('Clock ticks preserve unchanged labels, formats and disabled-clock behaviour.');
