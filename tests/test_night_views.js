/* Production C++ night form and local assets, with intercepted requests. No server. */
const assert=require('assert/strict');
const fs=require('fs'),path=require('path');
const {execFileSync}=require('child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync('python3',[path.join(root,'tests/render_setup_page.py'),'night'],{encoding:'utf8'});
(async()=>{
  const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try {
    const page=await browser.newPage({viewport:{width:390,height:844},locale:'es-CL',timezoneId:'America/Santiago'});
    await page.clock.install();
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    let posts=[],status=200,offline=false,pending=false,release;
    await page.route('http://device.test/**',async route=>{
      const req=route.request(),url=new URL(req.url());
      if(req.method()==='POST'){
        assert.equal(url.pathname,'/night-display');
        const fields=Object.fromEntries(new URLSearchParams(req.postData()));posts.push(fields);
        assert.equal(fields.csrf,'fixture-csrf-only');
        if(offline)return route.abort('connectionreset');
        if(pending)await new Promise(resolve=>{release=resolve;});
        if(fields.preview==='1')return route.fulfill({status,contentType:'application/json',body:status===200?' {"ok":true,"seconds":5}':'Rejected'});
        return route.fulfill({contentType:'text/html',body:'<h1>Horario guardado</h1>'});
      }
      const asset={'/assets/ui.css':['ui.css','text/css'],'/assets/night.js':['night.js','application/javascript']}[url.pathname];
      return asset?route.fulfill({contentType:asset[1],body:fs.readFileSync(path.join(root,'web',asset[0]))}):route.fulfill({contentType:'text/html',body:html});
    });
    const field=name=>page.locator(`[name="${name}"]`);
    async function slider(name,value){await field(name).evaluate((e,v)=>{e.value=v;e.dispatchEvent(new Event('input',{bubbles:true}));},String(value));}
    async function preview(){await page.locator('#night-preview').click();await page.waitForFunction(()=>!document.getElementById('night-preview').disabled);}
    await page.goto('http://device.test/settings');
    assert.equal(await field('enabled').isChecked(),false);
    assert.equal(await field('start').inputValue(),'22:00');assert.equal(await field('end').inputValue(),'07:00');
    assert((await page.locator('#night-display-form').textContent()).includes('La luz de fondo sigue encendida'));
    await field('enabled').check();await slider('day',80);await slider('night',20);
    assert.equal(await page.locator('#night-level-value').textContent(),'20 %');
    await field('end').fill('22:00');await page.locator('#night-preview').click();assert.equal(posts.length,0);
    await field('end').fill('07:00');
    await preview();assert.equal(posts.length,1);
    assert.deepEqual(posts.at(-1),{csrf:'fixture-csrf-only',enabled:'1',start:'22:00',end:'07:00',zone:'0',day:'80',night:'20',preview:'1',quick:'1'});
    assert((await page.locator('#night-feedback').textContent()).includes('20 %'));
    await page.clock.fastForward(5001);assert((await page.locator('#night-feedback').textContent()).includes('terminada'));
    for(status of [400,403,500]){
      await preview();assert((await page.locator('#night-feedback').textContent()).includes(status===403?'caducó':'No se pudo'));
      assert.equal(await field('night').inputValue(),'20');
    }
    status=200;offline=true;await preview();assert((await page.locator('#night-feedback').textContent()).includes('Sin conexión'));offline=false;
    pending=true;const count=posts.length;await page.locator('#night-preview').click();
    await page.waitForFunction(()=>document.getElementById('night-preview').disabled);
    assert(await page.locator('#night-save').isDisabled());
    await page.locator('#night-display-form').evaluate(form=>{form.requestSubmit(document.getElementById('night-preview'));});
    assert.equal(posts.length,count+1);release();
    await page.waitForFunction(()=>!document.getElementById('night-preview').disabled);pending=false;
    await field('enabled').uncheck();await field('zone').selectOption('1');
    await page.locator('#night-save').click();await page.locator('h1').waitFor();
    assert.equal(posts.at(-1).enabled,undefined);assert.equal(posts.at(-1).zone,'1');
    assert.equal(posts.at(-1).preview,undefined);assert.equal(posts.at(-1).quick,undefined);
    for(const viewport of [{width:390,height:844},{width:1100,height:1100}]){
      await page.setViewportSize(viewport);await page.goto('http://device.test/settings');
      await field('enabled').check();await slider('night',25);
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
      await page.locator('body').click({position:{x:4,y:4}});
      await page.screenshot({path:path.join(root,'docs/images',viewport.width===390?'horario-nocturno-movil.png':'horario-nocturno-web.png'),fullPage:true});
    }
    assert.deepEqual(errors,[]);
    const context=await browser.newContext({javaScriptEnabled:false});
    await context.route('http://device.test/**',route=>route.fulfill({contentType:'text/html',body:html}));
    const plain=await context.newPage();await plain.goto('http://device.test/settings');
    assert.equal(await plain.locator('#night-preview').getAttribute('name'),'preview');
    assert.equal(await plain.locator('#night-preview').getAttribute('value'),'1');
    assert.equal(await plain.locator('[name=csrf]').inputValue(),'fixture-csrf-only');
    await context.close();
    console.log('PASS: night form, defaults, sliders, equal-time validation, CSRF preview payload, expiry UI, errors/offline, duplicate guard, native save, mobile/desktop and no-JS form');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
