// Optional acceptance on an owned X11 display. Uses a disposable profile.
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
(async () => {
    if (evidence) fs.mkdirSync(evidence,{recursive:true});
    for (const scale of ['normal','large']) {
        const home = fs.mkdtempSync(path.join(os.tmpdir(),'fly-quiet-layout-'));
        const profile = path.join(home,'profile'); fs.mkdirSync(profile);
        fs.writeFileSync(path.join(profile,'Local State'),JSON.stringify({fly:{preferences:{schema:1,values:{interface_scale:scale}}}}));
        let child, browser;
        try {
            child=spawn(binary,['--fly-native',`--user-data-dir=${profile}`,
                '--test-type','--no-first-run','--ozone-platform=x11',
                '--password-store=basic','--remote-debugging-port=0'],
                {env:{...process.env,HOME:home,XDG_CONFIG_HOME:path.join(home,'.config'),XDG_CACHE_HOME:path.join(home,'.cache')},stdio:'ignore'});
            const portFile=path.join(profile,'DevToolsActivePort');
            for(let i=0;i<1200&&!fs.existsSync(portFile);i++) { assert.equal(child.exitCode,null); await wait(100); }
            assert(fs.existsSync(portFile),'No native debugging port');
            browser=await chromium.connectOverCDP({endpointURL:'http://127.0.0.1:'+fs.readFileSync(portFile,'utf8').split('\n')[0],noDefaults:true});
            const context=browser.contexts()[0];
            const page=context.pages().find(p=>p.url().startsWith('chrome://fly-new-tab'));
            assert(page,'Startup opens Quiet');
            const errors=[]; page.on('pageerror',e=>errors.push(e.message));
            await page.waitForSelector('#bookmarks');
            await page.evaluate(()=>document.fonts.ready);
            const data=await page.evaluate(()=>JSON.parse(document.getElementById('initial-data').textContent));
            assert.equal(data.scale,scale==='large'?1.25:1);
            assert.equal(data.bookmarks,undefined);
            assert.equal(data.sites,undefined);
            assert.equal(await page.locator('#memory-fly').evaluate(i=>i.complete&&i.naturalWidth>0),true);
            const cdp=await context.newCDPSession(page);
            for(const [width,height] of sizes) {
                await cdp.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
                await page.evaluate(async()=>{await document.fonts.ready;for(let i=0;i<3;i++)await new Promise(requestAnimationFrame);});
                const state=await page.evaluate(()=>{
                    const root=document.scrollingElement;
                    const offenders=[...document.querySelectorAll('#memory-fly,#search,#bookmarks')].filter(element=>{
                        const r=element.getBoundingClientRect();
                        return r.x< -1||r.y< -1||r.right>innerWidth+1||r.bottom>innerHeight+1||r.width<=0||r.height<=0;
                    }).map(element=>element.id);
                    return {scroll:[root.scrollWidth,root.scrollHeight],offenders,
                        searchLeft:document.getElementById('search').getBoundingClientRect().left};
                });
                report.push({scale,width,height,...state});
                assert(state.scroll[0]<=width+1&&state.scroll[1]<=height+1&&!state.offenders.length,JSON.stringify(report.at(-1)));
                assert(state.searchLeft>=Math.min(data.sidebarOverhang,Math.max(0,width-320))-1,JSON.stringify(report.at(-1)));
                if(evidence&&[[1366,768],[640,480],[360,640],[1200,360]].some(s=>s[0]===width&&s[1]===height))
                    await page.screenshot({path:path.join(evidence,`${scale}-${width}x${height}.png`)});
            }
            assert.deepEqual(errors,[]);
        } finally {
            if(browser) await browser.close().catch(()=>{});
            if(child&&child.exitCode===null)child.kill('SIGTERM');
            if(child)for(let i=0;i<50&&child.exitCode===null;i++)await wait(100);
            if(child&&child.exitCode===null){child.kill('SIGKILL');await wait(200);}
            fs.rmSync(home,{recursive:true,force:true,maxRetries:5,retryDelay:100});
        }
    }
    if(evidence)fs.writeFileSync(path.join(evidence,'layout.json'),JSON.stringify(report,null,2));
    console.log(`${report.length} native Quiet viewport/scale combinations passed.`);
})().catch(error=>{console.error(error);process.exitCode=1;});
