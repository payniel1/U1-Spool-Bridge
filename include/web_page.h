#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>U1 Spool Bridge</title>
<style>
:root{
  --bg:#0d1117; --panel:#161b22; --panel2:#1c232c; --line:#2a3441;
  --fg:#e6edf3; --dim:#8b98a5; --accent:#4f9dff; --ok:#3fb950; --warn:#d29922; --bad:#f85149;
  --r:12px;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
  font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  padding:0 0 40px;-webkit-font-smoothing:antialiased}
header{position:sticky;top:0;z-index:10;background:rgba(13,17,23,.92);
  backdrop-filter:blur(8px);border-bottom:1px solid var(--line);
  padding:12px 16px;display:flex;align-items:center;gap:12px;flex-wrap:wrap}
h1{font-size:16px;margin:0;font-weight:600;letter-spacing:-.01em}
.pills{display:flex;gap:6px;margin-left:auto;flex-wrap:wrap}
.pill{font-size:11px;padding:3px 9px;border-radius:999px;background:var(--panel2);
  border:1px solid var(--line);color:var(--dim);display:flex;align-items:center;gap:5px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--dim);flex:none}
.dot.on{background:var(--ok);box-shadow:0 0 6px var(--ok)}
.dot.off{background:var(--bad)}
.dot.idle{background:var(--warn)}
main{max-width:760px;margin:0 auto;padding:16px;display:grid;gap:16px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r);padding:16px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--dim);
  margin:0 0 14px;font-weight:600}
.spool{display:flex;gap:14px;align-items:center;margin-bottom:16px}
.swatch{width:60px;height:60px;border-radius:14px;flex:none;border:1px solid var(--line);
  background:#333;box-shadow:inset 0 0 0 3px rgba(255,255,255,.06)}
.spool .meta{min-width:0}
.spool .name{font-size:18px;font-weight:600;letter-spacing:-.01em;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.spool .sub{font-size:12px;color:var(--dim);margin-top:2px;font-family:ui-monospace,monospace}
.badge{display:inline-block;font-size:10px;padding:2px 7px;border-radius:5px;
  background:#1f3350;color:var(--accent);border:1px solid #2b4a72;
  text-transform:uppercase;letter-spacing:.06em;font-weight:600;margin-bottom:5px}
.badge.unk{background:#3a2a12;color:var(--warn);border-color:#5a4520}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
label{display:block;font-size:11px;color:var(--dim);margin-bottom:4px;
  text-transform:uppercase;letter-spacing:.05em}
input,select{width:100%;background:var(--panel2);border:1px solid var(--line);color:var(--fg);
  border-radius:8px;padding:9px 10px;font:inherit;font-size:14px;outline:none}
input:focus,select:focus{border-color:var(--accent)}
input[type=color]{padding:3px;height:38px;cursor:pointer}
input[type=range]{padding:0;background:none;border:none}
.slots{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin:16px 0 12px}
.slot{background:var(--panel2);border:1px solid var(--line);border-radius:10px;
  padding:12px 4px;text-align:center;cursor:pointer;font-weight:600;font-size:14px;
  transition:.12s;user-select:none}
.slot:hover{border-color:#3d4b5c}
.slot.sel{background:#12304f;border-color:var(--accent);color:#cfe4ff}
.slot.armed{border-color:var(--warn);background:#33270f;color:#f0c674}
.slot small{display:block;font-size:9px;color:var(--dim);text-transform:uppercase;
  letter-spacing:.06em;font-weight:500;margin-top:2px}
.slot .occ{display:block;width:6px;height:6px;border-radius:50%;
  background:var(--line);margin:5px auto 0}
.slot .occ.on{background:var(--ok);box-shadow:0 0 5px var(--ok)}
.tabs{display:flex;gap:6px;margin-bottom:14px}
.sl{display:flex;align-items:center;gap:12px;padding:9px 2px;min-width:0;
    border-bottom:1px solid var(--line)}
.sl:last-child{border-bottom:none}
.sl .n{font:600 12px ui-monospace,monospace;color:var(--dim);width:46px;flex:none}
.sl .chip{width:22px;height:22px;border-radius:6px;flex:none;border:1px solid var(--line)}
.sl .t{display:flex;flex-direction:column;gap:1px;min-width:0;overflow:hidden}
.sl .t b{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sl .t b{font-size:13.5px}
.sl .t span{font-size:11.5px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sl .box{margin-left:auto;flex:none;font:600 10px ui-monospace,SFMono-Regular,monospace;
         letter-spacing:.06em;padding:2px 7px;border-radius:4px;
         background:#33280f;color:#e3b341;border:1px solid #5c4718}
.sl .mine{margin-left:auto;flex:none;font:600 10px ui-monospace,monospace;letter-spacing:.06em;
          padding:2px 7px;border-radius:4px;background:#12331f;color:#5bd68a;border:1px solid #1e5433}
.tab{flex:1;padding:9px 11px;border-radius:9px;background:var(--panel2);
  border:1px solid var(--line);cursor:pointer;display:flex;align-items:center;gap:9px}
.tab:hover{border-color:#3d4b5c}
.tab.on{background:#12304f;border-color:var(--accent)}
.tab .sw2{width:16px;height:16px;border-radius:5px;flex:none;border:1px solid var(--line);
  background:repeating-linear-gradient(45deg,#222,#222 3px,#2c2c2c 3px,#2c2c2c 6px)}
.tab .t{min-width:0}
.tab b{display:block;font-size:13px;font-weight:600}
.tab span{display:block;color:var(--dim);font-size:11px;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.gate{font-size:12.5px;color:var(--dim);margin:12px 0 0;line-height:1.45}
.gate b{color:var(--fg);font-weight:600}
.gate .idle{color:var(--warn)}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
button{background:var(--accent);border:none;color:#04101f;font:inherit;font-weight:600;
  padding:11px 18px;border-radius:9px;cursor:pointer;transition:.12s}
button:hover{filter:brightness(1.1)}
button:disabled{opacity:.4;cursor:not-allowed;filter:none}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--fg);font-weight:500}
.toggle{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--dim);cursor:pointer}
.toggle input{width:auto;accent-color:var(--accent)}
#log{font-family:ui-monospace,SFMono-Regular,monospace;font-size:12px;max-height:190px;
  overflow-y:auto;display:flex;flex-direction:column-reverse;gap:2px}
#log div{padding:3px 0;border-bottom:1px solid #1d242d;color:var(--dim);
  white-space:pre-wrap;word-break:break-word}
#log .ok{color:var(--ok)} #log .bad{color:var(--bad)} #log .warn{color:var(--warn)}
details summary{cursor:pointer;font-size:12px;text-transform:uppercase;letter-spacing:.08em;
  color:var(--dim);font-weight:600;list-style:none}
details summary::-webkit-details-marker{display:none}
details summary::before{content:"\25B8 ";display:inline-block;transition:.15s}
details[open] summary::before{transform:rotate(90deg)}
details[open] summary{margin-bottom:14px}
.hint{font-size:12px;color:var(--dim);margin:10px 0 0}
.sm{font-size:12px;color:var(--dim);margin-top:5px;display:flex;align-items:center;gap:8px}
.chip{background:#13341f;color:#5ddc82;border:1px solid #1e5233;border-radius:5px;
  padding:1px 7px;font-size:10.5px;font-weight:600;letter-spacing:.03em}
.bar{flex:1;max-width:130px;height:5px;background:var(--panel2);border-radius:3px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--ok);border-radius:3px}
#fwplan{margin-top:14px;border-top:1px solid var(--line);padding-top:10px}
#fwplan .cap{font-size:12px;color:var(--dim);margin:0 0 8px}
.fwb{display:flex;align-items:center;gap:10px;padding:7px 2px;min-width:0;
     border-bottom:1px solid var(--line)}
.fwb:last-of-type{border-bottom:none}
.fwb input[type=checkbox]{flex:none;width:16px;height:16px;accent-color:var(--accent)}
.fwb .t{display:flex;flex-direction:column;gap:1px;min-width:0;overflow:hidden;flex:1}
.fwb .t b{font-size:13.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fwb .t span{font-size:11.5px;color:var(--dim);overflow:hidden;
             text-overflow:ellipsis;white-space:nowrap}
.fwb .st{flex:none;font:600 10px ui-monospace,SFMono-Regular,monospace;letter-spacing:.06em;
         padding:2px 7px;border-radius:4px;border:1px solid var(--line);color:var(--dim)}
.fwb .st.go{background:#12331f;color:#5bd68a;border-color:#1e5433}
.fwb .st.no{background:#3a1d1d;color:#ff8a80;border-color:#5c2b2b}
.fwb .st.wa{background:#33280f;color:#e3b341;border-color:#5c4718}
.modal{position:fixed;inset:0;background:rgba(0,0,0,.65);display:none;z-index:50;
  align-items:flex-end;justify-content:center}
.modal.on{display:flex}
.sheet{background:var(--panel);border:1px solid var(--line);border-radius:16px 16px 0 0;
  width:100%;max-width:640px;max-height:82vh;display:flex;flex-direction:column;padding:16px}
@media(min-width:700px){.modal{align-items:center}.sheet{border-radius:16px}}
.sheet h3{margin:0 0 4px;font-size:16px}
.sheet .hint{margin:0 0 12px}
#splist{overflow-y:auto;flex:1;margin-top:10px;display:flex;flex-direction:column;gap:6px}
.sprow{display:flex;align-items:center;gap:10px;padding:9px 10px;border-radius:9px;
  background:var(--panel2);border:1px solid var(--line);cursor:pointer}
.sprow:hover{border-color:var(--accent)}
.sprow .sw2{width:22px;height:22px;border-radius:6px;flex:none;border:1px solid var(--line)}
.sprow .t{min-width:0;flex:1}
.sprow .t b{display:block;font-size:13.5px;font-weight:600;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sprow .t span{font-size:11px;color:var(--dim)}
.sprow em{font-style:normal;font-size:10px;color:var(--warn)}
#fleet{display:flex;flex-direction:column;gap:14px}
.fgrp{border:1px solid var(--line);border-radius:10px;overflow:hidden}
.fgrp>.gh{display:flex;align-items:center;gap:9px;padding:9px 11px;
          background:var(--panel2);cursor:pointer;user-select:none;min-width:0}
.fgrp>.gh .caret{flex:none;width:9px;color:var(--dim);font-size:10px;
                 transition:transform .15s}
.fgrp.shut>.gh .caret{transform:rotate(-90deg)}
.fgrp>.gh b{font-size:13.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
            border-bottom:1px dashed transparent;cursor:text}
.fgrp>.gh b:hover{border-bottom-color:var(--dim)}
.fgrp>.gh input.ren{font:600 13.5px system-ui,sans-serif;background:var(--bg);
     color:var(--fg);border:1px solid var(--accent);border-radius:5px;
     padding:2px 6px;min-width:0;flex:1;max-width:230px}
.fgrp>.gh .sum{font-size:11.5px;color:var(--dim);margin-left:auto;flex:none;
               white-space:nowrap}
.fgrp>.gh button{flex:none;padding:3px 9px;font-size:11px;margin-left:4px}
.fgrp>.gb{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));
          gap:8px;padding:9px}
.fgrp.shut>.gb{display:none}
.fgrp .peer.me{border-color:var(--accent)}
.peer{background:var(--panel2);border:1px solid var(--line);border-radius:10px;
  padding:10px 11px;display:flex;gap:9px;align-items:center;text-decoration:none;color:inherit}
.peer:hover{border-color:var(--accent)}
.peer .sw2{width:20px;height:20px;border-radius:6px;flex:none;border:1px solid var(--line)}
.peer .t{min-width:0}
.peer .t b{display:block;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.peer .t span{font-size:11px;color:var(--dim);display:block;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.peer.empty .sw2{background:repeating-linear-gradient(45deg,#222,#222 4px,#2c2c2c 4px,#2c2c2c 8px)}
.note{background:#3a2a12;border:1px solid #5a4520;color:#f0c674;border-radius:8px;
  padding:9px 11px;font-size:12.5px;margin-bottom:14px;display:none}
code{background:var(--panel2);padding:1px 5px;border-radius:4px;font-size:12px}
</style></head><body>

<header>
  <h1 id="title">U1 Spool Bridge</h1>
  <div class="pills">
    <span class="pill"><i class="dot" id="d-rd"></i><span id="t-rd">Reader</span></span>
    <span class="pill"><i class="dot" id="d-wf"></i><span id="t-wf">WiFi</span></span>
    <span class="pill" id="p-band" hidden><i class="dot" id="d-bd"></i><span id="t-bd">Band</span></span>
    <span class="pill"><i class="dot" id="d-pr"></i><span id="t-pr">Printer</span></span>
    <span class="pill" id="p-be" hidden title=""><i class="dot" id="d-be"></i><span id="t-be"></span></span>
    <span class="pill" id="p-sm" hidden><i class="dot" id="d-sm"></i>Spoolman</span>
  </div>
</header>

<main>
  <section class="card">
    <h2>Scanned spool</h2>
    <div class="tabs" id="tabs" hidden></div>
    <div class="note" id="note"></div>
    <div class="spool">
      <div class="swatch" id="sw"></div>
      <div class="meta">
        <span class="badge" id="src">waiting</span>
        <div class="name" id="nm">Present a tag to the PN532</div>
        <div class="sub" id="uid">no tag</div>
        <div class="sm" id="sm" hidden></div>
      </div>
    </div>

    <div class="grid">
      <div><label>Vendor</label><input id="f-vendor" placeholder="Generic"></div>
      <div><label>Material</label><select id="f-main"></select></div>
      <div><label>Sub-type</label><select id="f-sub"></select></div>
      <div><label>Colour</label><input type="color" id="f-color" value="#4f9dff"></div>
      <div><label>Hotend min &deg;C</label><input type="number" id="f-hmin" min="140" max="400"></div>
      <div><label>Hotend max &deg;C</label><input type="number" id="f-hmax" min="140" max="400"></div>
      <div><label>Bed &deg;C</label><input type="number" id="f-bed" min="0" max="140"></div>
      <div><label>Weight g</label><input type="number" id="f-weight" min="0" max="10000"></div>
      <div><label>Alpha <span id="a-val">255</span></label>
           <input type="range" id="f-alpha" min="0" max="255" value="255"></div>
      <div><label>SKU</label><input type="number" id="f-sku" min="0"></div>
    </div>

    <div class="slots" id="slots"></div>

    <div class="row">
      <button id="send">Send to printer</button>
      <button class="ghost" id="arm" hidden>Arm slot 1</button>
      <button class="ghost" id="rescan">Re-scan tag</button>
      <button class="ghost" id="link" hidden>Link to a Spoolman spool</button>
      <select id="mode" style="width:auto;min-width:220px">
        <option value="4">Send when a spool is inserted</option>
        <option value="0">Send when a slot loads</option>
        <option value="1">Send on every scan</option>
        <option value="2">Send only when armed</option>
        <option value="3">Manual only</option>
      </select>
    </div>
    <p class="gate" id="gate"></p>
  </section>

  <section class="card">
    <div style="display:flex;align-items:center;gap:10px;margin-bottom:4px">
      <h2 style="margin:0">Loaded in the printer</h2>
      <button class="ghost" id="reslots"
              style="margin-left:auto;padding:5px 12px;font-size:12px">Refresh</button>
    </div>
    <p class="hint" style="margin:0 0 12px">What the printer itself reports, read back from
      the same object this box writes to. It keeps showing after the spool has left the
      reader.</p>
    <div id="loaded"></div>
  </section>

  <section class="card">
    <div style="display:flex;align-items:center;gap:10px;margin-bottom:14px">
      <h2 style="margin:0">Other boxes</h2>
      <button class="ghost" id="refleet"
              style="margin-left:auto;padding:5px 12px;font-size:12px">Refresh</button>
    </div>
    <div id="fleet"><p class="hint">Looking for other boxes&hellip;</p></div>
    <p class="hint" id="fleetmsg"></p>
  </section>

  <section class="card">
    <h2>Firmware</h2>
    <p class="hint" id="fwnow" style="margin-top:0"></p>
    <div class="row" style="margin-top:12px">
      <input type="file" id="fwfile" accept=".bin" style="flex:1;min-width:170px">
      <button id="fwgo">Install</button>
      <button class="ghost" id="fwall">Update all boxes</button>
    </div>
    <div id="fwplan" hidden></div>
    <div class="bar" id="fwbarwrap" hidden style="max-width:none;margin-top:12px;height:7px">
      <i id="fwbar" style="width:0%"></i>
    </div>
    <p class="hint" id="fwmsg"></p>
  </section>

  <section class="card">
    <h2>Tag dump</h2>
    <p class="hint" style="margin-top:0">For a tag this firmware cannot decode yet.
      Put it on the reader and this walks all 16 sectors with every key it knows —
      the Bambu-style keys derived from the UID, anything in <b>Extra MIFARE keys</b>,
      and the public defaults — then shows exactly what opened and what came back.
      A sector reading <code>no key worked</code> means the key is unknown, not that
      the tag is empty.</p>
    <div class="row" style="margin-top:12px">
      <button id="dumpgo">Read the tag</button>
      <button class="ghost" id="dumpcopy" hidden>Copy</button>
    </div>
    <pre id="dumpout" hidden style="margin-top:12px;max-height:340px;overflow:auto;
         font:500 11.5px/1.5 ui-monospace,SFMono-Regular,monospace;
         background:var(--panel2);border:1px solid var(--line);border-radius:8px;
         padding:12px;white-space:pre"></pre>
  </section>

  <section class="card">
    <h2>Reader diagnostics</h2>
    <p class="hint" id="diagnow" style="margin-top:0"></p>
    <p class="hint">If the reader keeps dropping out, this settles why. The board
      reboots, polls the reader for five minutes with the <b>radio switched off</b>,
      then comes back on its own. Watch it on the USB serial console &mdash; it is
      off the network for the whole five minutes.</p>
    <p class="hint">No I2C errors with the radio off means WiFi transmit current is
      browning out the PN532: fit 100&nbsp;&micro;F + 0.1&nbsp;&micro;F across its
      VCC/GND, or turn TX power down in Settings. Errors anyway means the wiring is
      at fault &mdash; shorten SDA/SCL and check the pull-ups.</p>
    <div class="row" style="margin-top:12px">
      <button class="ghost" id="diaggo">Run 5-minute radio-off test</button>
    </div>
  </section>

  <section class="card">
    <h2>Activity</h2>
    <div id="log"></div>
  </section>

  <section class="card">
    <details id="setpanel"><summary>Settings</summary>
      <div class="grid">
        <div><label>Box name</label><input id="s-box" placeholder="Drybox 3"></div>
        <div><label>Group</label><input id="s-group" maxlength="23"
             placeholder="(grouped by printer)"></div>
        <div><label>Printer host / IP</label><input id="s-host" placeholder="192.168.1.50"></div>
        <div><label>Printer port</label><input type="number" id="s-port" value="80"></div>
        <div><label>Moonraker API key</label><input id="s-key" type="password" placeholder="(unchanged)"></div>
        <div><label>Printer backend</label>
          <select id="s-backend">
            <option value="0">Auto &mdash; detect, and fix itself</option>
            <option value="1">paxx12 Extended Firmware</option>
            <option value="2">Stock firmware + Bespok3d</option>
          </select>
          <p class="hint" id="s-backendhint" style="margin:6px 0 0"></p>
        </div>
        <div><label>WiFi SSID</label><input id="s-ssid"></div>
        <div><label>WiFi password</label><input id="s-pass" type="password" placeholder="(unchanged)"></div>
        <div id="band-row" hidden><label>WiFi band</label><select id="s-band">
          <option value="0">Auto (2.4 + 5 GHz)</option>
          <option value="1">2.4 GHz only</option>
          <option value="2">5 GHz only</option></select></div>
        <div><label>WiFi TX power</label><select id="s-txp">
          <option value="0">Default (max)</option>
          <option value="17">17 dBm</option>
          <option value="15">15 dBm</option>
          <option value="13">13 dBm</option>
          <option value="11">11 dBm</option>
          <option value="8">8 dBm</option></select></div>
        <div><label>Scan interval ms</label><input type="number" id="s-scan" min="100" max="5000"></div>
        <div><label>Extra MIFARE keys</label><input id="s-keys" placeholder="FFFFFFFFFFFF,A0A1..."></div>
        <div><label>Spoolman host / IP</label><input id="s-smhost" placeholder="192.168.1.20"></div>
        <div><label>Spoolman port</label><input type="number" id="s-smport" value="7912"></div>
        <div style="grid-column:1/-1">
          <label>Spoolman location</label>
          <input id="s-locfmt" placeholder="U1 slot {slot}">
          <p class="hint" style="margin:6px 0 0">
            Where Spoolman files a spool this box sends. Spoolman groups its
            list by location, so <code>{group}</code> on its own gives one
            heading per group &mdash; add <code>{slot}</code> and you get one
            per box instead.
            Tokens: <code>{group}</code> <code>{slot}</code> <code>{box}</code>.
            <button type="button" class="ghost" id="s-locgrp"
                    style="padding:3px 9px;font-size:11px;margin-left:6px">Use the group name</button>
            <button type="button" class="ghost" id="s-locall"
                    style="padding:3px 9px;font-size:11px;margin-left:6px">Apply to every box</button>
          </p>
          <p class="hint" id="s-locprev" style="margin:6px 0 0"></p>
          <p class="hint" id="s-locmsg" style="margin:6px 0 0"></p>
        </div>
        <div><label>NTP server</label><input id="s-ntp" placeholder="pool.ntp.org"></div>
        <div><label>Timezone (POSIX TZ)</label><input id="s-tz" placeholder="UTC0"></div>
        <div><label>OTA password</label><input id="s-otapw" type="password"
             placeholder="(unchanged, - to clear)"></div>
        <div><label>Dwell ms</label><input type="number" id="s-dwell" min="0" max="10000"></div>
        <div><label>Absence ms</label><input type="number" id="s-abs" min="200" max="60000"></div>
        <div><label>Cooldown s</label><input type="number" id="s-cool" min="0" max="3600"></div>
        <div><label>Scan valid for s</label><input type="number" id="s-valid" min="5" max="3600"></div>
        <div><label>Arm timeout s</label><input type="number" id="s-armto" min="5" max="3600"></div>
        <div><label>Slot poll ms</label><input type="number" id="s-poll" min="250" max="10000"></div>
      </div>
      <div class="row" style="margin-top:14px">
        <label class="toggle"><input type="checkbox" id="s-generic"> Report vendor as "Generic"</label>
        <label class="toggle"><input type="checkbox" id="s-uid"> Send CARD_UID</label>
        <label class="toggle"><input type="checkbox" id="s-boot"> Re-send on boot</label>
        <label class="toggle"><input type="checkbox" id="s-ota"> Allow OTA updates</label>
      </div>
      <div class="row" style="margin-top:10px">
        <label class="toggle"><input type="checkbox" id="s-smon"> Use Spoolman</label>
        <label class="toggle"><input type="checkbox" id="s-smloc"> Set spool location on send</label>
        <label class="toggle"><input type="checkbox" id="s-smnote"> Note loads in the comment</label>
      </div>
      <div class="row" style="margin-top:14px">
        <button id="save">Save settings</button>
        <button class="ghost" id="test">Test printer</button>
      </div>
      <p class="hint">Slot 1&ndash;4 map to <code>channel</code> 0&ndash;3. Saving WiFi
         credentials reboots the bridge.</p>
    </details>
  </section>

  <p class="hint" style="text-align:center" id="ver"></p>
</main>

<div class="modal" id="picker">
  <div class="sheet">
    <h3>Link this tag</h3>
    <p class="hint">Pick the spool that's physically on the reader. The tag's UID
       gets written to that spool's <code>card_uids</code> field, and removed from
       any other spool claiming it.</p>
    <input id="spsearch" placeholder="Search vendor, name or material...">
    <div id="splist"></div>
    <div class="row" style="margin-top:12px">
      <button class="ghost" id="pclose">Cancel</button>
    </div>
  </div>
</div>

<script>
const $=id=>document.getElementById(id);
// Grouped so a 30-entry list stays navigable. The U1 accepts any MAIN_TYPE
// string — only PLA/PETG/ABS/TPU/PVA get its RFID-protocol mapping — so the
// filled grades are carried through verbatim rather than folded onto the base.
const MAIN_GROUPS=[
  ["Common",      ["PLA","PETG","PCTG","ABS","ASA","TPU","PVA"]],
  ["Carbon fibre",["PLA-CF","PETG-CF","PET-CF","ABS-CF","ASA-CF","PC-CF",
                   "PA-CF","PAHT-CF","PP-CF","PPA-CF","PPS-CF"]],
  ["Glass fibre", ["PLA-GF","PETG-GF","PET-GF","ABS-GF","ASA-GF","PC-GF",
                   "PA-GF","PP-GF"]],
  ["Engineering", ["PC","PA","PAHT","PET","PP","PPA","PPS","HIPS"]],
];
const MAINS=MAIN_GROUPS.flatMap(g=>g[1]);
const SUBS=["Basic","Matte","SnapSpeed","Silk","Support","HF","95A","95A HF",
            "CF","GF","Tough","Aero"];
MAIN_GROUPS.forEach(([label,items])=>{
  const g=document.createElement("optgroup"); g.label=label;
  items.forEach(m=>g.appendChild(new Option(m,m)));
  $("f-main").appendChild(g);
});
SUBS.forEach(s=>$("f-sub").add(new Option(s,s)));

let channel=0, spool=null, spoolmanOn=false, spools=[];
let mode=0, armed=false, armedCh=0, pending=false, pendingAge=0,
    chan=[false,false,false,false], chanKnown=false;
// One lane per reader — a dual-slot box runs two off one board.
let readers=1, active=0, lanes=[], laneSpools=[null,null], readerChannel=[0,1];
let myVersion="";
// What this box is running. myVersion is filled from the first websocket status
// frame, which can arrive after the fleet has already been drawn — so fall back
// to what /api/brief told us, or every peer silently looks up to date.
const refVersion=()=>myVersion||(fwSelf&&fwSelf.version)||"";

// The printer's own answer rather than our memory of what we sent. A slot whose
// CARD_UID matches a tag this box has read gets marked, so across a fleet you can
// see which drybox fed which slot.
let slots=[], slotsKnown=false, slotsErr="";
let backendName="", backendKnown=false, backendConfirmed=false,
    backendPinned=false, presenceOnly=false;

// Three states worth telling apart, because they mean different things:
//   pinned    — you chose it; detection is deliberately ignored
//   confirmed — a send settled it, which is the only direct evidence there is
//   inferred  — the 15s status probe recognised the shape, nothing more
// Anything else is still the opening assumption and says so.
function paintBackendPill(){
  const pill=$("p-be"), t=$("t-be"); if(!pill||!t)return;
  if(!backendName){pill.hidden=true;return;}
  pill.hidden=false;
  const how = backendPinned?"pinned"
            : backendConfirmed?"confirmed by a send"
            : backendKnown?"inferred from the printer's reply, not yet confirmed by a send"
            : "assumed \u2014 nothing has confirmed it yet";
  t.textContent=backendName+(backendPinned?" \u00B7 pinned":backendConfirmed?"":" ?");
  pill.title=`Printer backend: ${backendName} (${how}).`;
  setDot("d-be", backendPinned||backendConfirmed?"on":backendKnown?"idle":"off");
}

function paintBackendHint(){
  const el=$("s-backendhint"), sel=$("s-backend"); if(!el||!sel)return;
  const v=+sel.value||0;
  let t;
  if(v===1){
    t="Sends CARD_TYPE and reads the printer back. If a send is refused for an "
     +"unknown field, this pinned setting is why \u2014 the box will say so "
     +"rather than working around it.";
  }else if(v===2){
    t="Leaves CARD_TYPE out: the Bespok3d handler validates the whole info "
     +"object and rejects the entire request over one key it does not know. "
     +"\u201cLoaded in the printer\u201d still works \u2014 filament_detect is a stock "
     +"object \u2014 but it reports no tag UID, so the THIS BOX badge cannot appear.";
  }else{
    t="Works it out from the printer, and if a send is refused for an unknown "
     +"field it drops the Extended-only ones and retries.";
  }
  if(backendName){
    const how = backendPinned?"pinned here"
              : backendConfirmed?"confirmed by a send"
              : backendKnown?"inferred from the printer's reply, not yet confirmed by a send"
              : "assumed \u2014 nothing has confirmed it";
    t+=` Currently: ${backendName} (${how}).`;
  }
  el.textContent=t;
}
const myUids=new Set();

// What the dryboxes themselves say is sitting in each slot of THIS printer.
//
// The card used to show only the printer's own readback, which stays blank for
// a slot until the machine actually has that filament in it — so between
// putting a spool in a drybox and the printer pulling it through for a print,
// the card had nothing to show even though the answer was known all along.
// The dryboxes knew it; this is where it now comes from.
function boxesBySlot(){
  const mine=((fwSelf&&fwSelf.printer)||"").trim().toLowerCase();
  const out={};
  const all=fwSelf?[{...fwSelf,__self:true},...fleetPeers]:fleetPeers;
  all.forEach(b=>{
    const p=(b.printer||"").trim().toLowerCase();
    if(mine&&p&&p!==mine)return;          // a box feeding the other printer
    if(!b.present||!b.spool)return;
    if(!out[b.slot]||b.__self)out[b.slot]=b;   // prefer our own reading
  });
  return out;
}

function paintLoaded(){
  const el=$("loaded"); if(!el)return;
  if(!slotsKnown&&!slots.length){
    el.innerHTML=`<p class="hint">${slotsErr||"waiting for the printer\u2026"}</p>`;return;
  }
  el.innerHTML="";
  const fromBox=boxesBySlot();
  let anyBoxOnly=false;
  slots.forEach(s=>{
    const d=document.createElement("div"); d.className="sl";
    // The U1 fills unpopulated fields with the string "NONE", not "", so an
    // empty slot would otherwise read "NONE NONE NONE".
    const val=v=>{const t=(v||"").trim();return /^none$/i.test(t)?"":t;};
    const mt=val(s.mainType), vn=val(s.vendor), st=val(s.subType);
    const known=s.known&&mt;
    const bx=fromBox[s.n];
    let label,sub,sw,badge="";

    if(known){
      label=[vn||"Generic",mt,st].filter(Boolean).join(" ");
      const temps=s.hotendMax
          ?`${s.hotendMin}\u2013${s.hotendMax}\u00B0C nozzle \u00B7 ${s.bedTemp}\u00B0C bed`:"";
      const uid=s.uid?`UID ${s.uid}`:"";
      sub=[temps,uid].filter(Boolean).join("  \u00B7  ");
      sw=`style="background:${hex(s.rgb||0)}"`;
      if(s.uid&&myUids.has(s.uid.toUpperCase()))badge=`<span class="mine">THIS BOX</span>`;
    }else if(bx){
      // The printer has nothing for this slot, but a drybox on this printer
      // does. Say whose it is and be explicit that the printer has not
      // confirmed it, rather than dressing it up as a readback.
      anyBoxOnly=true;
      label=bx.spool;
      sub=`in ${bx.box||bx.host}`
         +(bx.remainingG?` \u00B7 ${bx.remainingG} g`:"")
         +` \u00B7 printer reports this slot ${s.present?"loaded, no filament data":"empty"}`;
      sw=`style="background:${hex(bx.rgb||0)};border-style:dashed"`;
      badge=`<span class="box">IN THE BOX</span>`;
    }else{
      label=s.present?"loaded \u2014 no filament data":"empty";
      sub="";
      sw=`style="background:transparent;border-style:dashed"`;
    }

    d.innerHTML=`<span class="n">SLOT ${s.n}</span>`
      +`<span class="chip" ${sw}></span>`
      +`<span class="t"><b>${label}</b>${sub?`<span>${sub}</span>`:""}</span>`
      +badge;
    el.appendChild(d);
  });
  if(anyBoxOnly)el.insertAdjacentHTML("beforeend",
    `<p class="hint">Rows marked <b>IN THE BOX</b> come from the drybox's own `
   +`reader. The printer only reports filament data for a slot once it has that `
   +`filament in it, so until you load it these are the only source.</p>`);
  if(presenceOnly){
    el.insertAdjacentHTML("beforeend",
      `<p class="hint">This printer reports which slots are <b>occupied</b> but not `
     +`what is in them \u2014 it has no queryable <code>filament_detect</code>. `
     +`Everything above that names a filament came from a drybox.</p>`);
  }else if(slotsErr){
    el.insertAdjacentHTML("beforeend",`<p class="hint">${slotsErr}</p>`);
  }
}

function paintTabs(){
  $("tabs").hidden=readers<2;
  if(readers<2)return;
  $("tabs").innerHTML="";
  lanes.forEach((l,i)=>{
    const d=document.createElement("div");
    d.className="tab"+(i===active?" on":"");
    const sw=l.present?`style="background:${hex(l.rgb||0)}"`:"";
    d.innerHTML=`<span class="sw2" ${sw}></span><span class="t">`
      +`<b>Reader ${i+1} - slot ${l.slot}</b>`
      +`<span>${l.ready?(l.present?(l.label||"loaded"):"empty")+(l.resets?` - ${l.resets} resets`:"")
                     :(l.err||"reader offline")}</span></span>`;
    d.onclick=()=>{
      active=i; channel=(readerChannel[i]|0);
      showSpool(laneSpools[i]); paintTabs(); paintSlots();
      fetch("/api/reader?i="+i);
    };
    $("tabs").appendChild(d);
  });
}

for(let i=0;i<4;i++){
  const d=document.createElement("div");
  d.className="slot"; d.dataset.c=i;
  d.innerHTML=(i+1)+"<small>slot</small><i class='occ'></i>";
  // The slot buttons are the reader's binding, not a transient selection:
  // "this reader feeds slot N".
  d.onclick=async()=>{
    channel=i; readerChannel[active]=i;
    paintSlots(); paintTabs();
    await fetch("/api/settings",{method:"POST",
      headers:{"Content-Type":"application/json"},
      body:JSON.stringify({readerChannel})});
    log(`reader ${active+1} now feeds slot ${i+1}`);
  };
  $("slots").appendChild(d);
}
function paintSlots(){
  document.querySelectorAll(".slot").forEach(e=>{
    const i=+e.dataset.c;
    e.classList.toggle("sel",i===channel);
    e.classList.toggle("armed",armed&&i===armedCh);
    // Green dot = the printer says that slot has filament in it.
    e.querySelector(".occ").className="occ"+(chanKnown&&chan[i]?" on":"");
    e.title=chanKnown?(chan[i]?"occupied":"empty"):"slot state unknown";
  });
  $("arm").hidden=(mode!==2);
  $("arm").textContent=`Arm slot ${channel+1}`;
  paintGate();
}

// Explain, in words, what will happen next. Without this the on-load mode
// looks broken — you scan a spool and nothing appears to occur.
function paintGate(){
  const g=$("gate");
  const held=pending?`<b>${spool?spool.vendor+" "+spool.mainType:"a spool"}</b> is held`
                    +(pendingAge?` (${pendingAge}s ago)`:"") :"nothing scanned yet";
  if(mode===4){
    g.innerHTML=pending
      ? `<b>${spool?spool.vendor+" "+spool.mainType:"A spool"}</b> is in this box, `
        +`assigned to <b>slot ${channel+1}</b>.`
      : `Box is empty. The next spool put in here goes to <b>slot ${channel+1}</b>.`;
  }else if(mode===0){
    g.innerHTML=chanKnown
      ? `${held}. It goes to whichever slot the printer reports filling next.`
      : `${held}. <span class="idle">Can't read slot state from the printer — `
        +`nothing will send automatically.</span>`;
  }else if(mode===1){
    g.innerHTML=`Every settled scan is sent straight to <b>slot ${channel+1}</b>.`;
  }else if(mode===2){
    g.innerHTML=armed?`<b>Armed: slot ${armedCh+1}</b>. Present a spool.`
                     :`${held}. Press <b>Arm slot ${channel+1}</b> to release it.`;
  }else{
    g.innerHTML=`${held}. Nothing sends until you press <b>Send to printer</b>.`;
  }
}

function log(msg,cls){
  const d=document.createElement("div");
  d.className=cls||"";
  d.textContent=new Date().toLocaleTimeString()+"  "+msg;
  $("log").prepend(d);
  while($("log").children.length>60)$("log").lastChild.remove();
}

function setDot(id,state){
  const e=$(id); e.className="dot"+(state?" "+state:"");
}

function hex(n){return "#"+(n&0xffffff).toString(16).padStart(6,"0");}

function showSpool(s,note){
  spool=s;
  $("note").style.display=note?"block":"none";
  $("note").textContent=note||"";
  const known=s&&s.source!=="Unknown tag";
  $("src").textContent=s?s.source:"waiting";
  $("src").className="badge"+(known?"":" unk");
  $("nm").textContent=known?`${s.vendor} ${s.mainType} ${s.subType}`:"Unrecognised tag";
  $("uid").textContent=s&&s.uid?`${s.cardType||"?"} - UID ${s.uid}`:"no tag";
  $("sw").style.background=s?hex(s.rgb):"#333";

  // Spoolman line: linked spool + how much is left on it.
  const linked=s&&s.spoolmanId>0;
  $("sm").hidden=!linked;
  if(linked){
    const g=s.remainingG||0;
    const pct=Math.max(0,Math.min(100,Math.round(g/1000*100)));
    $("sm").innerHTML=`<span class="chip">SPOOLMAN #${s.spoolmanId}</span>`
      +(g?`<span class="bar"><i style="width:${pct}%"></i></span><span>${g} g left</span>`:"");
  }
  // Offer to link only when there's an unlinked tag and Spoolman is configured.
  $("link").hidden=!(spoolmanOn && s && s.uid && !linked);

  if(!s)return;
  $("f-vendor").value=s.vendor||"";
  $("f-main").value=MAINS.includes(s.mainType)?s.mainType:"PLA";
  $("f-sub").value=SUBS.includes(s.subType)?s.subType:"Basic";
  $("f-color").value=hex(s.rgb);
  $("f-hmin").value=s.hotendMin; $("f-hmax").value=s.hotendMax;
  $("f-bed").value=s.bedTemp;    $("f-weight").value=s.weightG;
  $("f-alpha").value=s.alpha;    $("a-val").textContent=s.alpha;
  $("f-sku").value=s.sku||0;
  paintGate();
}

function formSpool(){
  return {
    reader:active,
    vendor:$("f-vendor").value||"Generic",
    mainType:$("f-main").value,
    subType:$("f-sub").value,
    rgb:parseInt($("f-color").value.slice(1),16),
    rgb2:spool?spool.rgb2:0,
    alpha:+$("f-alpha").value,
    hotendMin:+$("f-hmin").value, hotendMax:+$("f-hmax").value,
    bedTemp:+$("f-bed").value, weightG:+$("f-weight").value,
    sku:+$("f-sku").value,
    uid:spool?spool.uid:"", cardType:spool?spool.cardType:""
  };
}

$("f-alpha").oninput=e=>$("a-val").textContent=e.target.value;

$("send").onclick=async()=>{
  $("send").disabled=true;
  try{
    const r=await fetch("/api/send",{method:"POST",
      headers:{"Content-Type":"application/json"},
      body:JSON.stringify({channel,reader:active,spool:formSpool()})});
    const j=await r.json();
    // The board answers immediately and does the HTTP call to the printer from
    // its main loop; the outcome arrives as a websocket log line.
    if(j.queued)log(`queued for slot ${channel+1}...`);
    else if(!j.ok)log("send failed: "+j.error,"bad");
  }catch(e){log("send failed: "+e,"bad");}
  $("send").disabled=false;
};

$("rescan").onclick=()=>fetch("/api/rescan",{method:"POST"});
$("reslots").onclick=()=>fetch("/api/slots",{method:"POST"});
$("diaggo").onclick=()=>{
  if(!confirm("Reboot and run the reader for 5 minutes with WiFi off?\n\n"
    +"The board disappears from the network for 5 minutes and comes back by "
    +"itself. Watch the USB serial console for the result."))return;
  fetch("/api/radiotest",{method:"POST"});
  log("radio-off test starting - back in about 5 minutes","warn");
};

// ---- firmware update ----
$("fwgo").onclick=async()=>{
  const f=$("fwfile").files[0];
  if(!f){$("fwmsg").textContent="Pick a firmware .bin first.";return;}
  const fd=new FormData(); fd.append("firmware",f,f.name);
  let url="/api/ota";
  const pw=$("s-otapw").value;
  if(pw)url+="?pw="+encodeURIComponent(pw);
  $("fwbarwrap").hidden=false; $("fwbar").style.width="0%";
  $("fwgo").disabled=true; $("fwmsg").textContent="Uploading "+f.name+"...";
  try{
    const r=await fetch(url,{method:"POST",body:fd});
    const j=await r.json();
    if(!j.ok)$("fwmsg").textContent="Failed: "+(j.error||"unknown error");
  }catch(e){
    // The board reboots as soon as the image verifies, so the response often
    // never arrives. The websocket said "done" already if it worked.
  }
  $("fwgo").disabled=false;
};


// ---- update every box on the network -----------------------------------
// Eight boxes means a wrong file costs eight times what it used to, so the
// browser reads the image before it sends a byte of it. Three things it can
// know from the bytes alone: that it is an ESP32 app image at all, which chip
// it targets, and — from the marker the firmware bakes into .rodata — which
// project, version, transport and reader count it was built for.
//
// A wrong chip is refused outright. A wrong transport or reader count is
// survivable (the box boots, you just lose the reader) so it is offered
// unticked with the reason spelled out, rather than silently skipped.

const OTA_CHIP={0x17:"esp32c5",0x0d:"esp32c6",0x05:"esp32c3",0x0c:"esp32c2",
                0x09:"esp32s3",0x02:"esp32s2",0x00:"esp32",0x10:"esp32h2"};
const FP_TAG="U1SB-FINGERPRINT-v1|";

function fwInspect(buf){
  const b=new Uint8Array(buf), dv=new DataView(buf);
  if(b.length<4096)   return {err:"that file is too small to be a firmware image"};
  if(b[0]!==0xe9)     return {err:"not an ESP32 image — first byte is not 0xE9"};
  if(dv.getUint32(0x20,true)!==0xabcd5432)
    return {err:"no app descriptor at 0x20 — this looks like a bootloader or a "
               +"merged factory image, not the plain firmware.bin"};
  const id=dv.getUint16(0x0c,true);
  const chip=OTA_CHIP[id]||("chip id "+id);
  const txt=new TextDecoder("latin1").decode(b);
  const i=txt.indexOf(FP_TAG);
  if(i<0) return {err:"an ESP32 image for "+chip+", but not a u1-spool-bridge build "
                     +"— no build marker in it",chip};
  const kv={};
  txt.slice(i+FP_TAG.length,txt.indexOf("|end",i)).split("|").forEach(part=>{
    const e=part.indexOf("="); if(e>0)kv[part.slice(0,e)]=part.slice(e+1);
  });
  if(kv.tgt&&kv.tgt!==chip)
    return {err:"the image header says "+chip+" but the build marker says "+kv.tgt
               +" — refusing to guess"};
  return {chip,fw:kv.fw||"?",tgt:kv.tgt||chip,bus:kv.bus||"?",rc:kv.rc||"1",
          pins:kv.pins||"",size:b.length};
}

// Everything the plan needs about one box, self included.
//
// addr is stamped in by this box from the mDNS answer, so it is there whatever
// the peer runs. ip is a peer's own report and only exists from 1.12.0 — do not
// depend on it, or the updater works solely on boxes already updated. The
// .local fallback is last because the phone holding this page frequently
// cannot resolve it even when the box can.
function fwBoxes(){
  const list=[];
  if(fwSelf)list.push({...fwSelf,base:"",self:true});
  fleetPeers.forEach(p=>{
    const at=p.addr||p.ip||(p.host?p.host+".local":"");
    if(at)list.push({...p,at,base:"http://"+at,self:false});
  });
  return list;
}

function fwJudge(box,img){
  if(box.otaEnabled===false) return {go:false,hard:true, why:"OTA is switched off on this box"};
  if(box.target&&box.target!==img.tgt)
    return {go:false,hard:true, why:"this box is "+box.target+", the image is "+img.tgt};
  // Same chip, different board. The header cannot see this — a XIAO C5 and a
  // C5 devkit are both esp32c5 — so without the pin triple the image installs
  // happily and the reader goes quiet on the wrong GPIOs. Hard, because there
  // is no reason to want it: update a devkit box from a devkit box.
  if(box.pins&&img.pins&&box.pins!==img.pins){
    const gp=t=>"GPIO "+t.split(".").join("/");
    return {go:false,hard:true,why:"this box has its reader on "+gp(box.pins)
                                  +", the image is built for "+gp(img.pins)
                                  +" — same chip, different board"};
  }
  if(box.bus&&box.bus!==img.bus)
    return {go:false,hard:false,why:"box runs the "+box.bus+" build, image is "+img.bus
                                   +" — its reader would stop working"};
  if(box.rc&&String(box.rc)!==String(img.rc))
    return {go:false,hard:false,why:"box drives "+box.rc+" readers, image builds for "+img.rc};
  if(box.version===img.fw)  return {go:false,hard:false,why:"already on "+img.fw};
  // Older firmware reports no bus/target/rc at all, so the checks above simply
  // did not run. Say that out loud rather than letting a blank row read as a
  // clean bill of health.
  const blind=!box.bus&&!box.target;
  // A box from before the pin triple existed reports bus and target but no
  // pins, so the check above did not run. Say which check was skipped rather
  // than letting an unverified row look verified.
  const noPins=!blind&&!box.pins&&img.pins;
  return {go:true,hard:false,
          why:(box.version||"?")+" → "+img.fw
             +(blind?" · too old to report its build — check this is the "
                    +img.bus+" image":"")
             +(noPins?" · too old to report its wiring — check this image is "
                     +"for this board":"")};
}

function fwRow(box,plan,idx){
  const d=document.createElement("div"); d.className="fwb";
  // This box holds the image the others are fed from, so it is always in the
  // run and always last. Unticking it would leave nothing to push.
  const dis=plan.hard||box.self?"disabled":"";
  const chk=(plan.go||box.self)&&!plan.hard?"checked":"";
  d.innerHTML=`<input type="checkbox" data-i="${idx}" ${chk} ${dis}>`
    +`<span class="t"><b>${box.box||box.host||box.ip}${box.self?" (this box)":""}</b>`
    +`<span data-note="${idx}">${box.self&&!plan.hard
        ?plan.why+" · carries the image for the others, reboots last":plan.why}</span></span>`
    // This box is in the run whatever its version says, because it carries the
    // image, so it must not wear a SKIP badge while ticked and disabled.
    +`<span class="st ${plan.hard?"no":(plan.go||box.self)?"go":"wa"}" data-st="${idx}">`
    +`${plan.hard?"BLOCKED":(plan.go||box.self)?"READY":"SKIP"}</span>`;
  return d;
}

let fwSelf=null, fleetPeers=[], fwPlanState=null, fwRunning=false;

async function fwLoadSelf(){
  try{ fwSelf=await (await fetch("/api/brief",{cache:"no-store"})).json(); }catch(e){}
}

// The fleet list comes over the websocket, so ask and wait for it.
function fwWaitFleet(ms){
  return new Promise(res=>{
    if(fleetPeers.length)return res(true);
    let done=false;
    const t=setTimeout(()=>{if(!done){done=true;fwFleetWaiter=null;res(false);}},ms);
    fwFleetWaiter=()=>{if(!done){done=true;clearTimeout(t);res(true);}};
    fetch("/api/fleet");
  });
}
let fwFleetWaiter=null;

// onlyGroup limits the run to one group (plus this box, which has to be in it
// either way — it is the one holding the image the others are fed from).
async function fwOpenPlan(onlyGroup){
  if(fwRunning)return;
  const f=$("fwfile").files[0];
  if(!f){
    $("fwmsg").textContent="Pick a firmware .bin in the Firmware card first.";
    $("fwfile").scrollIntoView({behavior:"smooth",block:"center"});
    $("fwfile").focus({preventScroll:true});
    return;
  }

  $("fwmsg").textContent="Reading the image…";
  const img=fwInspect(await f.arrayBuffer());
  if(img.err){
    $("fwmsg").textContent="Refusing to send this: "+img.err;
    $("fwplan").hidden=true; return;
  }

  $("fwmsg").textContent="Looking for the other boxes…";
  await fwLoadSelf();
  // The box answers this by querying mDNS and then fetching /api/brief from
  // every peer it found, each with its own timeout. Across eight dryboxes that
  // is comfortably longer than the 6 s this used to allow, and timing out early
  // is indistinguishable from having no peers at all.
  const found=await fwWaitFleet(30000);

  let boxes=fwBoxes();
  if(onlyGroup)boxes=boxes.filter(b=>b.self||groupOf(b)===onlyGroup);
  const peersSeen=boxes.filter(b=>!b.self).length;
  if(!peersSeen){
    $("fwmsg").textContent=found
      ? "This box is the only one answering on mDNS. Check the others are "
       +"powered and on the same network and Wi-Fi band, then press Refresh "
       +"under Other boxes."
      : "The network scan has not answered yet. Press Refresh under Other "
       +"boxes, wait for the list to fill in, then try again.";
    if(!boxes.length)return;
  }
  const plans=boxes.map(b=>fwJudge(b,img));
  fwPlanState={img,boxes,plans,file:f};

  const el=$("fwplan"); el.hidden=false; el.innerHTML="";
  const cap=document.createElement("p"); cap.className="cap";
  cap.innerHTML=`<b>${f.name}</b> — ${img.fw}, ${img.tgt}, ${img.bus} build, `
               +`${img.rc} reader${img.rc==="1"?"":"s"}, `
               +`${(img.size/1024).toFixed(0)} kB. Your box updates last, so this page `
               +`keeps working until the rest are done.`;
  el.appendChild(cap);
  boxes.forEach((b,i)=>el.appendChild(fwRow(b,plans[i],i)));

  const row=document.createElement("div");
  row.className="row"; row.style.marginTop="12px";
  row.innerHTML=`<button id="fwrun">Update ticked boxes</button>`
               +`<button class="ghost" id="fwcancel">Cancel</button>`;
  el.appendChild(row);
  $("fwcancel").onclick=()=>{$("fwplan").hidden=true;$("fwmsg").textContent="";};
  $("fwrun").onclick=fwRun;

  // The peers are fed from THIS box's OTA slot, so an image this box cannot
  // take is an image it cannot pass on either — its own Update would reject
  // the wrong chip and the peers would never see a byte. Stop here and say
  // where to run it from, rather than letting it fail halfway.
  const selfIdx=boxes.findIndex(b=>b.self);
  const stuck=selfIdx>=0&&plans[selfIdx].hard;
  const peersReady=plans.filter((p,i)=>!boxes[i].self&&p.go).length;
  const n=plans.filter(p=>p.go).length;

  if(stuck&&peersReady){
    $("fwrun").disabled=true;
    $("fwmsg").textContent=`Can't run this from here — ${plans[selfIdx].why}, `
      +`and this box has to hold the image for the others. Install it on one of `
      +`those ${peersReady} boxes first, then press Update all boxes from that one.`;
    return;
  }

  $("fwmsg").textContent=(onlyGroup?`${onlyGroup}: `:"")
    +(n?`${n} box${n===1?"":"es"} ready.`:"nothing to update — review below.");
}
$("fwall").onclick=()=>fwOpenPlan(null);

function fwSet(i,state,note){
  const st=document.querySelector(`[data-st="${i}"]`);
  const nt=document.querySelector(`[data-note="${i}"]`);
  if(st){st.textContent=state;st.className="st "+(state==="DONE"?"go":
        state==="FAILED"?"no":"wa");}
  if(nt&&note!==undefined)nt.textContent=note;
}

// The box reboots the moment the image verifies, so the HTTP response is a
// coin flip — the connection is usually gone before it arrives. Don't trust it,
// in either direction: a box can answer {"ok":true} and still be running what
// it was running before. Ask it what it is now, instead.
//
// The honest signal is the version changing. Uptime is only the tiebreaker for
// the one case the version cannot settle — deliberately reflashing the version
// a box already has — and it is read fresh immediately before the upload,
// because an uptime captured when the fleet list was drawn is minutes stale by
// the time the eighth box is reached, and comparing against it would fail a
// perfectly good update.
function fwPost(base,file,pw,onPct){
  return new Promise(res=>{
    const x=new XMLHttpRequest();
    x.open("POST",base+"/api/ota"+(pw?"?pw="+encodeURIComponent(pw):""),true);
    x.timeout=180000;
    x.upload.onprogress=e=>{if(e.lengthComputable)onPct(Math.round(e.loaded*100/e.total));};
    const fin=()=>{let j={};try{j=JSON.parse(x.responseText);}catch(e){}
                   res({http:x.status,body:j});};
    x.onload=fin; x.onerror=()=>res({http:0,body:{}});
    x.ontimeout=()=>res({http:0,body:{},timeout:true});
    const fd=new FormData(); fd.append("firmware",file,file.name);
    x.send(fd);
  });
}

async function fwBrief(base){
  try{
    const r=await fetch(base+"/api/brief",{cache:"no-store"});
    if(r.ok)return await r.json();
  }catch(e){}
  return null;
}

async function fwConfirm(base,wantFw,beforeVer,preUp,ms){
  const t0=Date.now(); let sawOld=false, answered=false;
  while(Date.now()-t0<ms){
    await new Promise(r=>setTimeout(r,2000));
    const j=await fwBrief(base);
    if(!j)continue;                       // still down; that is expected for a while
    answered=true;
    const rebooted=(typeof j.uptime==="number"&&typeof preUp==="number")
                   ? j.uptime<preUp : false;
    if(j.version===wantFw&&(beforeVer!==wantFw||rebooted))return {ok:true,j};
    if(j.version===beforeVer)sawOld=true;
  }
  return {ok:false,why:sawOld?("still running "+beforeVer+" — the image did not take")
                     :answered?"came back on an unexpected version"
                              :"no answer after the upload"};
}

// One upload, to this box, which then hands the image to each peer itself.
//
// It used to be one upload per box, straight from here. That cannot work: a
// browser posting to a box it was not served from is a cross-origin request,
// and cross-origin is enforced by the RECEIVING box. Anything older than 1.12.0
// sends no CORS headers and has no OPTIONS route, so its catch-all redirects
// the preflight and the browser refuses to send the image at all — and those
// are precisely the boxes worth updating. Pushing from a box has no such rule,
// and costs the phone one upload instead of eight.
async function fwRun(){
  if(fwRunning||!fwPlanState)return;
  fwRunning=true;
  $("fwrun").disabled=true; $("fwcancel").disabled=true;
  $("fwgo").disabled=true; $("fwall").disabled=true;

  const {img,boxes,file}=fwPlanState;
  const pw=$("s-otapw").value;
  const picked=[...document.querySelectorAll('#fwplan input[type=checkbox]')]
      .filter(c=>c.checked).map(c=>+c.dataset.i);

  fwRowFor={};                       // peer address -> row index, for ws events
  const peers=[];
  picked.forEach(i=>{
    if(boxes[i].self)return;
    peers.push(boxes[i].at);
    fwRowFor[boxes[i].at]=i;
    fwSet(i,"QUEUED","waiting its turn");
  });
  const selfIdx=boxes.findIndex(b=>b.self);

  if(peers.length){
    const r=await fetch("/api/fleetplan",{method:"POST",
      headers:{"Content-Type":"application/json"},
      body:JSON.stringify({peers,pw,fw:img.fw})});
    const j=await r.json().catch(()=>({}));
    if(!j.ok){
      $("fwmsg").textContent="Could not set up the fleet update: "+(j.error||"unknown");
      fwRunning=false; $("fwrun").disabled=false; $("fwcancel").disabled=false;
      $("fwgo").disabled=false; $("fwall").disabled=false; return;
    }
  }

  if(selfIdx>=0)fwSet(selfIdx,"SENDING","uploading to this box…");
  $("fwmsg").textContent="Uploading once to this box…";

  let url="/api/ota"+(peers.length?"?fleet=1":"");
  if(pw)url+=(peers.length?"&":"?")+"pw="+encodeURIComponent(pw);

  const up=await new Promise(res=>{
    const x=new XMLHttpRequest();
    x.open("POST",url,true); x.timeout=180000;
    x.upload.onprogress=e=>{if(e.lengthComputable&&selfIdx>=0)
      fwSet(selfIdx,"SENDING","uploading "+Math.round(e.loaded*100/e.total)+"%");};
    const fin=()=>{let j={};try{j=JSON.parse(x.responseText);}catch(e){}res(j);};
    x.onload=fin; x.onerror=()=>res({}); x.ontimeout=()=>res({timeout:true});
    const fd=new FormData(); fd.append("firmware",file,file.name);
    x.send(fd);
  });

  if(up.ok===false){
    if(selfIdx>=0)fwSet(selfIdx,"FAILED",up.error||"refused");
    $("fwmsg").textContent="Upload refused: "+(up.error||"unknown error");
    fwRunning=false; $("fwrun").disabled=false; $("fwcancel").disabled=false;
    $("fwgo").disabled=false; $("fwall").disabled=false; return;
  }

  if(!peers.length){
    if(selfIdx>=0)fwSet(selfIdx,"WAITING","rebooting…");
    $("fwmsg").textContent="Installed. This box is rebooting.";
    setTimeout(()=>location.reload(),9000);
    return;                           // stays "running" until the reload
  }

  if(selfIdx>=0)fwSet(selfIdx,"WAITING","holding the image for the others");
  $("fwmsg").textContent="This box now has the image and is sending it on. "
                        +"It reboots last — watch the rows below.";
  // From here the box narrates over the websocket; see the fleetpush handler.
}

// Progress from the box doing the pushing.
let fwRowFor={};
function fwFleetEvent(m){
  if(m.state==="groupok"||m.state==="groupfail"||m.state==="groupdone"){
    // The same push backs both a group rename and a location-format rollout,
    // so report into whichever line the user is actually looking at.
    const line=($("s-locmsg").textContent||"").startsWith("Applying")
               ||($("s-locmsg").textContent||"").startsWith("Sent")
               ? "s-locmsg" : "fleetmsg";
    if(m.state==="groupdone"){
      $(line).textContent=`Applied on ${m.msg} of the other boxes.`;
      setTimeout(()=>{$(line).textContent="";},8000);
      fleetAuto();
    }else if(m.state==="groupfail"){
      $(line).textContent=`${m.peer}: ${m.msg}`;
    }
    return;
  }
  if(m.state==="done"){
    const sel=fwPlanState?fwPlanState.boxes.findIndex(b=>b.self):-1;
    if(sel>=0)fwSet(sel,"WAITING","rebooting into the new firmware");
    $("fwmsg").textContent="Other boxes finished ("+m.msg+"). This box is "
                          +"rebooting now; the page will come back on its own.";
    setTimeout(()=>location.reload(),12000);
    return;
  }
  const i=fwRowFor[m.peer];
  if(i===undefined)return;
  if(m.state==="sending")     fwSet(i,"SENDING","receiving the image…");
  else if(m.state==="waiting")fwSet(i,"WAITING","rebooting…");
  else if(m.state==="ok")     fwSet(i,"DONE",m.msg||"updated");
  else if(m.state==="fail")   fwSet(i,"FAILED",m.msg||"failed");
}

// ---- Spoolman location preview -----------------------------------------
// The same substitution the firmware does, so what you read here is what
// Spoolman will file the spool under.
function locFormat(fmt,slot,group,box){
  let f=(fmt||"").trim()?fmt:"U1 slot {slot}";
  const sub=(t,v)=>{let i=0;for(;;){const p=f.indexOf(t,i);if(p<0)break;
    f=f.slice(0,p)+v+f.slice(p+t.length);i=p+v.length;}};
  sub("{slot}",String(slot));
  sub("{group}",group||box||"");
  sub("{box}",box||"");
  return f.trim();
}
function locPreview(){
  const grp=$("s-group")?$("s-group").value.trim():"";
  const box=$("s-box")?$("s-box").value.trim():"";
  const eff=grp||(fwSelf&&fwSelf.printer)||"";
  const fmt=$("s-locfmt").value;
  const out=locFormat(fmt,2,eff,box||"this box");
  if(!out){ $("s-locprev").textContent="That format produces an empty location."; return; }
  // A format carrying {slot} gives each box its own location, which is the
  // thing that fills Spoolman with one heading per box. Say so here rather
  // than letting it be discovered in Spoolman.
  $("s-locprev").innerHTML=/\{slot\}/.test(fmt)
    ? `Slot 2 files under <b>${out}</b> &mdash; a separate Spoolman location `
      +`per slot, so this group appears as four headings, not one.`
    : `Every spool from this group files under <b>${out}</b>.`;
}
["s-locfmt","s-group","s-box"].forEach(id=>{
  const el=$(id); if(el)el.addEventListener("input",locPreview);
});
$("s-locgrp").onclick=()=>{
  $("s-locfmt").value="{group}";
  locPreview();
  $("s-locfmt").focus();
};

// One format, every box. Pushed box-to-box like a group rename, and each box
// then re-files whatever it currently has loaded so Spoolman catches up
// instead of holding the location from before the change.
$("s-locall").onclick=async()=>{
  const fmt=$("s-locfmt").value.trim();
  if(!fmt){$("s-locmsg").textContent="Put a format in the field first.";return;}
  const peers=fleetPeers.map(p=>p.addr||p.ip).filter(Boolean);
  $("s-locall").disabled=true;
  $("s-locmsg").textContent=`Applying to this box and ${peers.length} other`
                           +`${peers.length===1?"":"s"}…`;
  try{
    const r=await fetch("/api/fleetfmt",{method:"POST",
      headers:{"Content-Type":"application/json"},
      body:JSON.stringify({locationFmt:fmt,peers,includeSelf:true})});
    const j=await r.json();
    $("s-locmsg").textContent=j.ok
      ? (peers.length?"Sent. Watch the boxes report back below."
                     :"Applied to this box. No other boxes found yet.")
      : "Failed: "+(j.error||"unknown");
  }catch(e){ $("s-locmsg").textContent="Failed: no answer from this box"; }
  $("s-locall").disabled=false;
};

// ---- tag dump ----
$("dumpgo").onclick=()=>{
  $("dumpgo").disabled=true;
  $("dumpout").hidden=false;
  $("dumpout").textContent="Reading all 16 sectors — this takes 20-40 s…";
  $("dumpcopy").hidden=true;
  fetch("/api/dump",{method:"POST"}).catch(()=>{
    $("dumpout").textContent="Could not reach this box.";
    $("dumpgo").disabled=false;
  });
};
$("dumpcopy").onclick=async()=>{
  const t=$("dumpout").textContent;
  try{ await navigator.clipboard.writeText(t); $("dumpcopy").textContent="Copied"; }
  catch(e){
    // clipboard needs a secure context, and a box on plain http is not one
    const r=document.createRange(); r.selectNodeContents($("dumpout"));
    const sel=getSelection(); sel.removeAllRanges(); sel.addRange(r);
    $("dumpcopy").textContent="Selected — press copy";
  }
  setTimeout(()=>{$("dumpcopy").textContent="Copy";},2500);
};

// ---- fleet ----
$("refleet").onclick=()=>{
  $("fleet").innerHTML='<p class="hint">Looking for other boxes...</p>';
  fetch("/api/fleet");    // answers over the websocket
};
// ---- the fleet, grouped ------------------------------------------------
// A box's group is whatever it has been given in Settings; failing that, the
// printer it feeds, which for this setup means the boxes arrange themselves
// without anyone typing anything. This box is drawn alongside the others,
// because a group of four slots reads wrong with a hole where you are standing.
function groupOf(b){
  const g=(b.group||"").trim();
  if(g)return g;
  const p=(b.printer||"").trim();
  return p?p:"No printer set";
}

const fgShut=new Set();
try{ (JSON.parse(localStorage.getItem("u1-shut")||"[]")).forEach(g=>fgShut.add(g)); }catch(e){}
function fgPersist(){
  try{ localStorage.setItem("u1-shut",JSON.stringify([...fgShut])); }catch(e){}
}

function peerTile(p,isSelf){
  const a=document.createElement("a");
  a.className="peer"+(p.present?"":" empty")+(isSelf?" me":"");
  a.href=isSelf?"#":`http://${p.addr||p.ip||(p.host+".local")}/`;
  const sw=p.present?`style="background:${hex(p.rgb||0)}"`:"";
  a.innerHTML=`<span class="sw2" ${sw}></span><span class="t">`
    +`<b>${p.box||p.host||"?"}${isSelf?" (this box)":""}</b>`
    +`<span>slot ${p.slot} &middot; ${p.present?(p.spool||"loaded"):"empty"}`
    +`${p.present&&p.remainingG?" &middot; "+p.remainingG+" g":""}</span>`
    +`<span>${p.printer||"no printer"}${p.printerOk===false?" (unreachable)":""}`
    +`${p.version&&refVersion()&&p.version!==refVersion()
        ? ` &middot; <em style="color:var(--warn)">fw ${p.version}</em>` : ""}</span></span>`;
  return a;
}

function renderFleet(peers,err){
  // Kept for "Update all boxes", which needs each peer's address, transport
  // and chip — not just the tile drawn here.
  fleetPeers=peers||[];
  if(fwFleetWaiter){const w=fwFleetWaiter;fwFleetWaiter=null;w();}

  const el=$("fleet");
  if(!fwSelf){                       // first delivery can beat the self fetch
    fwLoadSelf().then(()=>{if(fwSelf)renderFleet(fleetPeers,err);});
  }
  const all=fwSelf?[{...fwSelf,__self:true},...fleetPeers]:fleetPeers.slice();
  if(!all.length){
    el.innerHTML=`<p class="hint">${err||"Looking for other boxes…"}</p>`;return;
  }

  const groups=new Map();
  all.forEach(b=>{
    const g=groupOf(b);
    if(!groups.has(g))groups.set(g,[]);
    groups.get(g).push(b);
  });
  const names=[...groups.keys()].sort((a,b)=>a.localeCompare(b));

  el.innerHTML="";
  names.forEach(name=>{
    const boxes=groups.get(name)
        .sort((a,b)=>(a.slot||0)-(b.slot||0)||(a.box||"").localeCompare(b.box||""));
    const loaded=boxes.filter(b=>b.present).length;
    const ref=refVersion();
    const stale=boxes.filter(b=>b.version&&ref&&b.version!==ref).length;

    const wrap=document.createElement("div");
    wrap.className="fgrp"+(fgShut.has(name)?" shut":"");

    const head=document.createElement("div");
    head.className="gh";
    head.innerHTML=`<span class="caret">&#9660;</span><b title="Tap the name to rename this group everywhere">${name}</b>`
      +`<span class="sum">${loaded}/${boxes.length} loaded`
      +`${stale?` &middot; <em style="color:var(--warn)">${stale} on older fw</em>`:""}</span>`;
    head.querySelector("b").onclick=e=>{e.stopPropagation(); fgRename(head,name,boxes);};
    const btn=document.createElement("button");
    btn.className="ghost"; btn.type="button"; btn.textContent="Update";
    btn.title="Update just the boxes in this group";
    btn.onclick=e=>{e.stopPropagation(); fwOpenPlan(name);};
    head.appendChild(btn);
    head.onclick=()=>{
      if(fgShut.has(name))fgShut.delete(name); else fgShut.add(name);
      wrap.classList.toggle("shut"); fgPersist();
    };

    const body=document.createElement("div");
    body.className="gb";
    boxes.forEach(b=>body.appendChild(peerTile(b,!!b.__self)));

    wrap.appendChild(head); wrap.appendChild(body);
    el.appendChild(wrap);
  });

  if(err)el.insertAdjacentHTML("beforeend",`<p class="hint">${err}</p>`);
  paintLoaded();   // the loaded card fills its gaps from exactly this data
}

// Renaming a group renames it on every box in it, so you do not type the same
// label into four Settings pages. This box applies its own copy and hands the
// rest to the peers itself — a browser cannot POST settings to a box it was not
// served from.
function fgRename(head,name,boxes){
  const b=head.querySelector("b");
  if(head.querySelector("input.ren"))return;
  const inp=document.createElement("input");
  inp.className="ren"; inp.value=name; inp.maxLength=23;
  inp.onclick=e=>e.stopPropagation();
  b.replaceWith(inp); inp.focus(); inp.select();

  let done=false;
  const finish=async commit=>{
    if(done)return; done=true;
    const val=inp.value.trim();
    if(!commit||!val||val===name){ fleetAuto(); renderFleet(fleetPeers,""); return; }
    const peers=boxes.filter(x=>!x.__self).map(x=>x.addr||x.ip).filter(Boolean);
    const mine=boxes.some(x=>x.__self);
    $("fleetmsg").textContent=`Renaming to “${val}” on ${boxes.length} box`
                             +`${boxes.length===1?"":"es"}…`;
    try{
      const r=await fetch("/api/group",{method:"POST",
        headers:{"Content-Type":"application/json"},
        body:JSON.stringify({group:val,peers,includeSelf:mine})});
      const j=await r.json();
      if(!j.ok){$("fleetmsg").textContent="Rename failed: "+(j.error||"unknown");return;}
      // Redraw now with the new name rather than waiting for the next scan:
      // the box refuses to rescan more often than every 10 s, so the list could
      // otherwise sit there showing the old name for half a minute.
      const inGroup=new Set(boxes.map(x=>x.addr||x.ip||x.host));
      fleetPeers=fleetPeers.map(x=>inGroup.has(x.addr||x.ip||x.host)?{...x,group:val}:x);
      if(mine&&fwSelf)fwSelf={...fwSelf,group:val};
      if($("s-group"))$("s-group").value=mine?val:$("s-group").value;
      renderFleet(fleetPeers,"");
    }catch(e){ $("fleetmsg").textContent="Rename failed: no answer from this box"; return; }
    // Peers confirm over the websocket; rescan once they have had a moment.
    setTimeout(fleetAuto,3000);
  };
  inp.onkeydown=e=>{
    if(e.key==="Enter")finish(true);
    else if(e.key==="Escape")finish(false);
  };
  inp.onblur=()=>finish(true);
}

// Ask on load, then keep it current. The box refuses to rescan more than once
// every 10 s, and a hidden tab does not ask at all — each scan stalls that
// box's reader for as long as the mDNS query and the per-peer fetches take.
let fleetTimer=null;
function fleetAuto(){
  if(document.visibilityState!=="visible")return;
  if(fwRunning)return;                     // never mid-update
  fetch("/api/fleet").catch(()=>{});
}
function fleetAutoStart(){
  fwLoadSelf().then(()=>{fleetAuto();});
  if(fleetTimer)clearInterval(fleetTimer);
  fleetTimer=setInterval(fleetAuto,30000);
  document.addEventListener("visibilitychange",()=>{
    if(document.visibilityState==="visible")fleetAuto();
  });
}

// ---- Spoolman spool picker ----
$("link").onclick=()=>{
  $("picker").classList.add("on");
  $("splist").innerHTML='<p class="hint">Loading spools...</p>';
  fetch("/api/spoolman/spools");   // result arrives over the websocket
};
$("pclose").onclick=()=>$("picker").classList.remove("on");
$("picker").onclick=e=>{if(e.target===$("picker"))$("picker").classList.remove("on");};
$("spsearch").oninput=()=>renderSpools();

function renderSpools(){
  const q=$("spsearch").value.toLowerCase();
  const hits=spools.filter(s=>!q||
    (s.label+" "+s.material+" "+s.location).toLowerCase().includes(q));
  if(!hits.length){$("splist").innerHTML='<p class="hint">No spools match.</p>';return;}
  $("splist").innerHTML="";
  hits.slice(0,200).forEach(s=>{
    const d=document.createElement("div");
    d.className="sprow";
    const rem=s.remaining>=0?`${s.remaining} g`:"weight unknown";
    d.innerHTML=`<span class="sw2" style="background:#${(s.color||"666").replace('#','')}"></span>`
      +`<span class="t"><b>${s.label}</b><span>#${s.id} - ${s.material||"?"} - ${rem}`
      +`${s.location?" - "+s.location:""}</span></span>`
      +(s.tagged?'<em>has a tag</em>':'');
    d.onclick=async()=>{
      $("picker").classList.remove("on");
      const r=await fetch("/api/spoolman/link",{method:"POST",
        headers:{"Content-Type":"application/json"},
        body:JSON.stringify({spoolId:s.id})});
      const j=await r.json();
      if(j.queued)log(`linking tag to #${s.id}...`);
      else if(!j.ok)log("link failed: "+j.error,"bad");
    };
    $("splist").appendChild(d);
  });
}

$("mode").onchange=async e=>{
  mode=+e.target.value;
  await fetch("/api/settings",{method:"POST",
    headers:{"Content-Type":"application/json"},
    body:JSON.stringify({triggerMode:mode})});
  log("trigger mode: "+e.target.selectedOptions[0].text.toLowerCase());
  paintSlots();
};

$("arm").onclick=async()=>{
  await fetch("/api/arm?channel="+channel);
};

$("test").onclick=async()=>{
  await fetch("/api/ping");
  log("pinging printer...");
};

$("s-backend").onchange=paintBackendHint;

$("save").onclick=async()=>{
  const body={
    printerHost:$("s-host").value, printerPort:+$("s-port").value,
    printerBackend:+$("s-backend").value,
    readerChannel, wifiSsid:$("s-ssid").value,
    scanIntervalMs:+$("s-scan").value, wifiBand:+$("s-band").value,
    wifiTxPower:+$("s-txp").value,
    spoolmanEnabled:$("s-smon").checked, spoolmanHost:$("s-smhost").value,
    spoolmanPort:+$("s-smport").value, spoolmanSetLocation:$("s-smloc").checked,
    spoolmanNoteLoads:$("s-smnote").checked, locationFmt:$("s-locfmt").value,
    ntpServer:$("s-ntp").value, timezone:$("s-tz").value,
    otaEnabled:$("s-ota").checked,
    boxName:$("s-box").value, groupName:$("s-group").value,
    sendOnBoot:$("s-boot").checked,
    dwellMs:+$("s-dwell").value, absenceMs:+$("s-abs").value,
    cooldownS:+$("s-cool").value,
    scanValidS:+$("s-valid").value, armTimeoutS:+$("s-armto").value,
    statePollMs:+$("s-poll").value,
    forceGenericVendor:$("s-generic").checked, sendCardUid:$("s-uid").checked,
    extraKeys:$("s-keys").value.split(/[,\s]+/).filter(k=>k.length===12)
  };
  if($("s-pass").value)body.wifiPass=$("s-pass").value;
  if($("s-otapw").value)body.otaPassword=$("s-otapw").value;
  if($("s-key").value)body.apiKey=$("s-key").value;
  const j=await(await fetch("/api/settings",{method:"POST",
    headers:{"Content-Type":"application/json"},body:JSON.stringify(body)})).json();
  log(j.ok?"settings saved":"save failed: "+j.error,j.ok?"ok":"bad");
  if(j.reboot)log("rebooting to join the new network...","warn");
};

async function loadSettings(){
  const s=await(await fetch("/api/settings")).json();
  $("s-host").value=s.printerHost; $("s-port").value=s.printerPort;
  $("s-backend").value=String(s.printerBackend||0); paintBackendHint();
  $("s-ssid").value=s.wifiSsid;    $("s-scan").value=s.scanIntervalMs;
  $("s-band").value=s.wifiBand||0;
  $("s-txp").value=s.wifiTxPower||0;
  // The band controls only exist on a dual-band part (ESP32-C5 and friends).
  $("band-row").hidden=!s.dualBand;
  $("s-generic").checked=s.forceGenericVendor;
  $("s-uid").checked=s.sendCardUid;
  $("s-keys").value=(s.extraKeys||[]).join(",");
  $("s-smon").checked=s.spoolmanEnabled;
  $("s-smhost").value=s.spoolmanHost||"";
  $("s-smport").value=s.spoolmanPort||7912;
  $("s-smloc").checked=s.spoolmanSetLocation;
  $("s-smnote").checked=s.spoolmanNoteLoads;
  $("s-locfmt").value=s.locationFmt||"";
  locPreview();
  $("s-ntp").value=s.ntpServer||"";
  $("s-ota").checked=s.otaEnabled!==false;
  $("s-otapw").placeholder=s.otaPasswordSet?"(set — blank to keep, - to clear)":"(none set)";
  $("s-tz").value=s.timezone||"";
  mode=s.triggerMode|0; $("mode").value=String(mode);
  $("s-box").value=s.boxName||""; $("s-boot").checked=s.sendOnBoot;
  $("s-group").value=s.groupName||"";
  $("s-abs").value=s.absenceMs;
  $("s-dwell").value=s.dwellMs; $("s-cool").value=s.cooldownS;
  $("s-valid").value=s.scanValidS; $("s-armto").value=s.armTimeoutS;
  $("s-poll").value=s.statePollMs;
  readerChannel=s.readerChannel||readerChannel;
  channel=readerChannel[active]|0;
  paintSlots(); paintTabs();
  if(!s.printerHost)$("setpanel").open=true;
}

function connect(){
  const ws=new WebSocket(`ws://${location.host}/ws`);
  ws.onopen=()=>log("connected");
  ws.onclose=()=>{log("disconnected, retrying...","warn");setTimeout(connect,2000);};
  ws.onmessage=e=>{
    const m=JSON.parse(e.data);
    if(m.ev==="status"){
      setDot("d-rd",m.reader?(m.resets?"idle":"on"):"off");
      // A reader that keeps needing bus resets still works, but the wiring is
      // telling you something. Amber, not green, and say how many.
      $("t-rd").textContent=m.resets?`Reader (${m.resets} resets)`:"Reader";
      setDot("d-wf",m.wifi?"on":"off");
      setDot("d-pr",m.printer?"on":(m.printerKnown?"off":"idle"));
      $("t-wf").textContent=m.ip||"WiFi";
      $("t-pr").textContent=m.printerHost||"Printer";
      readers=m.readerCount||1; lanes=m.lanes||lanes;
      if(lanes.length)readerChannel=lanes.map(l=>(l.slot|0)-1);
      if(m.box){
        $("title").textContent = readers>1
          ? `${m.box} → slots ${lanes.map(l=>l.slot).join(" + ")}`
          : `${m.box} → slot ${lanes.length?lanes[0].slot:1}`;
      }
      paintTabs();
      mode=m.triggerMode|0; $("mode").value=String(mode);
      armed=!!m.armed; armedCh=m.armedChannel|0;
      pending=!!m.pending; pendingAge=m.pendingAgeS|0;
      chan=m.chan||chan; chanKnown=!!m.chanKnown;
      paintSlots();
      spoolmanOn=!!m.spoolmanOn;
      $("p-sm").hidden=!m.spoolmanOn;
      setDot("d-sm",m.spoolman?"on":"off");
      $("p-band").hidden=!m.dualBand;
      if(m.dualBand){
        setDot("d-bd",m.band?(m.band==="5 GHz"?"on":"idle"):"off");
        $("t-bd").textContent=m.band?`${m.band} ${m.rssi} dBm`:"offline";
        // A band-restricted join that had to fall back is the kind of thing you
        // only notice weeks later, so say it where it cannot be missed.
        if(m.bandFellBack){
          setDot("d-bd","off");
          $("t-bd").textContent=`${m.band} — ${m.bandWanted==2?"5 GHz":"2.4 GHz"} only was requested`;
        }
      }
      myVersion=m.version||"";
      $("fwnow").textContent=`Running ${m.version} on ${m.chip||"esp32"}. `
        +`Upload a firmware.bin to update over the air — nothing is committed `
        +`until the whole image verifies.`;
      if(m.backend){backendName=m.backend;backendKnown=!!m.backendKnown;
        backendConfirmed=!!m.backendConfirmed;backendPinned=!!m.backendPinned;
        presenceOnly=!!m.presenceOnly;paintBackendHint();paintBackendPill();}
      if(m.slots){slots=m.slots;slotsKnown=!!m.slotsKnown;slotsErr=m.slotsErr||"";paintLoaded();}
      $("diagnow").textContent=m.resets
        ? `This reader has had to reset its I2C bus ${m.resets} time(s) since boot.`
        : "No bus resets since boot.";
      $("ver").textContent=`${m.chip||"esp32"} - firmware ${m.version} - `
        +`PN532 fw ${m.pn532||"n/a"} - uptime ${m.uptime}s`;
    }else if(m.ev==="tag"){
      const r=m.reader|0;
      laneSpools[r]=m.spool;
      if(m.spool&&m.spool.uid)myUids.add(m.spool.uid.toUpperCase());
      if(r===active)showSpool(m.spool,m.note);
      paintTabs();
      log(m.spool&&m.spool.source!=="Unknown tag"
        ? `read ${m.spool.source} tag: ${m.spool.vendor} ${m.spool.mainType}`
        : "unrecognised tag "+(m.spool?m.spool.uid:""),
        m.spool&&m.spool.source!=="Unknown tag"?"ok":"warn");
    }else if(m.ev==="spools"){
      spools=m.spools||[];
      if(m.error){$("splist").innerHTML=`<p class="hint">${m.error}</p>`;log("Spoolman: "+m.error,"bad");}
      else renderSpools();
    }else if(m.ev==="ota"){
      $("fwbarwrap").hidden=false;
      if(m.pct>=0)$("fwbar").style.width=m.pct+"%";
      if(m.state==="progress")$("fwmsg").textContent=`Writing... ${m.pct}%`;
      else if(m.state==="start")$("fwmsg").textContent="Receiving "+(m.msg||"image")+"...";
      else if(m.state==="done"){
        $("fwmsg").textContent="Installed — rebooting. This page will reconnect.";
        log("firmware installed, rebooting","ok");
      }else if(m.state==="error"){
        $("fwmsg").textContent="Failed: "+m.msg+" (the box is still running the old firmware)";
        log("OTA failed: "+m.msg,"bad");
      }
    }else if(m.ev==="dump"){
      $("dumpout").hidden=false;
      $("dumpout").textContent=m.text||"";
      $("dumpcopy").hidden=false;
      $("dumpgo").disabled=false;
    }else if(m.ev==="fleetpush"){
      fwFleetEvent(m);
    }else if(m.ev==="fleet"){
      renderFleet(m.peers||[],m.error);
    }else if(m.ev==="log"){
      log(m.msg,m.level);
    }
  };
}

paintSlots(); loadSettings(); connect(); fleetAutoStart();
</script>
</body></html>
)HTMLPAGE";
