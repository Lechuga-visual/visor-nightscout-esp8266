/* Actual bilingual assets and C++ templates, intercepted requests, no HTTP emulator. */
const assert=require('assert/strict');
const fs=require('fs'),path=require('path');
const {execFileSync}=require('child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const lang of ['es','en']){
   const setup=execFileSync('python3',[path.join(root,'tests/render_setup_page.py'),...(lang==='en'?['en']:[])],{encoding:'utf8'});
   const night=execFileSync('python3',[path.join(root,'tests/render_setup_page.py'),'night',...(lang==='en'?['en']:[])],{encoding:'utf8'});
   for(const viewport of [{width:390,height:844},{width:1365,height:1050}]){
    const page=await browser.newPage({viewport,reducedMotion:'reduce',locale:lang==='en'?'en-US':'es-CL'});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    let screen='setup',status=200,posts=[],offline=false;
    let state={firmware:'0.8.0',language:lang,csrf:'fixture-csrf-only',patient_name:'José <Alex>',available:true,glucose:142,reading_date_ms:Date.now()-120000,age_seconds:120,tone:'normal',trend_symbol:'↗',trend_label:lang==='en'?'Rising':'Subiendo',clock_valid:true,message:lang==='en'?'Connected':'Conectado',wifi_connected:true,rssi_dbm:-70,ip:'192.0.2.1',checked_seconds_ago:3,appearance:{mode:1,theme:0,accent:'#74edbc',background:'#0b1114',number:'#74edbc'},plot:{low:60,high:200,points:[[0,800,true],[160,750,false],[330,600,false],[500,650,false],[660,500,false],[830,450,false],[1000,400,false]]}};
    await page.route('http://device.test/**',async route=>{
     const req=route.request(),u=new URL(req.url());
     if(req.method()==='POST'){
      posts.push({url:u.pathname,...Object.fromEntries(new URLSearchParams(req.postData()))});
      if(u.pathname==='/appearance')state.appearance.mode=Number(posts.at(-1).mode);
      return route.fulfill({status,json:{ok:status===200}});
     }
     if(u.pathname==='/api/state')return offline?route.abort('connectionreset'):route.fulfill({json:state});
     if(u.pathname==='/')return route.fulfill({contentType:'text/html',body:screen==='setup'?setup:fs.readFileSync(path.join(root,'web',lang==='en'?'en':'','index.html'))});
     if(u.pathname==='/settings')return route.fulfill({contentType:'text/html',body:night});
     if(u.pathname.startsWith('/assets/')){
      const name=u.pathname.slice(8),file=path.join(root,'web',lang==='en'&&name!=='ui.css'?'en':'',name);
      return route.fulfill({contentType:name.endsWith('.css')?'text/css':'application/javascript',body:fs.readFileSync(file)});
     }
     return route.fulfill({status:404});
    });
    await page.goto('http://device.test/');
    assert.equal(await page.locator('html').getAttribute('lang'),lang);
    assert.equal(await page.locator('#language-form select').inputValue(),lang);
    assert.equal(await page.locator('#language-form [name=csrf]').inputValue(),'fixture-csrf-only');
    await page.locator('#language-form select').selectOption(lang==='en'?'es':'en');
    await page.locator('#language-form button').click();
    await page.waitForFunction(()=>document.body.textContent.includes('ok'));
    assert.deepEqual(posts.at(-1),{url:'/language',language:lang==='en'?'es':'en',csrf:'fixture-csrf-only'});
    assert.equal(Object.keys(posts.at(-1)).length,3,'Language form must not submit any secrets or other settings');
    await page.goto('http://device.test/');
    const input=name=>page.locator(`#setup-form [name="${name}"]`);
    await input('ssid').fill('My Wi-Fi');await input('wifi_password').fill('wifi-fixture-only');
    await page.locator('#setup-next').click();
    assert((await page.locator('#setup-counter').textContent()).includes(lang==='en'?'STEP 2 OF 3':'PASO 2 DE 3'));
    await input('patient_name').fill('José <Alex>');await input('token').fill('read-token-fixture');
    await page.locator('#setup-next').click();
    assert.equal(await page.locator('#review-patient').textContent(),'José <Alex>');
    assert.equal(await page.locator('#review-patient *').count(),0);
    assert(!(await page.locator('#setup-review').textContent()).includes('read-token-fixture'));
    await input('web_password').fill('admin-fixture-only');
    status=403;await page.locator('#setup-save').click();
    await page.waitForFunction(()=>!document.getElementById('setup-save').disabled);
    assert((await page.locator('#setup-error').textContent()).includes(lang==='en'?'expired':'caducó'));
    assert.equal(await input('token').inputValue(),'read-token-fixture');
    status=200;
    if(lang==='en'&&viewport.width===390){await page.goto('http://device.test/');await page.screenshot({path:path.join(root,'docs/images/setup-en.png'),fullPage:true});}
    screen='viewer';await page.goto('http://device.test/');await page.waitForFunction(()=>document.getElementById('glucose').textContent==='142');
    assert.equal(await page.locator('#reading-state').textContent(),lang==='en'?'In range':'En intervalo');
    assert.equal(await page.locator('#trend-label').textContent(),lang==='en'?'Rising':'Subiendo');
    assert((await page.locator('#age-detail').textContent()).includes(lang==='en'?'min ago':'Hace'));
    assert.equal(await page.locator('#dial-brand').getAttribute('aria-label'),'José <Alex>');
    for(const [tone,value,expected] of [['low',62,lang==='en'?'LOW':'BAJA'],['high',200,lang==='en'?'HIGH':'ALTA'],['old',142,lang==='en'?'OLD':'ANTIGUA']]){
     Object.assign(state,{tone,glucose:value,age_seconds:tone==='old'?900:120});await page.locator('#refresh').click();
     await page.waitForFunction(()=>!document.getElementById('refresh').disabled);
     assert((await page.locator(tone==='old'?'#age':'#dial-status').textContent()).includes(expected));
    }
    Object.assign(state,{tone:'normal',glucose:142,age_seconds:120,patient_name:'Alex'});
    for(const mode of ['0','1','2']){
     await page.locator('#view-mode').selectOption(mode);await page.waitForFunction(()=>!document.getElementById('view-mode').disabled);
     assert.equal(posts.at(-1).csrf,'fixture-csrf-only');assert.equal(state.appearance.mode,Number(mode));
    }
    state.appearance.mode=1;await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('refresh').disabled);
    if(lang==='en'&&viewport.width===1365){await page.goto('http://device.test/');await page.waitForFunction(()=>document.getElementById('glucose').textContent==='142');await page.screenshot({path:path.join(root,'docs/images/viewer-en.png'),fullPage:true,animations:'disabled'});}
    offline=true;await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('refresh').disabled);
    assert.equal(await page.locator('#glucose').textContent(),'142');assert(await page.locator('#network-notice').isVisible());offline=false;
    await page.goto('http://device.test/settings');assert.equal(await page.locator('h2').textContent(),lang==='en'?'Brightness and night schedule':'Brillo y horario nocturno');
    await page.locator('[name=enabled]').check();await page.locator('#night-preview').click();
    await page.waitForFunction(()=>!document.getElementById('night-preview').disabled);
    assert((await page.locator('#night-feedback').textContent()).includes(lang==='en'?'Not saved':'No se ha guardado'));
    assert.equal(posts.at(-1).csrf,'fixture-csrf-only');assert.equal(posts.at(-1).preview,'1');
    if(lang==='en'&&viewport.width===390){await page.reload();await page.locator('[name=enabled]').check();await page.screenshot({path:path.join(root,'docs/images/night-en.png'),fullPage:true});}
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    assert.deepEqual(errors,[]);await page.close();
   }
   const context=await browser.newContext({javaScriptEnabled:false});
   await context.route('http://device.test/**',route=>route.fulfill({contentType:'text/html',body:setup}));
   const page=await context.newPage();await page.goto('http://device.test/');
   assert.equal(await page.locator('#language-form').getAttribute('action'),'/language');
   assert.equal(await page.locator('[data-step]:visible').count(),3);
   assert((await page.locator('#setup-save').textContent()).includes(lang==='en'?'Save and restart':'Guardar y reiniciar'));
   await context.close();
  }
  console.log('PASS: ES/EN setup and language POST, secret-safe review, expired sessions, dashboard warnings/age/trends/modes/offline, night preview, mobile/desktop, no-JS and English screenshots');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1)});
