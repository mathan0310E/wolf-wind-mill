/*
   WINDX - Smart IoT Windmill Controller
   ESP32 Wireless Motor Control Station

   Hardware:
     - ESP32 DevKit
     - L298N motor driver (Channel A)
     - Simple DC motor with windmill blades
     - External DC motor power supply (NOT powered from the ESP32)

   WiFi Access Point:
     - SSID: WINDX-ESP32
     - Password: windmill123
     - IP:    192.168.4.1

   Connections (L298N Channel A):
     ESP32 GPIO 25  -> ENA
     ESP32 GPIO 26  -> IN1
     ESP32 GPIO 27  -> IN2
     ESP32 GND         -> L298N GND(common ground)
     L298N OUT1       -> DC motor terminal 1
     L298N OUT2       -> DC motor terminal 2
     External DC +     -> L298N motor supply input (+12V/VS)
     External DC -     -> L298N GND

   PWM: GPIO  25,, ~1000 Hz,, 8-bit resolution (0-255).

   Dependencies:
     - #include <WiFi.h>
     - #include <WebServer.h>
#include <cstring>
     (both ship with the ESP32 Arduino core; no extra libraries needed).



   Author: WINDX IoT Lab Demo
*/
#include <WiFi.h>
#include <WebServer.h>
#include <cstring>

// ---------------------------------------------------------------------------
// PIN DEFINITIONS
// ---------------------------------------------------------------------------
const int ENA    =  25;   // L298N Enable A -> ESP32 PWM speed control
const int IN1    =  26;   // L298N Input  1  (direction A)
const int IN2    =  27;   // L298N Input  2  (direction B)
const int PWMFREQ = 1000;  // ~1000 Hz PWM
const int PWMRES  =  8;      // 8-bit resolution (0-255)

// ---------------------------------------------------------------------------
// WiFi ACCESS POINT SETTINGS
// ---------------------------------------------------------------------------
const char* AP_SSID     = "WINDX-ESP32";
const char* AP_PASSWORD = "windmill123";
IPAddress localIP(192, 168,  4,  1);
IPAddress gatewayIP(192, 168,  4,  1);
IPAddress subnetMask(255, 255, 255, 0);

// ---------------------------------------------------------------------------
// MOTOR STATE MACHINE
// ---------------------------------------------------------------------------
enum MotorState { STOPPED, FORWARD, REVERSE, REVERSING, EMERGENCY };
MotorState state = STOPPED;

int  targetSpeed    =   0;   // requested speed 0-100 (kept even when stopped)
int  currentPWM     =   0;   // PWM actually written to ENA (0-255)
int  pendingAfterSwitch = REVERSE; // direction requested during REVERSING
bool demoRunning     =false;
bool emergencyLatched =false;
unsigned long reversingSince =0;   // timestamp of REVERSING start
unsigned long demoNextAt    =0;   // timestamp of next demo step

WebServer server(80);

// ---------------------------------------------------------------------------
// DEMO SEQUENCE: forward 40% / 5s, then speed  70% / 5s, stop 2s,
//                 reverse 50% / 5s, stop;  total ~17s.
//                 (See demoActions below.)

const int  DEMO_STEPS =5;
const char* demoActions[DEMO_STEPS]={
  "FORWARD", "SPEED", "STOP", "REVERSE", "STOP"
};
const int demoValues[DEMO_STEPS]={ 40,     70,    0,     50,      0  };
const unsigned long demoDurations[DEMO_STEPS]={ 5000,  5000, 2000,  5000,  1  };
int  demoStep =0;

// ---------------------------------------------------------------------------
// FORWARD DECLARATIONS
// ---------------------------------------------------------------------------
String htmlPage(void);

void handleRoot(void);
void handleStatus(void);
void handleMotor(void);
void handleSpeed(void);
void handleEmergency(void);
void handleReset(void);
void handleDemoStart(void);
void handleDemoStop(void);

void setupWiFi(void);
void setupMotor(void);
void motorStop(void);
void motorForward(void);
void motorReverse(void);
void setMotorSpeed(long v);
void applyDirectionState(int i1, int i2);
void applyPWM(void);
long   clampLong(long v, long lo, long hi);
void setTargetSpeed(long v);
void motorForwardInternal(void);
void motorReverseInternal(void);
void motorStopInternal(void);
void startForward(void);
void startReverse(void);
void stopMotor(void);
void beginReversing(MotorState afterSwitch);
void handleReversing(void);
void activateEmergency(void);
void resetEmergency(void);
void startDemo(void);
void stopDemo(void);
void stopDemoInternal(void);

void applyDemoAction(int step);
void runDemoStep(void);
void serialLog(const char* msg);
String statusJson();
const char* stateName();

// ===========================================================================
// SETUP
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("## WINDX Smart Windmill");
  Serial.println("## ESP32 Wireless Motor Control Station");

  setupMotor();
  setupWiFi();
  Serial.println("Motor: STOPPED");
 }

// ===========================================================================
// WIFI + WEB SERVER SETUP
// ===========================================================================
void setupWiFi() {
  Serial.println("WiFi AP Starting...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(localIP, gatewayIP, subnetMask);
  boolean apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
   if (apOk) {
    Serial.print("WiFi AP Started\nSSID: ");
     Serial.println(AP_SSID);
     Serial.print("IP: ");
     Serial.println(WiFi.softAPIP());
   } else {
    Serial.println("ERROR: WiFi AP failed to start");
   }

  server.on("/",             handleRoot);
   server.on("/status",        handleStatus);
   server.on("/motor",         handleMotor);
   server.on("/speed",         handleSpeed);
   server.on("/emergency",     handleEmergency);
   server.on("/reset",         handleReset);
   server.on("/demo/start",    handleDemoStart);
   server.on("/demo/stop",     handleDemoStop);
   server.begin();
   Serial.println("Web Server Started");
   Serial.println("Dashboard: http://192.168.4.1");
 }

 // ===========================================================================
 // MAIN LOOP
 // ===========================================================================
 void loop() {
   server.handleClient();
   handleReversing();
   if (demoRunning) { runDemoStep(); }
 }

// ===========================================================================
// SERIAL LOG HELPER
// ===========================================================================
void serialLog(const char* msg) {
  Serial.print("[WINDX] ");
  Serial.println(msg);
}

// ===========================================================================
// MOTOR PRIMITIVES
// ===========================================================================
void applyDirectionState(int i1, int i2) {
  digitalWrite(IN1, i1);
  digitalWrite(IN2, i2);
}

void applyPWM() {
  // 0-100% -> 0-255 (8-bit PWM). targetSpeed is always clamped 0-100.
  currentPWM = (targetSpeed * 255) / 100;
  ledcWrite(0, currentPWM);
}

long clampLong(long v, long lo, long hi) {
  if (v < lo) return lo;
  return (v > hi) ? hi : v;
}

void setTargetSpeed(long v) {
  long clamped = clampLong(v, 0, 100);
  // Clamp 0-100%: NEVER allow invalid PWM values.
  targetSpeed = (int)clamped;
  // If the motor is already moving, apply the new speed immediately.
  if ((state == FORWARD) || (state == REVERSE)) {
    applyPWM();
  }
}

void motorForwardInternal() {
  applyDirectionState(HIGH, LOW);
  applyPWM();
  state = FORWARD;
  serialLog("FORWARD");
}

void motorReverseInternal() {
  applyDirectionState(LOW, HIGH);
  applyPWM();
  state = REVERSE;
  serialLog("REVERSE");
}

// Coast-to-stop: IN1 = IN2 = LOW and PWM = 0 (no brake, no jerk).
void motorStopInternal() {
  applyDirectionState(LOW, LOW);
  ledcWrite(0, 0);
  currentPWM = 0;
  state = STOPPED;
  serialLog("STOP");
}

// Spec-aligned public wrappers (section 30 naming):
void motorStop()       { motorStopInternal(); }
void motorForward()    { startForward(); }
void motorReverse()    { startReverse(); }
void setMotorSpeed(long v) { setTargetSpeed(v); }

// ===========================================================================
// PUBLIC MOTOR COMMANDS (used by HTTP handlers and demo mode)
// ===========================================================================
void startForward() {
  if (state == EMERGENCY) return;
  if (state == FORWARD) { setTargetSpeed(targetSpeed); return; }
  if (state == REVERSING) { return; }   // ignore; switch in progress
  if (state == REVERSE) { beginReversing(FORWARD); return; }
  if (state == STOPPED) {
    if (targetSpeed == 0) setTargetSpeed(70);
    applyDirectionState(HIGH, LOW);
    applyPWM();
    state = FORWARD;
    serialLog("FORWARD");
  }
}

void startReverse() {
  if (state == EMERGENCY) return;
  if (state == REVERSE) { setTargetSpeed(targetSpeed); return; }
  if (state == REVERSING) { return; }   // ignore; switch in progress
  if (state == FORWARD) { beginReversing(REVERSE); return; }
  if (state == STOPPED) {
    if (targetSpeed == 0) setTargetSpeed(70);
    applyDirectionState(LOW, HIGH);
    applyPWM();
    state = REVERSE;
    serialLog("REVERSE");
  }
}

void stopMotor() {
  if (state == EMERGENCY) return;
  if (state == REVERSING) { return; }   // let the current switch finish
  if (demoRunning) {
    stopDemoInternal();
    delay(80);
  }
  motorStopInternal();
}

// ===========================================================================
// SAFE DIRECTION SWITCHING (FORWARD <-> REVERSE via REVERSING)
// ===========================================================================
void beginReversing(MotorState afterSwitch) {
  if (state == EMERGENCY) return;
  // Coast to a dead stop before flipping the H-bridge direction.
  applyDirectionState(LOW, LOW);
  ledcWrite(0, 0);
  currentPWM = 0;
  state = REVERSING;
  pendingAfterSwitch = afterSwitch;
  reversingSince = millis();
  serialLog("REVERSING");
}

void handleReversing() {
  // Ensures a full stop + pause before applying the new direction.
  if (state != REVERSING) return;
  if (millis() - reversingSince < 700UL) return;
  if (pendingAfterSwitch == FORWARD) {
    applyDirectionState(HIGH, LOW);
    state = FORWARD;
    serialLog("FORWARD");
  } else {
    applyDirectionState(LOW, HIGH);
    state = REVERSE;
    serialLog("REVERSE");
  }
  applyPWM();
}

// ===========================================================================
// EMERGENCY STOP / RESET
// ===========================================================================
void activateEmergency() {
  applyDirectionState(LOW, LOW);
  ledcWrite(0, 0);
  currentPWM = 0;
  state = EMERGENCY;
  stopDemoInternal();
  emergencyLatched = true;
  demoRunning = false;
  serialLog("EMERGENCY STOP");
}

void resetEmergency() {
  // Only RESET can clear the latched emergency state.
  if (!emergencyLatched) return;
  emergencyLatched = false;
  state = STOPPED;
  applyDirectionState(LOW, LOW);
  ledcWrite(0, 0);
  currentPWM = 0;
  serialLog("RESET - READY");
}

// ===========================================================================
// DEMO MODE (automatic sequence using ONLY motor control)
// ===========================================================================
void stopDemoInternal() {
  demoRunning = false;
  demoStep = 0;
}

void startDemo() {
  // Demo never bypasses the emergency-stop state.
  if (state == EMERGENCY) return;
  if (demoRunning) return;
  demoRunning = true;
  demoStep = 0;
  demoNextAt = 0;
  serialLog("DEMO START");
  runDemoStep();
}

void runDemoStep() {
  if (!demoRunning) return;
  if (state == EMERGENCY) {
    stopDemoInternal();
    return;
  }
  if ((long)(millis() - demoNextAt) < 0) return;
  applyDemoAction(demoStep);
  demoNextAt = millis() + demoDurations[demoStep];
  demoStep++;
  if (demoStep >= DEMO_STEPS) {
    demoRunning = false;
    demoNextAt = 0;
  }
}

void applyDemoAction(int step) {
  if (strcmp(demoActions[step], "FORWARD") == 0) {
    setTargetSpeed(demoValues[step]);
    startForward();
  } else if (strcmp(demoActions[step], "REVERSE") == 0) {
    setTargetSpeed(demoValues[step]);
    startReverse();
  } else if (strcmp(demoActions[step], "SPEED") == 0) {
    setTargetSpeed(demoValues[step]);
    serialLog("DEMO SPEED");
  } else if (strcmp(demoActions[step], "STOP") == 0) {
    motorStopInternal();
  }
}

// ===========================================================================
// MOTOR GPIO SETUP
// ===========================================================================
void setupMotor() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(ENA, HIGH);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcSetup(0, PWMFREQ, PWMRES);
  ledcAttachPin(ENA, 0);
  ledcWrite(0,  0);
  currentPWM = 0;
  state = STOPPED;
}

String htmlPage() {
  const char* html = R"WINDXHTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WINDX - Smart Windmill Controller</title>
<style>
:root{
  --bg:#050a18;--panel:rgba(10,18,36,.72);--line:rgba(0,229,255,.18);
  --cyan:#00e5ff;--green:#22e6a0;--red:#ff3b5c;--orange:#ffb347;--blue:#3ea6ff;--txt:#dce8f5;--dim:#7f93b0;
}
*{box-sizing:border-box;margin:0;padding:0}
body{
  background:radial-gradient(1200px 700px at 50% 0%,#0a1a38 0%,var(--bg) 55%);
  color:var(--txt);font-family:system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  min-height:100vh;padding:14px;
}
.wrap{max-width:1200px;margin:0 auto}
header.top{
  display:flex;flex-wrap:wrap;align-items:center;gap:14px;justify-content:space-between;
  background:var(--panel);border:1px solid var(--line);border-radius:18px;
  padding:16px 20px;backdrop-filter:blur(8px);box-shadow:0 10px 30px rgba(0,0,0,.35);
}
.logo b{color:var(--cyan);font-size:26px;letter-spacing:2px}
.logo span{display:block;font-size:12px;color:var(--dim);letter-spacing:1px}
.pill{
  display:inline-flex;align-items:center;gap:8px;font-size:13px;font-weight:700;
  padding:9px 14px;border-radius:999px;border:1px solid var(--line);
  background:rgba(10,20,40,.6);
}
.pill .dot{width:10px;height:10px;border-radius:50%;background:var(--dim)}
.pill.online .dot{background:var(--green);box-shadow:0 0 10px var(--green)}
.pill.offline .dot{background:var(--red);box-shadow:0 0 10px var(--red)}
.grid{display:grid;gap:14px;margin-top:14px}
.col{display:grid;gap:14px;align-content:start}
.card{
  background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:16px;
  backdrop-filter:blur(8px);box-shadow:0 10px 26px rgba(0,0,0,.3);
}
.card h2{font-size:11px;letter-spacing:2px;color:var(--cyan);text-transform:uppercase;margin-bottom:10px}
.center{text-align:center}
.windmill{width:100%;max-width:340px;margin:0 auto;display:block;height:auto}
.wm-rot{transform-origin:74px 118px}
.wm-rot.forward{animation:spin 2.4s linear infinite}
.wm-rot.reverse{animation:spinr 2.4s linear infinite}
.wm-rot.paused{animation-play-state:paused!important}
@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}
@keyframes spinr{from{transform:rotate(360deg)}to{transform:rotate(0deg)}}
.windmill-cap{margin-top:10px;font-size:12px;letter-spacing:2px;color:var(--cyan);opacity:.85;text-transform:uppercase}
.status-big{margin-top:10px;text-align:center}
.status-big .st{font-size:16px;font-weight:800;letter-spacing:1px}
.status-big .st.run{color:var(--green)}
.status-big .st.stop{color:var(--red)}
.status-big .st.reverse{color:var(--blue)}
.status-big .st.reversing{color:var(--orange)}
.status-big .st.emergency{color:var(--red)}
.status-big .dir{font-size:12px;color:var(--dim);margin-top:6px}
.btns{display:grid;grid-template-columns:1fr;gap:10px;margin-top:4px}
.btn{
  border:none;border-radius:14px;font-size:16px;font-weight:800;color:#041020;
  padding:16px 10px;cursor:pointer;letter-spacing:1px;transition:transform .12s,filter .12s;
}
.btn:active{transform:scale(.96)}
.btn .ico{font-size:20px;vertical-align:-2px}
.btn-fwd{background:linear-gradient(135deg,#2ee66f,#19bd8f)}
.btn-rev{background:linear-gradient(135deg,#3ea6ff,#1d6fd8)}
.btn-stop{background:linear-gradient(135deg,#ff5b79,#e02245);color:#fff}
.btn-busy{opacity:.55;filter:grayscale(.5);pointer-events:none}
.speedbar input{width:100%;accent-color:var(--cyan)}
.speedval{font-size:34px;font-weight:800;color:var(--cyan);text-align:center;margin:4px 0 8px}
.presets{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:10px}
.pre{
  background:rgba(0,229,255,.08);border:1px solid var(--line);border-radius:10px;
  color:var(--cyan);font-weight:700;padding:9px 4px;cursor:pointer;font-size:13px;
}
.emerg{
  width:100%;margin-top:14px;border:none;border-radius:14px;padding:15px 10px;
  font-size:15px;font-weight:800;color:#fff;cursor:pointer;letter-spacing:1px;
  background:linear-gradient(135deg,#ff3b5c,#c01035);box-shadow:0 6px 18px rgba(255,59,92,.35);
}
.emerg:active{transform:scale(.97)}

.reset-btn{
  width:100%;margin-top:10px;border:1px solid #00e5ff;border-radius:14px;padding:15px 10px;
  font-size:15px;font-weight:800;color:#00e5ff;cursor:pointer;letter-spacing:1px;
  background:rgba(0,229,255,.06);
}
.reset-btn:active{transform:scale(.97)}

.modegrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.demo-wrap{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}
.demo-btn{border:none;border-radius:12px;padding:13px 6px;font-weight:800;font-size:13px;cursor:pointer}
.demo-start{background:linear-gradient(135deg,#ffb347,#ff7a18);color:#231300}
.demo-stopb{background:linear-gradient(135deg,#5b6b8f,#35405f);color:#fff}
.mode-tab{border:1px solid var(--line);background:rgba(10,20,40,.6);border-radius:12px;padding:13px 6px;color:var(--dim);font-weight:800;cursor:pointer;text-align:center;font-size:13px}
.mode-tab.on{color:#041020;background:linear-gradient(135deg,#00e5ff,#00a8cc);border-color:transparent}
.rov{display:flex;justify-content:space-between;gap:10px;padding:7px 2px;border-bottom:1px dashed rgba(127,147,176,.18);font-size:13px}
.rov:last-child{border-bottom:none}
.rov b{color:#fff;font-weight:600}
.log{height:150px;overflow-y:auto;font-size:12px;color:var(--dim);line-height:1.7;background:rgba(0,0,0,.25);border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.log .t{color:var(--cyan);margin-right:6px}
.hint{font-size:11px;color:var(--dim);text-align:center;margin-top:8px}
@media(min-width:1024px){
  .grid{grid-template-columns:1fr 1.05fr 0.95fr}
  header.top{padding:18px 26px}
}
@media(max-width:560px){
  .presets{grid-template-columns:repeat(2,1fr)}
  .modegrid,.demo-wrap{grid-template-columns:1fr}
}
</style>
</head>
<body>
<div class="wrap">
  <header class="top">
    <div class="logo"><b>WINDX</b><span>Smart IoT Windmill Controller</span></div>
    <div class="pill online" id="conn"><span class="dot"></span><span id="connTxt">ESP32 ONLINE</span></div>
    <span style="font-size:12px;color:var(--dim)">192.168.4.1</span>
  </header>
  <div class="grid">
    <div class="col">
      <div class="card center">
        <h2>Wind Turbine</h2>
        <svg class="windmill" viewBox="0 0 220 220" xmlns="http://www.w3.org/2000/svg">
          <circle cx="110" cy="210" r="38" fill="none" stroke="rgba(0,229,255,.25)" stroke-width="3"/>
          <line x1="110" y1="176" x2="110" y2="118" stroke="rgba(127,147,176,.6)" stroke-width="6"/>
          <g class="wm-rot paused" id="rotor">
            <g id="blades">
              <line x1="74" y1="118" x2="74" y2="34" stroke="var(--cyan)" stroke-width="9" stroke-linecap="round"/>
              <line x1="74" y1="118" x2="146" y2="206" stroke="var(--cyan)" stroke-width="9" stroke-linecap="round"/>
              <line x1="74" y1="118" x2="2" y2="206" stroke="var(--cyan)" stroke-width="9" stroke-linecap="round"/>
            </g>
            <circle cx="74" cy="118" r="12" fill="#0d1a35" stroke="var(--cyan)" stroke-width="4"/>
          </g>
          <g>
            <line x1="74" y1="196" x2="74" y2="176" stroke="rgba(0,229,255,.85)" stroke-width="8"/>
            <line x1="74" y1="118" x2="30" y2="98" stroke="rgba(0,229,255,.5)" stroke-width="2"/>
            <line x1="74" y1="118" x2="118" y2="98" stroke="rgba(0,229,255,.5)" stroke-width="2"/>
          </g>
        </svg>
        <div id="spinTxt" class="windmill-cap">STOPPED</div>
        <div class="status-big">
          <div class="st stop" id="bigStatus">STOPPED</div>
          <div class="dir" id="bigDir">Direction: STOP</div>
        </div>
      </div>
      <div class="card">
        <h2>Status</h2>
        <div class="status-big" style="margin:0">
          <div class="st stop" id="stStatus">STOPPED</div>
          <div class="dir" id="stDir">Direction: STOP</div>
          <div class="dir" id="stSpeed">Speed: 0%</div>
        </div>
      </div>
    </div>
    <div class="col">
      <div class="card">
        <h2>Motor Control</h2>
        <div class="btns">
          <button class="btn btn-fwd" id="bFwd"><span class="ico">&#9654;</span> FORWARD</button>
          <button class="btn btn-stop" id="bStop"><span class="ico">&#9632;</span> STOP</button>
          <button class="btn btn-rev" id="bRev"><span class="ico">&#9664;</span> REVERSE</button>
        </div>
      </div>
      <div class="card">
        <h2>Motor Speed</h2>
        <div class="speedval" id="speedVal">70%</div>
        <div class="speedbar"><input type="range" id="speed" min="0" max="100" value="70"></div>
        <div class="presets">
          <button class="pre" data-v="25">25%</button>
          <button class="pre" data-v="50">50%</button>
          <button class="pre" data-v="75">75%</button>
          <button class="pre" data-v="100">100%</button>
        </div>
        <button class="emerg" id="bEmerg">&#128680; EMERGENCY STOP</button>
        <button class="reset-btn" id="bReset">RESET / CLEAR</button>
      </div>
    </div>
    <div class="col">
      <div class="card">
        <h2>Control Mode</h2>
        <div class="modegrid">
          <div class="mode-tab on" id="modeManual">MANUAL</div>
          <div class="mode-tab" id="modeDemo">DEMO</div>
        </div>
        <div class="demo-wrap">
          <button class="demo-btn demo-start" id="bDemoStart">&#9654; START DEMO</button>
          <button class="demo-btn demo-stopb" id="bDemoStop">&#9632; STOP DEMO</button>
        </div>
      </div>
      <div class="card">
        <h2>Motor Information</h2>
        <div id="motInfo"></div>
      </div>
      <div class="card">
        <h2>System</h2>
        <div id="sysInfo"></div>
      </div>
    </div>
  </div>
  <div class="grid" style="grid-template-columns:1fr">
    <div class="card">
      <h2>Command Log</h2>
      <div class="log" id="log"></div>
      <div class="hint">RPM sensor: Not installed</div>
    </div>
  </div>
</div>
<script>
var logN=0;
function ts(){var d=new Date();function p(n){return(n<10?'0':'')+n}return p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds())}
function addLog(m){var L=document.getElementById('log');var e=document.createElement('div');e.innerHTML='<span class="t">['+ts()+']</span>'+m;L.insertBefore(e,L.firstChild);while(L.children.length>20)L.removeChild(L.lastChild)}
function busy(id,on){var b=document.getElementById(id);if(on)b.classList.add('btn-busy');else b.classList.remove('btn-busy')}
function setConn(ok){var p=document.getElementById('conn');if(ok){p.className='pill online';document.getElementById('connTxt').textContent='ESP32 ONLINE'}else{p.className='pill offline';document.getElementById('connTxt').textContent='ESP32 OFFLINE'}}
var lastAction=0;
function send(url,msg){fetch(url,{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);if(msg)addLog(msg);busy('bFwd',false);busy('bStop',false);busy('bRev',false);busy('bDemoStart',false);busy('bDemoStop',false);busy('bEmerg',false);busy('bReset',false)}).catch(function(err){setConn(false);addLog('Command failed: '+err.message);busy('bFwd',false);busy('bStop',false);busy('bRev',false);busy('bDemoStart',false);busy('bDemoStop',false);busy('bEmerg',false);busy('bReset',false)})}
function cmd(action,msg){busy('bFwd',action==='forward'||action==='start');busy('bRev',action==='reverse');busy('bStop',action==='stop');busy('bDemoStart',action==='demoStart');busy('bDemoStop',action==='demoStop');send('/motor?action='+action,msg)}
function setStatus(d){setConn(true);
  var st=document.getElementById('bigStatus');var st2=document.getElementById('stStatus');
  var d1=document.getElementById('bigDir');var d2=document.getElementById('stDir');
  var dir=d.direction||'STOP';
  var sp=document.getElementById('stSpeed');sp.textContent='Speed: '+d.speed+'%';
  document.getElementById('speedVal').textContent=d.speed+'%';
  var slider=document.getElementById('speed');if(parseInt(slider.value)!==d.speed)slider.value=d.speed;
  if(d.emergency){st.textContent='EMERGENCY STOP';st.className='st emergency';st2.textContent='EMERGENCY STOP';st2.className='st emergency';d1.textContent='Direction: STOP';d2.textContent='Direction: STOP';document.getElementById('rotor').className='wm-rot paused';document.getElementById('modeManual').className='mode-tab on';document.getElementById('modeDemo').className='mode-tab';return}
  document.getElementById('modeManual').className='mode-tab on';
  document.getElementById('modeDemo').className='mode-tab';
  if(dir==='FORWARD'){st.textContent='RUNNING';st.className='st run';st2.textContent='RUNNING';st2.className='st run'}
  else if(dir==='REVERSE'){st.textContent='RUNNING';st.className='st run';st2.textContent='RUNNING';st2.className='st run'}
  else if(dir==='REVERSING'){st.textContent='REVERSING...';st.className='st reversing';st2.textContent='REVERSING...';st2.className='st reversing'}
  else{st.textContent='STOPPED';st.className='st stop';st2.textContent='STOPPED';st2.className='st stop'}
  if(dir==='FORWARD'){d1.textContent='Direction: FORWARD';d2.textContent='Direction: FORWARD'}
  else if(dir==='REVERSE'){d1.textContent='Direction: REVERSE';d2.textContent='Direction: REVERSE'}
  else if(dir==='REVERSING'){d1.textContent='Direction: REVERSING...';d2.textContent='Direction: REVERSING...'}
  else{d1.textContent='Direction: STOP';d2.textContent='Direction: STOP'}
  var rot=document.getElementById('rotor');rot.className='wm-rot '+(dir==='FORWARD'?'forward':(dir==='REVERSE'?'reverse':'paused'));document.getElementById('spinTxt').textContent=(dir==='FORWARD'?'CLOCKWISE':(dir==='REVERSE'?'COUNTER-CLOCKWISE':(dir==='REVERSING'?'REVERSING...':'STOPPED')));
  var mi=document.getElementById('motInfo');
  mi.innerHTML='<div class="rov"><span>Direction</span><b>'+dir+'</b></div>'+
    '<div class="rov"><span>PWM</span><b>'+d.pwm+' / 255</b></div>'+
    '<div class="rov"><span>Speed</span><b>'+d.speed+'%</b></div>'+
    '<div class="rov"><span>Motor</span><b>'+(d.emergency?'EMERGENCY STOP':(dir==='STOP'||dir==='STOPPED'?'STOPPED':'RUNNING'))+'</b></div>'+
    '<div class="rov"><span>Control</span><b>'+(d.mode||'MANUAL')+'</b></div>'+
    '<div class="rov"><span>RPM sensor</span><b>Not installed</b></div>';
  var si=document.getElementById('sysInfo');
  si.innerHTML='<div class="rov"><span>ESP32</span><b>ONLINE</b></div>'+
    '<div class="rov"><span>Wi-Fi</span><b>WINDX-ESP32</b></div>'+
    '<div class="rov"><span>IP</span><b>192.168.4.1</b></div>'+
    '<div class="rov"><span>Driver</span><b>L298N</b></div>'+
    '<div class="rov"><span>Motor</span><b>DC MOTOR</b></div>'+
    '<div class="rov"><span>Channel</span><b>A</b></div>'+
    '<div class="rov"><span>PWM</span><b>GPIO 25</b></div>'+
    '<div class="rov"><span>Direction</span><b>GPIO 26 / GPIO  27</b></div>';
}
function setModeTabs(m){document.getElementById('modeManual').className='mode-tab'+(m==='MANUAL'?' on':'');document.getElementById('modeDemo').className='mode-tab'+(m==='DEMO'?' on':'')}
document.getElementById('bFwd').addEventListener('click',function(){cmd('forward','Motor started - FORWARD')});
document.getElementById('bRev').addEventListener('click',function(){cmd('reverse','Motor started - REVERSE')});
document.getElementById('bStop').addEventListener('click',function(){cmd('stop','Motor stopped')});
document.getElementById('bEmerg').addEventListener('click',function(){busy('bFwd',true);busy('bRev',true);busy('bStop',true);fetch('/emergency',{cache:'no-store'}).then(function(){addLog('EMERGENCY STOP');setStatus({direction:'STOP',speed:0,pwm:0,emergency:true,mode:'MANUAL'})}).catch(function(){setConn(false)})});
document.getElementById('bDemoStart').addEventListener('click',function(){busy('bDemoStart',true);fetch('/demo/start',{cache:'no-store'}).then(function(){addLog('DEMO started');setTimeout(poll,300)}).catch(function(err){setConn(false);addLog('Command failed: '+err.message)})});
document.getElementById('bDemoStop').addEventListener('click',function(){busy('bDemoStop',true);fetch('/demo/stop',{cache:'no-store'}).then(function(){addLog('DEMO stopped')}).catch(function(err){setConn(false);addLog('Command failed: '+err.message)})});

document.getElementById('bReset').addEventListener('click',function(){busy('bFwd',true);busy('bRev',true);busy('bStop',true);fetch('/reset',{cache:'no-store'}).then(function(){addLog('EMERGENCY cleared - READY');setTimeout(poll,300)}).catch(function(err){setConn(false);addLog('Command failed: '+err.message)})});
document.getElementById('speed').addEventListener('input',function(){var v=this.value;document.getElementById('speedVal').textContent=v+'%';fetch('/speed?value='+v,{cache:'no-store'}).then(function(){addLog('Speed changed - '+v+'%')}).catch(function(err){setConn(false);addLog('Command failed: '+err.message)})});
Array.prototype.forEach.call(document.querySelectorAll('.pre'),function(b){b.addEventListener('click',function(){var v=b.getAttribute('data-v');document.getElementById('speed').value=v;document.getElementById('speedVal').textContent=v+'%';fetch('/speed?value='+v,{cache:'no-store'}).then(function(){addLog('Speed changed - '+v+'%')}).catch(function(err){setConn(false);addLog('Command failed: '+err.message)})})});
function poll(){fetch('/status',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}).then(function(d){setStatus(d)}).catch(function(){setConn(false);addLog('Connection lost - retrying')})}setInterval(poll,1500);poll();
addLog('System connected');
</script>
</body>
</html>
)WINDXHTML";
  return String(html);
}

// ===========================================================================
// HTTP HANDLERS
// ===========================================================================
String statusJson() {
  String s = String("{\"online\":true,\"direction\":\"") + stateName() +
            String("\",\"speed\":") + String(targetSpeed) +
            String(",\"pwm\":") + String(currentPWM) +
            String(",\"emergency\":") + (emergencyLatched ? String("true") : String("false")) +
            String(",\"mode\":\"") + (demoRunning ? String("DEMO") : String("MANUAL")) +
            String("\"}");
  return s;
}
const char* stateName() {
  switch (state) {
    case FORWARD:    return "FORWARD";
    case REVERSE:    return "REVERSE";
    case REVERSING:  return "REVERSING";
    case EMERGENCY: return "EMERGENCY";
    case STOPPED:
    default:           return "STOP";
  }
}
void handleRoot()   { server.send(200, "text/html", htmlPage()); }
void handleStatus() { server.send(200, "application/json", statusJson()); }
void handleMotor() {
  String act = server.arg("action");
  if (act == "forward")     { startForward(); server.send(200, "text/plain", "OK"); }
  else if (act == "reverse")  { startReverse(); server.send(200, "text/plain", "OK"); }
  else if (act == "stop")     { stopMotor();      server.send(200, "text/plain", "OK"); }
  else { server.send(400, "text/plain", "UNKNOWN ACTION"); }
}
void handleSpeed() {
  long v = server.arg("value").toInt();
  v = clampLong(v, 0, 100);
  setTargetSpeed(v);
  server.send(200, "text/plain", "OK");
}
void handleEmergency() { activateEmergency(); server.send(200, "text/plain", "OK"); }
void handleReset()    { resetEmergency();  server.send(200, "text/plain", "OK"); }
void handleDemoStart(){ startDemo();        server.send(200, "text/plain", "OK"); }
void handleDemoStop() { stopDemoInternal(); motorStop(); server.send(200, "text/plain", "OK"); }
void stopDemo() { stopDemoInternal(); }
