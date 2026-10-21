/* Browser checks against tools/preview_web.py. Requires Playwright and Chromium.
 * UI_PREVIEW_URL=http://127.0.0.1:8766 CHROMIUM_PATH=/usr/bin/chromium node tools/test_web_ui.js
 */
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const path = require('node:path');
(async () => {
  let browser,server;
  try {
    let url=process.env.UI_PREVIEW_URL;
    if (!url) {
      server=spawn(process.env.PYTHON || 'python3',[path.join(__dirname,'preview_web.py'),'--port','0']);
      url=await new Promise((resolve,reject)=>{
        const timeout=setTimeout(()=>reject(new Error('Preview server did not start')),10000);
        server.on('error',error=>{clearTimeout(timeout);reject(error);});
        server.stdout.on('data',data=>{
          const match=String(data).match(/http:\/\/127\.0\.0\.1:\d+/);
          if (match) {clearTimeout(timeout);resolve(match[0]);}
        });
      });
    }
    browser = await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_PATH || undefined,args:['--no-sandbox']});
    const context = await browser.newContext({baseURL:url,httpCredentials:{username:'preview',password:'preview-only'},viewport:{width:1365,height:1000},timezoneId:'America/Santiago'});
    await context.request.post('/preview/scenario',{data:{scenario:'normal'}});
    const page = await context.newPage();
    const errors=[]; page.on('pageerror',error=>errors.push(error.message));
    await page.goto(url);
    await page.locator('#glucose').filter({hasText:'118'}).waitFor();
    assert.equal(await page.locator('#dial').getAttribute('data-tone'),'normal');
    assert.ok((await page.locator('#history-path').getAttribute('d')).startsWith('M'));
    await page.screenshot({path:'/tmp/nightscout-dashboard-desktop.png',fullPage:true});
    await page.setViewportSize({width:390,height:844});
    await page.screenshot({path:'/tmp/nightscout-dashboard-mobile.png',fullPage:true});
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    for (const [scenario,tone] of [['low','low'],['high','high'],['stale','old'],['clock','clock'],['empty','empty'],['error','error'],['untrusted-text','normal']]) {
      await context.request.post('/preview/scenario',{data:{scenario}});
      const response=page.waitForResponse(r=>r.url().endsWith('/api/state'));
      await page.locator('#refresh').click(); await response;
      await page.waitForFunction(expected=>document.getElementById('dial').dataset.tone===expected,tone);
      if (scenario==='empty') assert.equal(await page.locator('#glucose').textContent(),'---');
      if (scenario==='clock') assert.equal(await page.locator('#age').textContent(),'Hora sin sincronizar');
      if (scenario==='untrusted-text') assert.equal(await page.locator('#server-status img').count(),0);
    }
    const previous=await page.locator('#glucose').textContent();
    await context.request.post('/preview/scenario',{data:{scenario:'unreachable'}});
    await page.locator('#refresh').click();
    await page.locator('#network-notice').waitFor({state:'visible'});
    assert.equal(await page.locator('#glucose').textContent(),previous);
    assert.equal(await page.locator('#dial').getAttribute('data-tone'),'error');
    assert.deepEqual(errors,[]);
    console.log('Browser checks passed: desktop/mobile, 7 reading states, retained offline reading, safe text rendering.');
  } finally {if (browser) await browser.close();if(server) server.kill();}
})().catch(error=>{console.error(error);process.exitCode=1;});
