<img width="1113" height="1458" alt="wolf wind mill connection" src="https://github.com/user-attachments/assets/87101153-be61-406a-ba4b-f80d1c6720a6" />
# WINDX - Smart IoT Windmill Controller

**ESP32 Wireless Motor Control Station**

A complete, self-contained IoT laboratory project. An ESP32 creates
its own Wi-Fi Access Point and hosts a web dashboard. A phone connects
directly to the ESP32 and the dashboard controls a DC motor (windmill blade)
through an L298N H-bridge driver.

No internet, no cloud, no sensors, no databases are used.

## Deliverables

| File | Purpose |
| ---- | ------- |
| `WINDX_Smart_IoT_Windmill_Controller.ino` | Single-file Arduino sketch (ESP32) with embedded web dashboard |
| `README.md` | This document: wiring, safety, usage, testing, troubleshooting |

---

## 1. Hardware Required

1. ESP32 DevKit (ESP32-WROOM-32)
2. L298N motor driver module
3. Simple DC motor (6-12 V typically - check your motor label first)
4. External DC motor power supply (appropriate for your motor)
5. Windmill blade attached to the motor shaft
6. Breadboard and jumper wires
7. Micro-USB cable (power + upload for the ESP32)

No sensors, Firebase, MQTT, databases, cloud services, or extra hardware
are used or required.

---

## 2. Wiring Table (L298N Channel A)

| L298N terminal   | Connect to                     |
| ---------------- | ------------------------------ |
| ENA              | ESP32 GPIO 25                  |
| IN1              | ESP32 GPIO 26                  |
| IN2              | ESP32 GPIO 27                  |
| GND              | ESP32 GND (common ground - MUST be shared) |
| OUT1             | DC motor terminal 1            |
| OUT2             | DC motor terminal 2            |
| +12V / VS        | External DC supply positive    |
| GND              | External DC supply negative    |
| 5V regulator out | DO NOT USE for the ESP32       |

The motor supply voltage must match YOUR motor. If the motor says 6 V,
use a 6 V supply. Never assume 12 V. The L298N is powered from the
external supply only.

### ESP32 Pin Mapping

| ESP32 GPIO | L298N   | Function                    |
| ---------- | ------- | --------------------------- |
| 25         | ENA     | PWM speed control (8-bit, 1000 Hz) |
| 26         | IN1     | Direction control A1        |
| 27         | IN2     | Direction control A2        |
| GND        | GND     | Common ground               |

---

## 3. L298N Setup - Important Steps

1. **Remove the ENA jumper.** The ENA jumper shorts ENA to 5 V and
   disables PWM. With the jumper removed, GPIO 25 controls motor speed
   via PWM.
2. Common ground: connect L298N GND to ESP32 GND. Without this the
   control signals have no reference and the motor will not run reliably.
3. Motor supply: connect a suitable external DC supply (voltage matching
   your motor rating) to the +12V/VS input. The ESP32 must NEVER power
   the motor.
4. The IN1/IN2 jumpers on your module serve as level selectors only in
   some boards. For this sketch we are wiring GPIO 26/27 directly, so
   remove those jumpers as well if present (check your module silkscreen)
   so the ESP32 signals reach the inputs.

---

## 4. How the System Works

- The ESP32 boots into **Access Point mode** (SSID `WINDX-ESP32`,
  password `windmill123`, IP `192.168.4.1`).
- It hosts a small HTTP web server. All dashboard files (HTML/CSS/JS) are
  embedded in one `.ino` file - no external resources needed.
- A phone connected to the AP opens `http://192.168.4.1`.
- The dashboard polls `/status` every 1.5 s to show live state.
- Commands (forward / reverse / stop / speed / emergency / demo) are sent
  via simple HTTP GET endpoints.
- The ESP32 drives the L298N H-bridge: GPIO 26/27 select direction and
  GPIO 25 provides PWM speed on ENA. The H-bridge then switches the
  external motor supply onto the motor terminals accordingly.

---

## 5. API Endpoints

| Endpoint                            | Effect                |
| ----------------------------------- | --------------------- |
| `GET /`                             | Dashboard HTML        |
| `GET /status`                       | JSON: online, direction, speed, pwm, emergency, mode |
| `GET /motor?action=forward`         | Start forward         |
| `GET /motor?action=reverse`         | Start reverse         |
| `GET /motor?action=stop`            | Stop motor            |
| `GET /speed?value=70`               | Set speed 0-100% (clamped) |
| `GET /emergency`                    | Emergency stop (latched) |
| `GET /reset`                        | Clear emergency stop  |
| `GET /demo/start`                   | Start auto demo       |
| `GET /demo/stop`                    | Stop auto demo        |

---

## 6. Motor Control and H-Bridge Basics

**PWM (Pulse Width Modulation):** The ESP32 outputs a square wave on
GPIO 25 (1000 Hz, 8-bit resolution). A duty cycle of 0-255 maps to
0-100% speed (PWM = speed x 255 / 100). The ENA input of the L298N
gates the motor power: higher duty = higher average voltage = faster
motor.

**H-bridge forward/reverse:** L298N channel A is an H-bridge. Four
switches steer current through the motor in either direction:

| IN1  | IN2  | Result                     |
| ---- | ---- | -------------------------- |
| HIGH | LOW  | Forward (OUT1 -> OUT2)     |
| LOW  | HIGH | Reverse (OUT2 -> OUT1)     |
| LOW  | LOW  | Coast / stop (no brake)    |
| HIGH | HIGH | Brake (not used in WINDX)  |

The two inputs are never both HIGH in this firmware.

**Safe direction switching:** reversing instantly at high speed can
damage the motor and H-bridge. The sketch transitions
FORWARD -> REVERSING -> REVERSE: it coasts to a stop (PWM = 0,
IN1 = IN2 = LOW), waits about 700 ms, then applies the new direction
with the current speed. The dashboard shows `REVERSING...` during the
switch. The same applies to REVERSE -> FORWARD.

---

## 7. Upload Instructions

1. Install the **Arduino IDE** (2.x recommended) from arduino.cc.
2. In **Boards Manager**, install **esp32 by Espressif Systems**, then
   select board **ESP32 Dev Module**. Install the USB-UART driver
   (CP210x or CH340) if your OS does not detect the board.
3. Open `WINDX_Smart_IoT_Windmill_Controller.ino`.
4. Select the correct **Port** under Tools > Port.
5. Upload Speed 115200 (default is fine). Flash Size: keep the default.
6. Click **Upload**. Open the Serial Monitor at 115200 baud. You should
   see:

```
##WINDX Smart Windmill
WiFi AP Started
SSID: WINDX-ESP32
IP: 192.168.4.1
Web Server Started
Motor: STOPPED
```

---

## 8. Phone Connection Instructions

1. Power the ESP32 from a USB power bank, charger, or lab supply. Keep
   the motor on its own external supply when running real hardware.
2. Open the phone **Wi-Fi settings**.
3. Join **WINDX-ESP32** with password **windmill123**.
4. Open the browser and go to `http://192.168.4.1`.
5. The dashboard loads. The header must show a green `ESP32 ONLINE`.
6. Use FORWARD / STOP / REVERSE, the speed slider, the 25/50/75/100%
   presets, and EMERGENCY STOP. Try DEMO MODE for an automatic sequence.

---

## 9. Testing Procedure (bench)

1. With NO motor connected, upload the sketch and confirm the Serial
   output shows AP started.
2. Open the dashboard and verify `ESP32 ONLINE` and the windmill renders.
3. Place a small LED + resistor (or a multimeter) across OUT1/OUT2 in
   place of the motor. Press FORWARD: voltage appears. Move the slider:
   brightness / voltage changes smoothly.
4. Connect the real DC motor with an external supply sized for it.
5. FORWARD: shaft spins one way. The slider changes speed smoothly.
6. STOP: shaft coasts to a stop.
7. REVERSE while running: the dashboard shows `REVERSING...` for about
   0.7 s, then the shaft spins the other way smoothly (no jerk).
8. EMERGENCY STOP: the motor stops immediately, the dashboard shows
   `EMERGENCY STOP`, and all motor commands are ignored until
   `RESET / CLEAR` is pressed.
9. DEMO MODE runs: forward at 40%, speed to 70%, stop, reverse at 50%,
   stop. STOP DEMO halts it at any time.
10. Turn off the phone Wi-Fi while the dashboard is open: within about
    3 s the indicator flips to `ESP32 OFFLINE` and the motor is never
    shown as running when the ESP32 is unreachable. Rejoin to resume.

---

## 10. Troubleshooting

### Motor does not rotate

- Check the motor supply is on and sized correctly for your motor.
- Check L298N GND to ESP32 GND common ground connection.
- Check the ENA jumper was REMOVED (GPIO 25 PWM needs it gone).
- Check OUT1/OUT2 to the motor terminals.
- Check IN1/IN2 wiring (GPIO 26/27).
- Watch the dashboard: the status card must show RUNNING with PWM > 0.
- For bench tests using only USB power, the motor cannot run from USB.
  It requires the external motor supply.

### Dashboard does not open

- The phone must be connected to `WINDX-ESP32`, not your home Wi-Fi.
- Use the exact URL `http://192.168.4.1` (plain http, no https).
- The ESP32 must be powered; the Serial Monitor should show the AP
  boot messages. If not, press the EN/RESET button and watch again.

### Forward and reverse are opposite

- Swap the OUT1 / OUT2 wires to the motor, OR swap the IN1 / IN2
  mapping in the sketch (GPIO 26 / GPIO 27).

### Motor only runs at full speed

- The ENA jumper is still ON. Remove it. PWM cannot lower speed while
  the jumper shorts ENA to +5 V.

### ESP32 resets when the motor starts

- Motor startup current demand / noise is collapsing the supply. The
  motor must be powered from its OWN external supply sized for it, with
  a common ground to the ESP32. Never feed the motor from the ESP32 or
  from USB. If spikes persist, add a large electrolytic capacitor
  (100-470 uF, rated above the motor voltage) across the motor supply
  terminals.

### Serial Monitor shows nothing

- Check that the baud rate is 115200.
- On some boards, press the EN/RESET button once after upload.

---

## 11. Hardware Safety

- **NEVER** power the motor directly from ESP32 GPIO pins. GPIO pins
  carry only small control signals.
- The motor receives power ONLY through the L298N, from the external
  motor supply.
- Use an external supply voltage appropriate for YOUR motor (read the
  motor label).
- The L298N may become hot during operation. Provide airflow, do not
  block it, and stop if it overheats or smells.
- Keep fingers, hair, ties, and tools away from spinning blades. Wait
  for the blade to fully stop before touching anything.
- Do not reverse the motor instantly at high speed. The firmware
  already enforces a REVERSING pause; do not bypass it.
- Disconnect power before rewiring.
- The dashboard EMERGENCY STOP is a software control. For real, physical
  safety, also keep a power switch for the motor supply within reach.

---

## 12. Dashboard Features (summary)

- Live animated SVG windmill - CSS rotation matches the real motor state
  (clockwise forward, counter-clockwise reverse, paused when stopped).
- Status card: RUNNING / STOPPED / REVERSING... / EMERGENCY STOP with
  direction and speed.
- Large FORWARD / STOP / REVERSE buttons with busy animations.
- Speed slider with real-time percentage and 25/50/75/100% presets.
- EMERGENCY STOP with a latched RESET / CLEAR flow.
- MANUAL / DEMO modes with START DEMO / STOP DEMO buttons.
- MOTOR INFORMATION card: direction, PWM/255, speed, motor state,
  control mode. It explicitly shows `RPM sensor: Not installed`
  (no fake RPM).
- SYSTEM card: ESP32, Wi-Fi SSID, IP, driver, motor, channel, pins.
- Command log (last 20 events, generated in-browser).
- Connection polling every 1.5 s with `ESP32 OFFLINE` handling.

---

## 13. Notes

- This project demonstrates: IoT, Wi-Fi AP, an embedded web server, PWM,
  H-bridge motor control, a responsive UI, real-time status, and safety
  controls - suitable for a college IoT laboratory.
- Built with only `<WiFi.h>` and `<WebServer.h>` plus the Arduino core.
  No unnecessary libraries and no external assets (system fonts, inline
  SVG/CSS, no CDNs).
