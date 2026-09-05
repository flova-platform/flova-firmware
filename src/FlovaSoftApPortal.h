#pragma once

#include <Arduino.h>

namespace flova {

// Kept self-contained so the browser never needs internet access while joined
// to the device setup network. A fragment carries the handoff across the
// HTTPS-to-HTTP top-level navigation without entering the HTTP request.
static const char kSoftApSetupPage[] PROGMEM = R"FLOVA(
<!doctype html><html lang="en" dir="ltr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta http-equiv="Content-Security-Policy" content="default-src 'none';connect-src 'self';style-src 'unsafe-inline';script-src 'unsafe-inline'">
<meta name="referrer" content="no-referrer"><title>Flova setup</title>
<style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f7f6;color:#12332e;font:16px system-ui,sans-serif;padding:24px}.card{width:min(420px,100%);background:#fff;border:1px solid #d9e5e2;border-radius:20px;padding:28px;box-shadow:0 12px 40px #1233}h1{font-size:22px;margin:0 0 12px}p{line-height:1.6;margin:0}.spin{width:28px;height:28px;border:3px solid #cde0dc;border-top-color:#087f6d;border-radius:50%;animation:s 1s linear infinite;margin:24px 0}@keyframes s{to{transform:rotate(1turn)}}a{display:none;margin-top:20px;color:#087f6d;font-weight:700}</style></head>
<body><main class="card"><h1>Setting up your Flova device</h1><p id="message">Checking the local device…</p><div id="spin" class="spin"></div><a id="back">Return to Flova</a></main>
<script>(async()=>{const message=document.querySelector('#message'),spin=document.querySelector('#spin'),back=document.querySelector('#back');let handoff,returnUrl;const showBack=()=>{back.href=returnUrl.href;back.style.display='inline-block'};const fail=(text)=>{message.textContent=text;spin.hidden=true;if(returnUrl)showBack()};try{const encoded=new URLSearchParams(location.hash.slice(1)).get('handoff');history.replaceState(null,'','/setup');if(!encoded)throw Error();let base64=encoded.replace(/-/g,'+').replace(/_/g,'/');while(base64.length%4)base64+='=';const bytes=Uint8Array.from(atob(base64),c=>c.charCodeAt(0));handoff=JSON.parse(new TextDecoder().decode(bytes));if(handoff.kind!=='flova-softap-handoff'||handoff.version!==1||!handoff.payload)throw Error();returnUrl=new URL(handoff.returnUrl);const localReturn=returnUrl.hostname==='localhost'||returnUrl.hostname==='127.0.0.1';if(returnUrl.protocol!=='https:'&&!localReturn)throw Error();const status=await fetch('/status',{headers:{Accept:'application/json'},cache:'no-store'});const statusBody=await status.json();if(!status.ok||statusBody.status!=='setup_mode'||statusBody.protocol!=='flova-link-v1')return fail('This device setup firmware is not compatible with this Flova app.');message.textContent='Sending Wi-Fi settings to the device…';const response=await fetch('/provision',{method:'POST',headers:{'Content-Type':'application/json',Accept:'application/json'},body:JSON.stringify(handoff.payload)});const body=await response.json().catch(()=>null);if(response.status!==202||body?.status!=='accepted')return fail('The device rejected the setup handoff. Return to Flova and try again.');message.textContent='Settings received. Reconnect to the internet, then return to Flova for verification.';spin.hidden=true;back.textContent='Return to Flova';showBack()}catch(error){history.replaceState(null,'','/setup');fail('Open this page from the Flova PWA after connecting to the device setup network.')}})();</script></body></html>
)FLOVA";

}  // namespace flova
