/* Browser checks with the production template/assets and intercepted requests.
 * No HTTP server is started. EEPROM/Wi-Fi/SPI/reboot checks require the device.
 */
const assert = require('assert/strict');
const fs = require('fs');
const path = require('path');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '..');
const {execFileSync} = require('child_process');
const html = execFileSync('python3', [path.join(root, 'tests/render_setup_page.py')], {encoding:'utf8'});
(async () => {
  const browser = await chromium.launch({executablePath:process.env.CHROMIUM_PATH || '/usr/bin/chromium',
    headless:true,args:['--no-sandbox']});
  try {
    const page = await browser.newPage({viewport:{width:390,height:844}});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    let posts = [], status = 200, pending = false, networkFail = false, release;
    await page.route('http://device.test/**', async route => {
      const request = route.request(), pathname = new URL(request.url()).pathname;
      if (pathname === '/configure' && request.method() === 'POST') {
        posts.push(Object.fromEntries(new URLSearchParams(request.postData())));
        if (networkFail) return route.abort('connectionreset');
        if (pending) await new Promise(resolve => { release = resolve; });
        return route.fulfill({status,contentType:'text/plain',body:status === 200 ? 'Saved' : status === 400 ? 'Revisa el PEM.' : 'Rejected'});
      }
      if (pathname === '/' || pathname === '/configure') return route.fulfill({contentType:'text/html',body:html});
      const asset = {'/assets/ui.css':['ui.css','text/css'], '/assets/setup.js':['setup.js','application/javascript']}[pathname];
      return asset ? route.fulfill({contentType:asset[1],body:fs.readFileSync(path.join(root,'web',asset[0]))}) : route.abort();
    });
    const input = name => page.locator(`[name="${name}"]`);
    const step = async index => {
      assert.equal(await page.locator(`[data-step="${index}"]`).isVisible(), true);
      assert.equal(await page.locator('[data-step]:visible').count(), 1);
      assert.equal(await page.locator('#setup-counter').textContent(), `PASO ${index+1} DE 3`);
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    };
    await page.goto('http://device.test/'); await step(0);
    await page.locator('#setup-next').click(); await step(0); assert.equal(posts.length,0);
    await input('ssid').fill('ñ'.repeat(17)); await page.locator('#setup-next').click(); await step(0);
    await input('ssid').fill('Casa Wi-Fi'); await input('wifi_password').fill('short');
    await page.locator('#setup-next').click(); await step(0);
    await input('wifi_password').fill('wifi-secret-fixture'); await page.locator('#setup-next').click(); await step(1);
    await input('patient_name').fill('<img src=x onerror=bad>');
    await input('url').fill('https://example.test/?token=unsafe');
    await page.locator('#setup-next').click(); await step(1);
    await input('url').fill('https://tu-nightscout.example');
    await input('token').fill('token-secret-fixture');
    await input('url').fill('http://tu-nightscout.example'); await page.locator('#setup-next').click(); await step(1);
    await input('url').fill('https://tu-nightscout.example');
    await input('clear_token').check(); await page.locator('#setup-next').click(); await step(1);
    await input('clear_token').uncheck();
    await page.locator('summary').click();
    await input('custom_ca').fill('-----BEGIN PRIVATE KEY-----');
    await page.locator('summary').click(); // Invalid advanced fields must reopen before focus.
    await page.locator('#setup-next').click(); await step(1); assert(await page.locator('details').getAttribute('open') !== null);
    await input('custom_ca').fill('-----BEGIN PUBLIC KEY-----\nfixture-public-key\n-----END PUBLIC KEY-----');
    await page.locator('#setup-next').click(); await step(2);
    assert.equal(posts.length,0);
    assert.equal(await page.locator('#review-patient').textContent(), '<img src=x onerror=bad>');
    assert.equal(await page.locator('#review-patient *').count(),0);
    const review = await page.locator('#setup-review').textContent();
    assert(!review.includes('wifi-secret-fixture') && !review.includes('token-secret-fixture') && !review.includes('fixture-public-key'));
    await input('web_password').fill('admin-secret-fixture');
    await page.locator('#setup-back').click(); await step(1);
    assert.equal(await input('token').inputValue(),'token-secret-fixture');
    assert((await input('custom_ca').inputValue()).includes('fixture-public-key'));
    await page.locator('#setup-back').click(); await step(0);
    assert.equal(await input('wifi_password').inputValue(),'wifi-secret-fixture');
    await page.locator('#setup-next').click(); await page.locator('#setup-next').click(); await step(2);
    assert.equal(await input('web_password').inputValue(),'admin-secret-fixture');
    // Enter before the final step must advance without writing configuration.
    await page.locator('#setup-back').click(); await input('url').press('Enter'); await step(2); assert.equal(posts.length,0);
    await input('web_username').fill('bad:user'); await page.locator('#setup-save').click(); await step(2); assert.equal(posts.length,0);
    await input('web_username').fill('admin');
    for (status of [400,401,403,500]) {
      await page.locator('#setup-save').click(); await page.waitForFunction(() => !document.getElementById('setup-save').disabled);
      assert(await page.locator('#setup-error').isVisible());
      if (status === 403) assert((await page.locator('#setup-error').textContent()).includes('caducó'));
      assert.equal(await input('wifi_password').inputValue(),'wifi-secret-fixture');
      assert.equal(await input('token').inputValue(),'token-secret-fixture');
      assert.equal(await input('web_password').inputValue(),'admin-secret-fixture');
    }
    networkFail=true;const failedBefore=posts.length;
    await page.locator('#setup-save').click();await page.waitForFunction(() => !document.getElementById('setup-save').disabled);
    assert((await page.locator('#setup-error').textContent()).includes('No se pudo confirmar'));
    assert.equal(posts.length,failedBefore+1); // Never auto-retry an uncertain save.
    assert.equal(await input('web_password').inputValue(),'admin-secret-fixture');
    networkFail=false;
    status=200;pending=true;const before=posts.length;
    await page.locator('#setup-save').click();
    await page.waitForFunction(() => document.getElementById('setup-save').disabled);
    await page.locator('#setup-form').evaluate(form => { form.requestSubmit(); form.requestSubmit(); });
    assert.equal(posts.length,before+1); assert(await page.locator('#setup-back').isDisabled());
    const saved=posts.at(-1);
    assert.equal(saved.csrf,'fixture-csrf-only'); assert.equal(saved.ssid,'Casa Wi-Fi');
    assert.equal(saved.wifi_password,'wifi-secret-fixture'); assert.equal(saved.token,'token-secret-fixture');
    assert.equal(saved.web_password,'admin-secret-fixture'); assert(saved.custom_ca.includes('fixture-public-key'));
    release(); await page.locator('#setup-success').waitFor({state:'visible'});
    assert.equal(await page.locator('#setup-form').isVisible(),false);
    assert.equal(await input('wifi_password').inputValue(),''); assert.equal(await input('token').inputValue(),'');
    assert.equal(await input('web_password').inputValue(),'');
    // Capture real production template renders at desktop/mobile sizes.
    pending=false;posts=[];
    for (const viewport of [{width:1100,height:1000},{width:390,height:844}]) {
      await page.setViewportSize(viewport);await page.goto('http://device.test/');await step(0);
      await input('ssid').fill('Casa Wi-Fi');await input('wifi_password').fill('example-wifi-only');
      if(viewport.width===390)await page.screenshot({path:path.join(root,'docs/images/configuracion-wifi-movil.png'),fullPage:true});
      await page.locator('#setup-next').click();await input('patient_name').fill('Alex');await step(1);
      if(viewport.width===390)await page.screenshot({path:path.join(root,'docs/images/configuracion-nightscout-movil.png'),fullPage:true});
      await page.locator('#setup-next').click();await step(2);
      if(viewport.width===1100)await page.screenshot({path:path.join(root,'docs/images/configuracion-resumen.png'),fullPage:true});
    }
    assert.deepEqual(errors,[]);
    // No JavaScript: all fields remain available, CSRF and native form submission stay intact.
    const fallback=await browser.newContext({javaScriptEnabled:false,viewport:{width:390,height:844}});
    await fallback.route('http://device.test/**',route => new URL(route.request().url()).pathname==='/assets/ui.css'
      ?route.fulfill({contentType:'text/css',body:fs.readFileSync(path.join(root,'web/ui.css'))})
      :route.fulfill({contentType:'text/html',body:html}));
    const plain=await fallback.newPage();await plain.goto('http://device.test/');
    assert.equal(await plain.locator('[data-step]:visible').count(),3);
    assert.equal(await plain.locator('#setup-form input[name=csrf]').inputValue(),'fixture-csrf-only');
    await fallback.close();
    console.log('PASS: setup steps, validation, back/Enter, secret-safe review, CSRF payload, errors, duplicate-save guard, success, mobile/desktop, no-JS fallback');
  } finally { await browser.close(); }
})().catch(error => { console.error(error);process.exitCode=1; });
