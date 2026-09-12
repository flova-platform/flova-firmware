#pragma once

#include <Arduino.h>

namespace flova {

// Kept self-contained so the browser never needs internet access while joined
// to the device setup network. A fragment carries the handoff across the
// HTTPS-to-HTTP top-level navigation without entering the HTTP request.
static const char kSoftApSetupPage[] PROGMEM = R"FLOVA(
<!doctype html><html lang="fa" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta http-equiv="Content-Security-Policy" content="default-src 'none';connect-src 'self';style-src 'unsafe-inline';script-src 'unsafe-inline'">
<meta name="referrer" content="no-referrer"><title>راه‌اندازی دستگاه فلووا</title>
<style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f7ed;color:#19230e;font:16px Tahoma,"Noto Sans Arabic","Vazirmatn",sans-serif;padding:24px;text-align:right}.card{width:min(420px,100%);background:#fff;border:1px solid #d9e9b7;border-radius:20px;padding:28px;box-shadow:0 12px 40px #17200b24}h1{font-size:22px;line-height:1.5;margin:0 0 12px}p{line-height:1.9;margin:0}.spin{width:28px;height:28px;border:3px solid #e4efc9;border-top-color:#8bbd16;border-radius:50%;animation:s 1s linear infinite;margin:24px 0}@keyframes s{to{transform:rotate(1turn)}}a{display:none;align-items:center;justify-content:center;min-height:46px;margin-top:20px;padding:10px 18px;border-radius:12px;background:#BFFF32;color:#17200B;font-weight:700;text-decoration:none;box-shadow:0 4px 0 #91b51e}a:focus-visible{outline:3px solid #17200B;outline-offset:3px}@media(max-width:420px){body{padding:16px}.card{padding:22px;border-radius:16px}}</style></head>
<body><main class="card"><h1>راه‌اندازی دستگاه فلووا</h1><p id="message" aria-live="polite">در حال بررسی دستگاه محلی…</p><div id="spin" class="spin" aria-hidden="true"></div><a id="back">بازگشت به فلووا</a></main>
<script>(async()=>{const message=document.querySelector('#message'),spin=document.querySelector('#spin'),back=document.querySelector('#back');let handoff,returnUrl;const showBack=()=>{back.href=returnUrl.href;back.style.display='inline-flex'};const fail=(text)=>{message.textContent=text;spin.hidden=true;if(returnUrl)showBack()};try{const encoded=new URLSearchParams(location.hash.slice(1)).get('handoff');history.replaceState(null,'','/setup');if(!encoded)throw Error();let base64=encoded.replace(/-/g,'+').replace(/_/g,'/');while(base64.length%4)base64+='=';const bytes=Uint8Array.from(atob(base64),c=>c.charCodeAt(0));handoff=JSON.parse(new TextDecoder().decode(bytes));if(handoff.kind!=='flova-softap-handoff'||handoff.version!==1||!handoff.payload)throw Error();returnUrl=new URL(handoff.returnUrl);const localReturn=returnUrl.hostname==='localhost'||returnUrl.hostname==='127.0.0.1';if(returnUrl.protocol!=='https:'&&!localReturn)throw Error();const status=await fetch('/status',{headers:{Accept:'application/json'},cache:'no-store'});const statusBody=await status.json();if(!status.ok||statusBody.status!=='setup_mode'||statusBody.protocol!=='flova-link-v1')return fail('این firmware راه‌اندازی با برنامهٔ فلووا سازگار نیست.');message.textContent='در حال ارسال تنظیمات Wi‑Fi به دستگاه…';const response=await fetch('/provision',{method:'POST',headers:{'Content-Type':'application/json',Accept:'application/json'},body:JSON.stringify(handoff.payload)});const body=await response.json().catch(()=>null);if(response.status!==202||body?.status!=='accepted')return fail('دستگاه تنظیمات را نپذیرفت. به فلووا برگردید و دوباره تلاش کنید.');message.textContent='تنظیمات دریافت شد. به اینترنت برگردید و برای تأیید به فلووا بازگردید.';spin.hidden=true;back.textContent='بازگشت به فلووا';showBack()}catch(error){history.replaceState(null,'','/setup');fail('این صفحه را پس از اتصال به شبکهٔ راه‌اندازی دستگاه، از داخل PWA فلووا باز کنید.')}})();</script></body></html>
)FLOVA";

}  // namespace flova
