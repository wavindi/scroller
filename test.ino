
#include <BleMouse.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

// ==========================================
// ESP32-C3 SUPERMINI SCROLLER V3.6
// OLED + BLE + SERIAL + BUTTONS
// NEW: DOUBLE CLICK (r) + STATUS
// ==========================================

// ---------- OLED ----------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0,
  U8X8_PIN_NONE,
  6,  // SCL
  5   // SDA
);

// ---------- BLUETOOTH ----------
BleMouse bleMouse("scroller", "Custom IoT", 100);

// ---------- PINS ----------
const uint8_t LED_PIN = 8;
const uint8_t BTN_S = 3;
const uint8_t BTN_D = 4;
const uint8_t BTN_X = 7;
const uint8_t BTN_C = 10;

// Active-low LED
const uint8_t LED_ON = LOW;
const uint8_t LED_OFF = HIGH;

// ---------- TIMING ----------
const unsigned long DEBOUNCE_MS = 50;
const unsigned long BLINK_INTERVAL = 500;
const unsigned long SERIAL_TIMEOUT = 250;

// Double-click timing
const int CLICK_HOLD_MS = 50;
const int DOUBLE_CLICK_GAP_MS = 100;

// ---------- OLED POSITIONS ----------
// Tested and confirmed working
const int TEXT_Y1 = 43;
const int TEXT_Y2 = 56;

// ---------- SYSTEM STATE ----------
bool autoCalibrate = true;
bool wasConnected = false;

bool ledState = false;
unsigned long lastBlink = 0;

// ---------- BUTTON CONFIG ----------
struct Button {
  uint8_t pin;
  bool lastRaw;
  bool stableState;
  unsigned long lastChange;
  int distance;
  int speed;
  bool swipeUp;
  const char* name;
};

Button buttons[] = {
  {BTN_S, HIGH, HIGH, 0, 100, 6, true,  "S"},
  {BTN_D, HIGH, HIGH, 0, 150, 12, true, "D"},
  {BTN_X, HIGH, HIGH, 0, 100, 6, false, "X"},
  {BTN_C, HIGH, HIGH, 0, 150, 12, false, "C"}
};

const int BUTTON_COUNT =
  sizeof(buttons) / sizeof(buttons[0]);

// ---------- OLED STATE ----------
String displayLine1 = "Starting";
String displayLine2 = "Please wait";
String lastAction = "None";

// ---------- SERIAL INPUT ----------
char serialBuffer[64];
uint8_t serialIndex = 0;
unsigned long lastSerialTime = 0;
bool serialOverflow = false;

// ==========================================
// OLED - CENTERED TWO-LINE DISPLAY
// ==========================================
void updateScreen(
  const String& line1,
  const String& line2
) {
  displayLine1 = line1;
  displayLine2 = line2;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tf);

  int width = u8g2.getDisplayWidth();

  int x1 = (width -
    u8g2.getStrWidth(line1.c_str())) / 2;

  int x2 = (width -
    u8g2.getStrWidth(line2.c_str())) / 2;

  u8g2.drawStr(
    max(0, x1),
    TEXT_Y1,
    line1.c_str()
  );

  u8g2.drawStr(
    max(0, x2),
    TEXT_Y2,
    line2.c_str()
  );

  u8g2.sendBuffer();
}

// ==========================================
// BLUETOOTH LED
// ==========================================
void updateLED() {
  if (bleMouse.isConnected()) {
    ledState = false;
    digitalWrite(LED_PIN, LED_OFF);
    return;
  }

  if (millis() - lastBlink >= BLINK_INTERVAL) {
    lastBlink = millis();
    ledState = !ledState;

    digitalWrite(
      LED_PIN,
      ledState ? LED_ON : LED_OFF
    );
  }
}

// ==========================================
// SERIAL HELP
// ==========================================
void printHelp() {
  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32-C3 SCROLLER V3.6");
  Serial.println("================================");

  Serial.println("SWIPE:");
  Serial.println(" s -> UP 100, 6ms");
  Serial.println(" d -> UP 150, 12ms");
  Serial.println(" x -> DOWN 100, 6ms");
  Serial.println(" c -> DOWN 150, 12ms");

  Serial.println();
  Serial.println("DOUBLE CLICK:");
  Serial.println(" r -> Double click + calibration");

  Serial.println();
  Serial.println("CALIBRATION:");
  Serial.println(" cal -> Standard calibration");
  Serial.println(" e   -> Edge calibration");

  Serial.println();
  Serial.println("CUSTOM SWIPE:");
  Serial.println(" up 120 8");
  Serial.println(" down 180 10");

  Serial.println();
  Serial.println("MANUAL CURSOR:");
  Serial.println(" 100,200");
  Serial.println(" -100,0");

  Serial.println();
  Serial.println("SETTINGS:");
  Serial.println(" auto on");
  Serial.println(" auto off");
  Serial.println(" status");
  Serial.println(" help");

  Serial.println("================================");
}

// ==========================================
// CURSOR MOVEMENT
// ==========================================
bool moveCursor(int x, int y) {
  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] BLE disconnected");
    return false;
  }

  while (x != 0 || y != 0) {
    if (!bleMouse.isConnected()) {
      Serial.println("[ERROR] BLE connection lost");
      return false;
    }

    int moveX = constrain(x, -127, 127);
    int moveY = constrain(y, -127, 127);

    bleMouse.move(moveX, moveY);

    x -= moveX;
    y -= moveY;

    delay(5);
  }

  return true;
}

// ==========================================
// STANDARD CALIBRATION
// ==========================================
void calibrateStandard() {
  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] BLE disconnected");
    return;
  }

  lastAction = "Standard calibration";

  Serial.println("[CAL] Standard calibration");
  updateScreen("Calibrating", "Standard");

  if (!moveCursor(3000, 3000)) {
    updateScreen("Calibration", "Failed");
    return;
  }

  delay(100);

  if (!moveCursor(-500, -200)) {
    updateScreen("Calibration", "Failed");
    return;
  }

  Serial.println("[CAL] Standard complete");
  updateScreen("Calibration", "Complete");
}

// ==========================================
// EDGE CALIBRATION
// NO LEFT 1000 MOVEMENT
// ==========================================
bool calibrateEdge() {
  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] BLE disconnected");
    return false;
  }

  Serial.println("[CAL] Edge calibration");
  updateScreen("Calibrating", "Right edge");

  // Move to bottom-right
  if (!moveCursor(5000, 5000)) {
    updateScreen("Calibration", "Failed");
    return false;
  }

  delay(100);

  // Move UP 100
  if (!moveCursor(0, -100)) {
    updateScreen("Calibration", "Failed");
    return false;
  }

  // Move UP another 70
  if (!moveCursor(0, -70)) {
    updateScreen("Calibration", "Failed");
    return false;
  }

  // No LEFT movement

  Serial.println("[CAL] Edge complete");
  updateScreen("Calibration", "Complete");

  return true;
}

// ==========================================
// SWIPE ENGINE
// ==========================================
void simulateSwipe(
  int distance,
  int speedDelay,
  bool swipeUp,
  const String& source
) {
  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] Bluetooth not connected");
    updateScreen("Bluetooth", "Disconnected");
    return;
  }

  if (distance < 1 || distance > 2000 ||
      speedDelay < 1 || speedDelay > 100) {
    Serial.println("[ERROR] Invalid swipe settings");
    updateScreen("Invalid", "Swipe");
    return;
  }

  String direction = swipeUp ? "UP" : "DOWN";

  lastAction = direction + " " + String(distance);

  updateScreen(
    "Swipe " + direction,
    String(distance) + " units"
  );

  Serial.println();
  Serial.println("--------- SWIPE ---------");

  Serial.print("Source: ");
  Serial.println(source);

  Serial.print("Direction: ");
  Serial.println(direction);

  Serial.print("Distance: ");
  Serial.println(distance);

  Serial.print("Speed delay: ");
  Serial.print(speedDelay);
  Serial.println("ms");

  bleMouse.press(MOUSE_LEFT);
  delay(50);

  int remaining = distance;

  while (remaining > 0) {
    if (!bleMouse.isConnected()) {
      bleMouse.release(MOUSE_LEFT);

      Serial.println("[ERROR] BLE connection lost");
      updateScreen("Bluetooth", "Connection lost");
      return;
    }

    int step = min(remaining, 10);

    bleMouse.move(
      0,
      swipeUp ? -step : step
    );

    remaining -= step;
    delay(speedDelay);
  }

  delay(20);
  bleMouse.release(MOUSE_LEFT);

  Serial.println("[SWIPE] Complete");

  delay(100);

  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge()) return;
  }

  if (bleMouse.isConnected()) {
    updateScreen(lastAction, "Ready");
  }

  Serial.println("[READY] Next command");
  Serial.println("-------------------------");
}

// ==========================================
// DOUBLE CLICK / LIKE
// ==========================================
void simulateDoubleClick() {
  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] Bluetooth not connected");
    updateScreen("Bluetooth", "Disconnected");
    return;
  }

  lastAction = "Double click";

  Serial.println();
  Serial.println("------- DOUBLE CLICK -------");
  Serial.println("[ACTION] Double click / Like");

  updateScreen("Double click", "Like!");

  // First click
  bleMouse.press(MOUSE_LEFT);
  delay(CLICK_HOLD_MS);
  bleMouse.release(MOUSE_LEFT);

  // Gap between clicks
  delay(DOUBLE_CLICK_GAP_MS);

  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] BLE disconnected");
    updateScreen("Bluetooth", "Disconnected");
    return;
  }

  // Second click
  bleMouse.press(MOUSE_LEFT);
  delay(CLICK_HOLD_MS);
  bleMouse.release(MOUSE_LEFT);

  Serial.println("[CLICK] Double click complete");

  // Same pause as swipe
  delay(100);

  // Same calibration as swipe
  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge()) return;
  }

  if (bleMouse.isConnected()) {
    updateScreen("Double click", "Ready");
  }

  Serial.println("[READY] Next command");
  Serial.println("----------------------------");
}

// ==========================================
// PHYSICAL BUTTONS
// ==========================================
void handleButtons() {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    Button& b = buttons[i];

    unsigned long now = millis();
    bool raw = digitalRead(b.pin);

    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChange = now;
    }

    if ((now - b.lastChange) >= DEBOUNCE_MS &&
        raw != b.stableState) {
      b.stableState = raw;

      if (b.stableState == LOW &&
          bleMouse.isConnected()) {
        Serial.print("[BUTTON] ");
        Serial.println(b.name);

        simulateSwipe(
          b.distance,
          b.speed,
          b.swipeUp,
          "BTN " + String(b.name)
        );
      }
    }
  }
}

// ==========================================
// STATUS: SERIAL + OLED
// ==========================================
void showStatus() {
  bool connected = bleMouse.isConnected();

  Serial.println();
  Serial.println("========== STATUS ==========");

  Serial.print("Bluetooth: ");
  Serial.println(
    connected ? "CONNECTED" : "DISCONNECTED"
  );

  Serial.print("Auto calibration: ");
  Serial.println(
    autoCalibrate ? "ON" : "OFF"
  );

  Serial.print("Last action: ");
  Serial.println(lastAction);

  Serial.print("OLED: ");
  Serial.print(displayLine1);
  Serial.print(" / ");
  Serial.println(displayLine2);

  Serial.println("============================");

  // OLED: show Bluetooth and calibration status
  updateScreen(
    connected ? "BLE Connected" : "BLE Waiting",
    autoCalibrate ? "Auto Cal: ON" : "Auto Cal: OFF"
  );
}

// ==========================================
// SERIAL COMMAND PROCESSOR
// ==========================================
void processCommand(String input) {
  input.trim();
  input.toLowerCase();

  if (input.length() == 0) return;

  Serial.print("[SERIAL] Received: ");
  Serial.println(input);

  // Commands available without BLE
  if (input == "help") {
    printHelp();
    return;
  }

  if (input == "status") {
    showStatus();
    return;
  }

  if (input == "auto on") {
    autoCalibrate = true;

    Serial.println("[AUTO CAL] Enabled");
    updateScreen("Auto Cal", "Enabled");
    return;
  }

  if (input == "auto off") {
    autoCalibrate = false;

    Serial.println("[AUTO CAL] Disabled");
    updateScreen("Auto Cal", "Disabled");
    return;
  }

  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] Connect Bluetooth first");
    updateScreen("Bluetooth", "Waiting...");
    return;
  }

  // Double-click
  if (input == "r") {
    simulateDoubleClick();
  }

  // Calibration
  else if (input == "cal") {
    calibrateStandard();
  }
  else if (input == "e") {
    lastAction = "Edge calibration";
    calibrateEdge();
  }

  // UP
  else if (input == "s") {
    simulateSwipe(100, 6, true, "SERIAL S");
  }
  else if (input == "d") {
    simulateSwipe(150, 12, true, "SERIAL D");
  }

  // DOWN
  else if (input == "x") {
    simulateSwipe(100, 6, false, "SERIAL X");
  }
  else if (input == "c") {
    simulateSwipe(150, 12, false, "SERIAL C");
  }

  // Custom swipe
  else if (
    input.startsWith("up ") ||
    input.startsWith("down ")
  ) {
    bool swipeUp = input.startsWith("up ");

    int distance = 0;
    int speed = 0;
    int parsed = 0;

    if (swipeUp) {
      parsed = sscanf(
        input.c_str(),
        "up %d %d",
        &distance,
        &speed
      );
    } else {
      parsed = sscanf(
        input.c_str(),
        "down %d %d",
        &distance,
        &speed
      );
    }

    if (parsed == 2) {
      simulateSwipe(
        distance,
        speed,
        swipeUp,
        "SERIAL CUSTOM"
      );
    } else {
      Serial.println("[ERROR] Example: up 120 8");
    }
  }

  // Manual cursor movement
  else if (input.indexOf(',') >= 0) {
    int x = 0;
    int y = 0;

    int parsed = sscanf(
      input.c_str(),
      "%d,%d",
      &x,
      &y
    );

    if (parsed == 2) {
      lastAction = "Move cursor";

      Serial.print("[MOVE] X=");
      Serial.print(x);
      Serial.print(" Y=");
      Serial.println(y);

      updateScreen("Moving", "Cursor");

      if (moveCursor(x, y)) {
        updateScreen("Cursor", "Moved");
      } else {
        updateScreen("Cursor", "Failed");
      }
    } else {
      Serial.println("[ERROR] Invalid coordinates");
    }
  }

  else {
    Serial.println("[ERROR] Unknown command");
    updateScreen("Unknown", "Command");
  }
}

// ==========================================
// SERIAL INPUT
// ==========================================
void executeSerialBuffer() {
  if (serialIndex == 0) return;

  serialBuffer[serialIndex] = '\0';

  String input = String(serialBuffer);

  serialIndex = 0;

  processCommand(input);
}

void handleSerial() {
  while (Serial.available() > 0) {
    char ch = Serial.read();

    lastSerialTime = millis();

    if (ch == '\n' || ch == '\r') {
      if (serialOverflow) {
        serialOverflow = false;
        serialIndex = 0;
      } else {
        executeSerialBuffer();
      }
    }
    else if (!serialOverflow) {
      if (serialIndex < sizeof(serialBuffer) - 1) {
        serialBuffer[serialIndex++] = ch;
      } else {
        serialOverflow = true;
        serialIndex = 0;
        Serial.println("[ERROR] Command too long");
      }
    }
  }

  // Supports "No line ending"
  if (serialIndex > 0 &&
      millis() - lastSerialTime >= SERIAL_TIMEOUT) {
    executeSerialBuffer();
  }

  if (serialOverflow &&
      millis() - lastSerialTime >= SERIAL_TIMEOUT) {
    serialOverflow = false;
    serialIndex = 0;
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  unsigned long startWait = millis();

  while (!Serial &&
         millis() - startWait < 3000) {
    delay(10);
  }

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
  lastBlink = millis();

  // Buttons
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);

    bool state = digitalRead(buttons[i].pin);

    buttons[i].lastRaw = state;
    buttons[i].stableState = state;
    buttons[i].lastChange = millis();
  }

  // OLED
  u8g2.begin();
  updateScreen("Starting", "Scroller");

  // Bluetooth
  bleMouse.begin();

  Serial.println();
  Serial.println("ESP32-C3 SCROLLER V3.6 STARTED");

  printHelp();

  updateScreen("Bluetooth", "Waiting...");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  bool connected = bleMouse.isConnected();

  // LED: blink while disconnected, off when connected
  updateLED();

  // Bluetooth connection changes
  if (connected != wasConnected) {
    wasConnected = connected;

    if (connected) {
      digitalWrite(LED_PIN, LED_OFF);
      ledState = false;

      Serial.println("[BLE] Connected");
      updateScreen("Bluetooth", "Connected!");
    }
    else {
      Serial.println("[BLE] Disconnected");

      ledState = true;
      digitalWrite(LED_PIN, LED_ON);
      lastBlink = millis();

      updateScreen("Bluetooth", "Waiting...");
    }

    // Synchronize button states
    for (int i = 0; i < BUTTON_COUNT; i++) {
      bool state = digitalRead(buttons[i].pin);

      buttons[i].lastRaw = state;
      buttons[i].stableState = state;
      buttons[i].lastChange = millis();
    }
  }

  // Serial commands
  handleSerial();

  // Physical buttons
  handleButtons();

  delay(1);
}
