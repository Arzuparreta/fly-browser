const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
 const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-live-'));
 const wrapper = path.join(temp, 'start.cjs');
 fs.writeFileSync(wrapper, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temp)}); app.setPath('userData',${JSON.stringify(temp+'/data')}); require(${JSON.stringify(root+'/main.js')});`);
 const server = http.createServer((req,res) => {res.end(req.url === '/frame' ? '<!doctype html><input id="framed">' : '<!doctype html><title>Keyboard fixture</title><input id="text" style="margin-left:260px"><news-text tabindex="0" id="noneditable">Article</news-text><iframe src="/frame" style="display:block"></iframe><div style="height:9000px">Scroll test</div><button id="target">Hint target</button>');});
 await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
 const fixture = `http://127.0.0.1:${server.address().port}`;
 let app;
 try {
  app = await _electron.launch({executablePath: root+'/node_modules/electron/dist/electron', args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, wrapper], timeout: 30000});
  const page = await app.firstWindow();
  const errors=[]; page.on('pageerror',error=>errors.push(error.message));
  await page.waitForFunction(() => !!window.executeBrowserAction && document.querySelectorAll('.keyboard-row').length > 0);
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('Control+h');
  await page.locator('#PreferencesOverlay').waitFor({state:'visible'});
  assert.equal(await page.locator('#OpenKeyboardSettings').count(), 0);
  await page.locator('#SettingsTab-keyboard').click();
  await page.locator('#KeyboardSearch').fill('workspace dashboard');
  assert.equal(await page.locator('.keyboard-row details').count(), 2);
  assert.match(await page.locator('details[data-keyboard-section=shortcuts] summary').textContent(), /Ctrl\+Shift\+W/);
  assert.match(await page.locator('details[data-keyboard-section=normal] summary').textContent(), /: w$/);
  await page.locator('#KeyboardSearch').fill('new blank');
  assert.equal(await page.locator('.keyboard-row').count(), 1);
  await page.locator('details[data-keyboard-section=shortcuts] summary').click();
  await page.locator('details[data-keyboard-section=shortcuts]').getByRole('button',{name:'Add binding for New Blank Tab',exact:true}).click();
  await page.keyboard.press('Control+w');
  await page.locator('#KeyboardRecordError').filter({hasText:'conflicts'}).waitFor();
  assert(await page.locator('#KeyboardRecordReplace').isVisible());
  await page.keyboard.press('Escape');
  assert(!(await page.locator('#KeyboardRecorder').isVisible()));
  await page.locator('details[data-keyboard-section=shortcuts]').getByRole('button',{name:'Add binding for New Blank Tab',exact:true}).click();
  await page.keyboard.press('Control+Alt+b');
  await page.locator('#KeyboardRecordUse').click();
  await page.locator('#KeyboardSave').click();
  await page.locator('#KeyboardStatus').filter({hasText:'saved and applied'}).waitFor();
  const config=await page.evaluate(()=>window.miseAPI.getKeyboardConfig());
  assert(config.shortcuts['spawn-tab'].includes('Ctrl+Alt+B'));
  await page.locator('#KeyboardSearch').fill('scroll down');
  await page.locator('details[data-keyboard-section=normal] summary').click();
  await page.getByRole('button',{name:'Change j for Scroll down',exact:true}).click();
  await page.keyboard.press('z'); await page.locator('#KeyboardRecordUse').click();
  await page.locator('#KeyboardSave').click();
  await page.locator('#KeyboardStatus').filter({hasText:'saved and applied'}).waitFor();
  if (process.env.MISE_KEYBOARD_SCREENSHOT) await page.screenshot({path:process.env.MISE_KEYBOARD_SCREENSHOT});
  // Record a page sequence and verify the draft can be discarded without changing live bindings.
  await page.locator('#KeyboardSearch').fill('page top');
  await page.locator('details[data-keyboard-section=normal] summary').click();
  await page.getByRole('button',{name:'Change g g for Go to page top',exact:true}).click();
  await page.locator('#KeyboardSequence').check();
  await page.locator('#KeyboardRecorded').focus();
  await page.keyboard.press('q'); await page.keyboard.press('q');
  await page.locator('#KeyboardRecordUse').click();
  assert(!(await page.locator('#KeyboardSave').isDisabled()));
  await page.locator('#KeyboardDiscard').click();
  assert.deepEqual((await page.evaluate(()=>window.miseAPI.getKeyboardConfig())).normal['page-top'], ['G G']);
  await page.locator('#CloseSettingsBtn').focus();
  await page.keyboard.press('Shift+Tab');
  assert(await page.evaluate(()=>document.getElementById('PreferencesOverlay').contains(document.activeElement)));
  await page.locator('#KeyboardSearch').focus();
  await page.keyboard.press('Escape');
  await page.evaluate(url=>{const view=[...document.querySelectorAll('webview')].find(view=>view.getClientRects().length); return view.loadURL(url);},fixture);
  await page.waitForTimeout(600);
  const guest=await app.evaluate(({webContents})=>webContents.getAllWebContents().find(w=>w.getType()==='webview' && w.getURL().startsWith('http://127.0.0.1')).id);
  await page.locator('webview:visible').click({position:{x:280,y:20}});
  await page.keyboard.type('z');
  const typed=await app.evaluate(({webContents},id)=>webContents.fromId(id).executeJavaScript('document.getElementById("text").value'),guest);
  assert.equal(typed.toLowerCase(),'z');
  await page.keyboard.press('Escape');
  await page.keyboard.press('z');
  await page.waitForTimeout(250);
  const scroll=await app.evaluate(({webContents},id)=>webContents.fromId(id).executeJavaScript('scrollY'),guest);
  assert(scroll>0,'Custom page binding must scroll the live guest');
  // Native interception honours passthrough and reaches a focused guest.
  await page.keyboard.press('Shift+Escape');
  const before = await app.evaluate(({webContents},id)=>webContents.fromId(id).executeJavaScript('scrollY'),guest);
  await page.keyboard.press('z'); await page.waitForTimeout(150);
  assert.equal(await app.evaluate(({webContents},id)=>webContents.fromId(id).executeJavaScript('scrollY'),guest),before);
  await page.keyboard.press('Shift+Escape');
  await page.keyboard.press('Control+,');
  await page.locator('#PreferencesOverlay').waitFor({state:'visible'});
  assert.equal(await page.evaluate(()=>document.activeElement.id),'KeyboardSearch');
  // Invalid imports never alter the current profile.
  await page.locator('#KeyboardImport').setInputFiles({name:'invalid.json', mimeType:'application/json',buffer:Buffer.from('{')});
  await page.locator('#KeyboardStatus').filter({hasText:/JSON|Unexpected|property name/}).waitFor();
  assert.equal((await page.evaluate(()=>window.miseAPI.getKeyboardConfig())).normal['scroll-down'][0],'Z');
  await page.locator('#CloseSettingsBtn').click();
  await app.evaluate(({webContents},id)=>webContents.fromId(id).executeJavaScript('scrollTo(0,0)'),guest);
  await page.locator('webview:visible').click({position:{x:30,y:55}});
  await app.evaluate(({webContents},id)=>webContents.fromId(id).sendInputEvent({type:'keyDown',keyCode:',',modifiers:['control']}),guest);
  await page.locator('#PreferencesOverlay').waitFor({state:'visible'});
  assert.equal(await page.evaluate(()=>document.activeElement.id),'KeyboardSearch');
  await page.locator('#CloseSettingsBtn').click();
  await page.evaluate(()=>window.executeBrowserAction('toggle-palette'));
  await page.locator('#PaletteInput').fill('customise keyboard controls');
  await page.locator('#PaletteInput').press('Enter');
  await page.locator('#PreferencesOverlay').waitFor({state:'visible'});
  assert.equal(await page.evaluate(()=>document.activeElement.id),'KeyboardSearch');
  await page.locator('#CloseSettingsBtn').click();
  await page.evaluate(()=>window.executeBrowserAction('new-workspace'));
  await page.locator('#NewWorkspaceTitleInput').fill('Transfer target');
  await page.locator('#NewWorkspaceTitleInput').press('Enter');
  await page.evaluate(()=>{window.executeBrowserAction('previous-workspace'); window.executeBrowserAction('tab-9');});
  await page.evaluate(()=>window.executeBrowserAction('move-tab-to-workspace'));
  await page.locator('#WorkspaceMoveDialog').waitFor({state:'visible'});
  await page.locator('#WorkspaceMoveTarget').selectOption('Transfer target');
  await page.getByRole('button',{name:'Move tab',exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('WorkspaceLabel').textContent === 'Transfer target');
  assert.equal(await page.locator('webview:visible').getAttribute('partition'),'persist:transfer-target');
  assert(await app.evaluate(({webContents},id)=>!webContents.fromId(id),guest),'The old container webview must be destroyed');
  // Native numbered shortcuts select tabs without opening extension actions.
  for (let count = 2; count <= 3; count++) {
   await page.evaluate(() => window.executeBrowserAction('spawn-tab'));
   await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count, count);
  }
  const tabCount = await page.locator('#TabList li').count();
  for (let number = 1; number <= 9; number++) {
   await page.locator('#menu-btn').focus();
   await page.keyboard.press(`Alt+${number}`);
   const expected = number === 9 ? tabCount - 1 : Math.min(number - 1, tabCount - 1);
   await page.waitForFunction(index => [...document.querySelectorAll('#TabList li')].findIndex(tab => tab.classList.contains('selected')) === index, expected);
   assert(!(await page.locator('#CommandPaletteOverlay').isVisible()), `Alt+${number} must not open the command palette`);
  }
  await page.evaluate(() => window.executeBrowserAction('new-workspace'));
  await page.locator('#NewWorkspaceTitleInput').fill('Empty numbered shortcut fixture');
  await page.locator('#NewWorkspaceTitleInput').press('Enter');
  await page.locator('#EmptyWorkspaceWelcome').waitFor({state:'visible'});
  await page.keyboard.press('Alt+1');
  assert(!(await page.locator('#CommandPaletteOverlay').isVisible()), 'Numbered shortcuts in an empty workspace must not open the command palette');
  await page.evaluate(() => window.executeBrowserAction('open-extension-actions'));
  await page.locator('#CommandPaletteOverlay').waitFor({state:'visible'});
  await page.keyboard.press('Escape');
  // waitForFunction does not await async predicates, so guest state is polled from here.
  const until = async (check, message) => {
   for (let attempt = 0; attempt < 250; attempt++) { if (await check().catch(() => false)) return; await page.waitForTimeout(20); }
   throw new Error(message);
  };
  const activeScript = script => page.evaluate(script => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript(script), script);
  // Clicking a webview moves host focus to it asynchronously; keys sent earlier reach the host body.
  const clickGuest = async position => {
   await page.locator('webview:visible').click({position});
   await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
  };
  // Repeated close keys remain available after a tab closes, and on sidebar controls.
  for (let count = 1; count <= 4; count++) {
   await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), fixture);
   await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count, count);
   await until(() => page.evaluate(() => {
    const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
    return view && !view.isLoading() && view.executeJavaScript('!!document.getElementById("text")');
   }), 'Fixture tab did not load');
  }
  // A custom element containing "text" in its name is not itself a text field.
  await clickGuest({x:350,y:350});
  await activeScript('document.getElementById("noneditable").focus()');
  await page.keyboard.press('x');
  await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 3);
  await until(() => activeScript('document.hasFocus()'), 'The next tab did not receive focus');
  await clickGuest({x:350,y:350});
  await clickGuest({x:280,y:20});
  await until(() => activeScript('document.activeElement.id === "text"'), 'The fixture field did not receive focus');
  await page.keyboard.press('x');
  await until(async () => await activeScript('document.getElementById("text").value') === 'x', 'Typing x must edit the field');
  assert.equal(await page.locator('#TabList li').count(), 3);
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => document.getElementById('StatusBarMode').textContent === 'NORMAL');
  await page.keyboard.press('x');
  await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 2);
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('i');
  await page.waitForFunction(() => document.getElementById('StatusBarMode').textContent === 'INSERT');
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => document.getElementById('StatusBarMode').textContent === 'NORMAL');
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('x');
  await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 1);
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('w');
  await page.locator('#DashboardOverlay').waitFor({state:'visible'});
  await page.keyboard.press('w');
  assert.equal(await page.locator('#TabList li').count(), 1);
  await page.keyboard.press('Escape');
  await page.keyboard.press('Shift+Escape');
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('x');
  assert.equal(await page.locator('#TabList li').count(), 1, 'Passthrough must protect page bindings in browser controls too');
  await page.keyboard.press('Shift+Escape');
  await page.locator('#menu-btn').focus();
  await page.keyboard.press('x');
  await page.locator('#EmptyWorkspaceWelcome').waitFor({state:'visible'});
  console.log('Live Electron keyboard editor and guest input passed.', {errors,scroll});
  assert.deepEqual(errors,[]);
 } finally { if(app) await app.close(); server.close(); fs.rmSync(temp,{recursive:true,force:true}); }
})().catch(error=>{console.error(error);process.exitCode=1;});
