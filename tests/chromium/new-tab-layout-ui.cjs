// Optional native WebUI acceptance. Run on an owned X11 display with
// FLY_NATIVE_BINARY=/path/to/out/FlyRelease/chrome and FLY_PLAYWRIGHT_MODULE.
// Tests desktop viewport sizes, not device emulation. Never imports real data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {chromium} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const binary = process.env.FLY_NATIVE_BINARY;
assert(binary, 'Set FLY_NATIVE_BINARY to a built native Fly browser');
const evidence = process.env.FLY_LAYOUT_EVIDENCE;
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const sizes = [[1920,1080],[1366,768],[1280,720],[1024,600],[800,600],[640,480],
    [480,640],[360,640],[320,480],[320,240],[480,240],[800,300],
    [1200,360],[2560,600],[900,1600],[240,480]];
const report = [];
async function settled(page) {
    await page.evaluate(async () => {
        await document.fonts.ready;
        for (let i=0;i<6;i++) await new Promise(requestAnimationFrame);
    });
}
async function geometry(page) {
    return page.evaluate(() => {
        const viewport = {width:innerWidth, height:innerHeight};
        const root = document.scrollingElement;
        const offenders = [];
        const visible = element => element.getClientRects().length && getComputedStyle(element).visibility !== 'hidden';
        for (const element of document.querySelectorAll('#clock,#date,.collection-header,.empty,.entries a,.entries button,.paging button,.annotation,footer,.specimen')) {
            if (!visible(element)) continue;
            const r = element.getBoundingClientRect();
            if (r.x < -1 || r.y < -1 || r.right > innerWidth+1 || r.bottom > innerHeight+1 || r.width <= 0 || r.height <= 0)
                offenders.push({element:element.id || element.className || element.tagName, bounds:r.toJSON()});
        }
        for (const list of document.querySelectorAll('.entries')) {
            if (!visible(list)) continue;
            if (list.scrollHeight > list.clientHeight+1 || list.scrollWidth > list.clientWidth+1)
                offenders.push({element:list.id, listOverflow:[list.scrollWidth,list.scrollHeight], allocation:[list.clientWidth,list.clientHeight]});
        }
        return {viewport, scroll:[root.scrollWidth,root.scrollHeight], offenders,
            clockSize:getComputedStyle(document.getElementById('clock')).fontSize,
            scale:getComputedStyle(document.documentElement).zoom};
    });
}
(async () => {
    if (evidence) fs.mkdirSync(evidence,{recursive:true});
    for (const scale of ['normal','large']) {
        const home = fs.mkdtempSync(path.join(os.tmpdir(),'fly-newtab-layout-'));
        const profile = path.join(home,'profile'); fs.mkdirSync(profile);
        fs.writeFileSync(path.join(profile,'Local State'),JSON.stringify({fly:{preferences:{schema:1,values:{interface_scale:scale}}}}));
        let child, browser;
        try {
            child=spawn(binary,['--fly-native',`--user-data-dir=${profile}`,
                '--fly-import-from='+path.join(home,'nonexistent-source'),'--test-type','--no-first-run',
                '--ozone-platform=x11','--password-store=basic','--remote-debugging-port=0'],
                {env:{...process.env,HOME:home,XDG_CONFIG_HOME:path.join(home,'.config'),XDG_CACHE_HOME:path.join(home,'.cache')},stdio:'ignore'});
            const portFile=path.join(profile,'DevToolsActivePort');
            for(let i=0;i<1200&&!fs.existsSync(portFile);i++) { assert.equal(child.exitCode,null); await wait(100); }
            assert(fs.existsSync(portFile),'Native browser did not expose its debugging port within 120 seconds');
            browser=await chromium.connectOverCDP({endpointURL:'http://127.0.0.1:'+fs.readFileSync(portFile,'utf8').split('\n')[0],noDefaults:true});
            const context=browser.contexts()[0];
            const page=context.pages().find(p=>p.url().startsWith('chrome://fly-new-tab'));
            assert(page,'Native startup opens its new tab WebUI');
            const errors=[]; page.on('pageerror',e=>errors.push(e.message));
            await page.waitForFunction(()=>document.getElementById('memory').textContent.endsWith(' MB'));
            assert.equal(await page.evaluate(()=>document.documentElement.style.getPropertyValue('--scale')),scale==='large'?'1.25':'1');
            // Freeze only the list/theme fixture; memory and the native window
            // keep running. The actual native interface scale was loaded above.
            const data=await page.evaluate(()=>new Promise(resolve=>{
                const render=window.updateFlyNewTab;
                window.updateFlyNewTab=d=>{ render(d); window.renderLayoutFixture=render; window.updateFlyNewTab=()=>{}; resolve(d); };
                chrome.send('ready');
            }));
            const cdp=await context.newCDPSession(page);
            // First test the actual host viewport, before any size override.
            await settled(page);
            assert.equal(await page.locator('#memory-fly').evaluate(image=>image.complete && image.naturalWidth>0),true);
            assert.equal(await page.evaluate(()=>document.fonts.check('16px "IBM Plex Sans"') && document.fonts.check('16px "IBM Plex Mono"') && document.fonts.check('800 32px "Doto"')),true);
            const host=await geometry(page);
            assert(host.scroll[1]<=host.viewport.height+1 && !host.offenders.length,JSON.stringify({scale,host}));
            for (const [width,height] of sizes) {
                await cdp.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
                for(const count of [0,6,37]) {
                    await page.evaluate(({data,count})=>window.renderLayoutFixture({...data,
                        showBookmarks:true,showSites:true,
                        bookmarks:Array.from({length:count},(_,i)=>({url:`https://bookmark-${i}.example/`,title:`Saved page ${i} — ${'long title '.repeat(8)}`})),
                        sites:count?Array.from({length:8},(_,i)=>({url:`https://frequent-${i}.example/`,title:`frequent-${i}.example`})) : []}),{data,count});
                    await settled(page);
                    if(!count) {
                        assert.equal(await page.locator('#bookmarks a').count(),0);
                        assert.equal(await page.locator('#sites a').count(),0);
                    }
                    const state=await geometry(page);
                    report.push({scale,width,height,count,...state});
                    assert(state.scroll[0]<=width+1 && state.scroll[1]<=height+1 && !state.offenders.length,JSON.stringify(report.at(-1)));
                    if(evidence && count===6 && [[1366,768],[640,480],[360,640],[1200,360]].some(s=>s[0]===width&&s[1]===height))
                        await page.screenshot({path:path.join(evidence,`${scale}-${width}x${height}.png`)});
                }
            }
            // Every saved destination remains reachable with no list scrolling.
            await cdp.send('Emulation.setDeviceMetricsOverride',{width:640,height:480,deviceScaleFactor:1,mobile:false});
            await settled(page);
            const seen=new Set();
            for(let i=0;i<100;i++) {
                const urls=await page.locator('#bookmarks a').evaluateAll(links=>links.map(a=>a.href));
                urls.forEach(url=>seen.add(url));
                const next=page.getByRole('button',{name:'Next bookmarks page',exact:true});
                if(await next.isDisabled()) break;
                await next.click(); await settled(page);
                const state=await geometry(page); assert(!state.offenders.length,JSON.stringify(state));
            }
            assert.equal(seen.size,37);
            await page.getByRole('button',{name:'Previous bookmarks page',exact:true}).focus();
            await page.keyboard.press('Enter'); await settled(page);
            assert.equal(await page.getByRole('button',{name:'Previous bookmarks page',exact:true}).evaluate(b=>b===document.activeElement),true);
            // Hidden/private sections and live theme changes share the canvas.
            await page.evaluate(data=>window.renderLayoutFixture({...data,light:true,showBookmarks:false,showSites:false}),data);
            await settled(page); assert.equal(await page.locator('.collection:visible').count(),0);
            assert(!(await geometry(page)).offenders.length);
            assert.deepEqual(errors,[]);
        } finally {
            if(browser) await browser.close().catch(()=>{});
            if(child && child.exitCode===null) child.kill('SIGTERM');
            if(child) for(let i=0;i<50&&child.exitCode===null;i++) await wait(100);
            if(child && child.exitCode===null) { child.kill('SIGKILL'); await wait(200); }
            fs.rmSync(home,{recursive:true,force:true,maxRetries:5,retryDelay:100});
        }
    }
    if(evidence) fs.writeFileSync(path.join(evidence,'layout.json'),JSON.stringify(report,null,2));
    console.log(`${report.length} native viewport/scale/content combinations passed; pagination, keyboard access, private layout and assets checked.`);
})().catch(error=>{ console.error(error); if(evidence)fs.writeFileSync(path.join(evidence,'layout-failure.json'),JSON.stringify(report,null,2));process.exitCode=1; });
