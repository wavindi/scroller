
#include <BleMouse.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>
#include <stdio.h>

// =============================================
// ESP32-C3 SCROLLER V3.8
// Short / Long Press + OLED Icons + BLE
// =============================================

// 1 = Serial input and debug logs ON
// 0 = Serial completely disabled
#define DEBUG_MODE 1

#if DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x) do {} while (0)
  #define DEBUG_PRINTLN(x) do {} while (0)
#endif

// OLED - your confirmed working pins
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE,
  6,  // SCL
  5   // SDA
);

BleMouse bleMouse("scroller", "Custom IoT", 100);

// =============================================
// PIN CONFIGURATION
// =============================================
const uint8_t LED_PIN = 8;

const uint8_t BTN_DOWN = 3;
const uint8_t BTN_UP   = 4;
const uint8_t BTN_LIKE = 10;
const uint8_t BTN_RESERVED = 1;

// =============================================
// SETTINGS
// =============================================
const int SHORT_DISTANCE = 100;
const int LONG_DISTANCE = 150;

const int SHORT_SPEED_MS = 6;
const int LONG_SPEED_MS = 12;

const unsigned long LONG_PRESS_MS = 500;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long ACTION_DISPLAY_MS = 2000;
const unsigned long BLINK_MS = 500;
const unsigned long SERIAL_TIMEOUT_MS = 250;

// OLED placement
const int ICON_X = 64;
const int ICON_Y = 39;
const int TEXT_Y = 56;

// =============================================
// STATE
// =============================================
bool autoCalibrate = true;
bool wasConnected = false;

bool ledState = false;
unsigned long lastBlink = 0;

String lastAction = "None";

enum ScreenType {
  SCREEN_SEARCHING,
  SCREEN_READY,
  SCREEN_LIKE,
  SCREEN_UP,
  SCREEN_DOWN,
  SCREEN_CALIBRATE,
  SCREEN_STATUS,
  SCREEN_MOVE,
  SCREEN_ERROR
};

ScreenType currentScreen = SCREEN_SEARCHING;
bool actionScreenActive = false;
unsigned long actionFinishedAt = 0;

// =============================================
// BUTTON STRUCTURE
// =============================================
struct Button {
  uint8_t pin;
  bool lastRaw;
  bool stableState;
  unsigned long lastChange;
  unsigned long pressedAt;
  bool longTriggered;
};

Button downButton = {
  BTN_DOWN, HIGH, HIGH, 0, 0, false
};

Button upButton = {
  BTN_UP, HIGH, HIGH, 0, 0, false
};

Button likeButton = {
  BTN_LIKE, HIGH, HIGH, 0, 0, false
};

Button reservedButton = {
  BTN_RESERVED, HIGH, HIGH, 0, 0, false
};

// Serial buffer
char serialBuffer[64];
uint8_t serialIndex = 0;
unsigned long lastSerialTime = 0;
bool serialOverflow = false;

// =============================================
// OLED ICONS
// =============================================
void drawBluetoothIcon(int x, int y) {
  u8g2.drawLine(x, y - 12, x, y + 12);
  u8g2.drawLine(x, y - 12, x + 7, y - 6);
  u8g2.drawLine(x + 7, y - 6, x - 7, y + 6);
  u8g2.drawLine(x - 7, y - 6, x + 7, y + 6);
  u8g2.drawLine(x + 7, y + 6, x, y + 12);
}

void drawSmileIcon(int x, int y) {
  u8g2.drawCircle(x, y, 11);

  u8g2.drawDisc(x - 4, y - 3, 1);
  u8g2.drawDisc(x + 4, y - 3, 1);

  u8g2.drawLine(x - 5, y + 3, x - 2, y + 6);
  u8g2.drawLine(x - 2, y + 6, x + 2, y + 6);
  u8g2.drawLine(x + 2, y + 6, x + 5, y + 3);
}

void drawHeartIcon(int x, int y) {
  u8g2.drawDisc(x - 5, y - 3, 5);
  u8g2.drawDisc(x + 5, y - 3, 5);

  u8g2.drawTriangle(
    x - 10, y - 1,
    x + 10, y - 1,
    x, y + 9
  );
}

void drawUpIcon(int x, int y) {
  u8g2.drawLine(x, y - 11, x, y + 10);
  u8g2.drawLine(x, y - 11, x - 8, y - 3);
  u8g2.drawLine(x, y - 11, x + 8, y - 3);
}

void drawDownIcon(int x, int y) {
  u8g2.drawLine(x, y - 10, x, y + 10);
  u8g2.drawLine(x, y + 10, x - 8, y + 2);
  u8g2.drawLine(x, y + 10, x + 8, y + 2);
}

void drawGearIcon(int x, int y) {
  u8g2.drawCircle(x, y, 8);
  u8g2.drawCircle(x, y, 3);

  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159265f / 4.0f;

    int x1 = x + cosf(a) * 9;
    int y1 = y + sinf(a) * 9;
    int x2 = x + cosf(a) * 12;
    int y2 = y + sinf(a) * 12;

    u8g2.drawLine(x1, y1, x2, y2);
  }
}

void drawCheckIcon(int x, int y) {
  u8g2.drawCircle(x, y, 11);
  u8g2.drawLine(x - 6, y, x - 1, y + 5);
  u8g2.drawLine(x - 1, y + 5, x + 7, y - 6);
}

void drawMoveIcon(int x, int y) {
  u8g2.drawLine(x - 10, y, x + 10, y);
  u8g2.drawLine(x, y - 10, x, y + 10);
}

void drawErrorIcon(int x, int y) {
  u8g2.drawCircle(x, y, 11);
  u8g2.drawLine(x - 5, y - 5, x + 5, y + 5);
  u8g2.drawLine(x + 5, y - 5, x - 5, y + 5);
}

// =============================================
// OLED SCREEN
// =============================================
void drawScreen(
  ScreenType screen,
  const char* customLabel = nullptr
) {
  currentScreen = screen;

  const char* label = "";

  u8g2.clearBuffer();

  switch (screen) {
    case SCREEN_SEARCHING:
      drawBluetoothIcon(ICON_X, ICON_Y);
      label = "SEARCHING";
      break;

    case SCREEN_READY:
      drawSmileIcon(ICON_X, ICON_Y);
      label = "READY";
      break;

    case SCREEN_LIKE:
      drawHeartIcon(ICON_X, ICON_Y);
      label = "LIKE!";
      break;

    case SCREEN_UP:
      drawUpIcon(ICON_X, ICON_Y);
      label = "UP";
      break;

    case SCREEN_DOWN:
      drawDownIcon(ICON_X, ICON_Y);
      label = "DOWN";
      break;

    case SCREEN_CALIBRATE:
      drawGearIcon(ICON_X, ICON_Y);
      label = "CALIBRATE";
      break;

    case SCREEN_STATUS:
      drawCheckIcon(ICON_X, ICON_Y);
      label = "STATUS";
      break;

    case SCREEN_MOVE:
      drawMoveIcon(ICON_X, ICON_Y);
      label = "CURSOR";
      break;

    case SCREEN_ERROR:
      drawErrorIcon(ICON_X, ICON_Y);
      label = "ERROR";
      break;
  }

  if (customLabel != nullptr) {
    label = customLabel;
  }

  u8g2.setFont(u8g2_font_5x8_tf);

  int textWidth = u8g2.getStrWidth(label);
  int x = max(
    0,
    (u8g2.getDisplayWidth() - textWidth) / 2
  );

  u8g2.drawStr(x, TEXT_Y, label);
  u8g2.sendBuffer();
}

void showAction(
  ScreenType screen,
  const char* label = nullptr
) {
  drawScreen(screen, label);

  actionScreenActive = true;
  actionFinishedAt = millis();
}

void updateDisplay() {
  if (!bleMouse.isConnected()) {
    actionScreenActive = false;

    if (currentScreen != SCREEN_SEARCHING) {
      drawScreen(SCREEN_SEARCHING);
    }

    return;
  }

  if (actionScreenActive &&
      millis() - actionFinishedAt >= ACTION_DISPLAY_MS) {

    actionScreenActive = false;
    drawScreen(SCREEN_READY);
  }
}

// =============================================
// LED
// =============================================
void updateLED() {
  if (bleMouse.isConnected()) {
    ledState = false;
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  if (millis() - lastBlink >= BLINK_MS) {
    lastBlink = millis();
    ledState = !ledState;

    // Active-low LED
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
  }
}

// =============================================
// BLE CURSOR MOVEMENT
// =============================================
bool moveCursor(int x, int y) {
  if (!bleMouse.isConnected()) {
    DEBUG_PRINTLN("[ERROR] BLE disconnected");
    return false;
  }

  while (x != 0 || y != 0) {
    if (!bleMouse.isConnected()) {
      DEBUG_PRINTLN("[ERROR] BLE connection lost");
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

// =============================================
// CALIBRATION
// =============================================
bool calibrateEdge(bool showIcon = true) {
  if (!bleMouse.isConnected()) return false;

  if (showIcon) {
    drawScreen(SCREEN_CALIBRATE);
  }

  DEBUG_PRINTLN("[CAL] Edge calibration");

  if (!moveCursor(5000, 5000)) return false;

  delay(100);

  if (!moveCursor(0, -100)) return false;
  if (!moveCursor(0, -70)) return false;

  DEBUG_PRINTLN("[CAL] Complete");
  return true;
}

bool calibrateStandard() {
  if (!bleMouse.isConnected()) return false;

  drawScreen(SCREEN_CALIBRATE);

  DEBUG_PRINTLN("[CAL] Standard calibration");

  if (!moveCursor(3000, 3000)) return false;

  delay(100);

  if (!moveCursor(-500, -200)) return false;

  DEBUG_PRINTLN("[CAL] Complete");
  return true;
}

// =============================================
// SWIPE
// =============================================
void simulateSwipe(
  int distance,
  int speedDelay,
  bool swipeUp,
  const String& source,
  const char* oledLabel = nullptr
) {
  if (!bleMouse.isConnected()) {
    DEBUG_PRINTLN("[ERROR] Not connected");
    return;
  }

  if (distance < 1 || distance > 2000 ||
      speedDelay < 1 || speedDelay > 100) {
    DEBUG_PRINTLN("[ERROR] Invalid swipe");
    showAction(SCREEN_ERROR);
    return;
  }

  ScreenType icon =
    swipeUp ? SCREEN_UP : SCREEN_DOWN;

  const char* label = oledLabel;

  if (label == nullptr) {
    label = swipeUp ? "SWIPE UP" : "SWIPE DOWN";
  }

  lastAction = String(swipeUp ? "UP " : "DOWN ")
               + distance
               + " (" + source + ")";

  drawScreen(icon, label);

  DEBUG_PRINT("[SWIPE] ");
  DEBUG_PRINTLN(lastAction);

  bleMouse.press(MOUSE_LEFT);
  delay(50);

  int remaining = distance;

  while (remaining > 0) {
    if (!bleMouse.isConnected()) {
      bleMouse.release(MOUSE_LEFT);
      DEBUG_PRINTLN("[ERROR] Connection lost");
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

  delay(100);

  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge(false)) return;
  }

  if (bleMouse.isConnected()) {
    showAction(icon, label);
  }

  DEBUG_PRINTLN("[SWIPE] Complete");
}

// =============================================
// DOUBLE CLICK / LIKE
// =============================================
void simulateDoubleClick() {
  if (!bleMouse.isConnected()) return;

  lastAction = "Double click / Like";

  drawScreen(SCREEN_LIKE);

  DEBUG_PRINTLN("[LIKE] Double click");

  bleMouse.press(MOUSE_LEFT);
  delay(50);
  bleMouse.release(MOUSE_LEFT);

  delay(100);

  if (!bleMouse.isConnected()) return;

  bleMouse.press(MOUSE_LEFT);
  delay(50);
  bleMouse.release(MOUSE_LEFT);

  delay(100);

  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge(false)) return;
  }

  if (bleMouse.isConnected()) {
    showAction(SCREEN_LIKE);
  }

  DEBUG_PRINTLN("[LIKE] Complete");
}

// =============================================
// STATUS - OLED + OPTIONAL DEBUG LOG
// =============================================
void showStatus() {
  bool connected = bleMouse.isConnected();

  DEBUG_PRINTLN("========== STATUS ==========");
  DEBUG_PRINT("BLE: ");
  DEBUG_PRINTLN(connected ? "CONNECTED" : "DISCONNECTED");

  DEBUG_PRINT("Auto calibration: ");
  DEBUG_PRINTLN(autoCalibrate ? "ON" : "OFF");

  DEBUG_PRINT("Last action: ");
  DEBUG_PRINTLN(lastAction);

  DEBUG_PRINTLN("============================");

  if (connected) {
    showAction(
      SCREEN_STATUS,
      autoCalibrate ? "AUTO CAL ON" : "AUTO CAL OFF"
    );
  } else {
    drawScreen(SCREEN_SEARCHING);
  }
}

// =============================================
// BUTTON HANDLING
// =============================================

// Call once during setup or connection changes
void syncButton(Button& b) {
  bool state = digitalRead(b.pin);

  b.lastRaw = state;
  b.stableState = state;
  b.lastChange = millis();
  b.pressedAt = millis();
  b.longTriggered = false;
}

void handleSwipeButton(Button& b, bool swipeUp) {
  unsigned long now = millis();
  bool raw = digitalRead(b.pin);

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
  }

  // Debounced transition
  if ((now - b.lastChange) >= DEBOUNCE_MS &&
      raw != b.stableState) {

    b.stableState = raw;

    if (raw == LOW) {
      // Button pressed
      b.pressedAt = now;
      b.longTriggered = false;
    } else {
      // Button released
      if (!b.longTriggered &&
          bleMouse.isConnected()) {

        simulateSwipe(
          SHORT_DISTANCE,
          SHORT_SPEED_MS,
          swipeUp,
          swipeUp ? "BTN UP SHORT" : "BTN DOWN SHORT",
          swipeUp ? "UP SHORT" : "DOWN SHORT"
        );
      }
    }
  }

  // Long press fires once while held
  if (b.stableState == LOW &&
      !b.longTriggered &&
      bleMouse.isConnected() &&
      (millis() - b.pressedAt >= LONG_PRESS_MS)) {

    b.longTriggered = true;

    simulateSwipe(
      LONG_DISTANCE,
      LONG_SPEED_MS,
      swipeUp,
      swipeUp ? "BTN UP LONG" : "BTN DOWN LONG",
      swipeUp ? "UP LONG" : "DOWN LONG"
    );
  }
}

void handleLikeButton(Button& b) {
  unsigned long now = millis();
  bool raw = digitalRead(b.pin);

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
  }

  if ((now - b.lastChange) >= DEBOUNCE_MS &&
      raw != b.stableState) {

    b.stableState = raw;

    if (raw == LOW && bleMouse.isConnected()) {
      simulateDoubleClick();
    }
  }
}

void handleButtons() {
  handleSwipeButton(downButton, false);
  handleSwipeButton(upButton, true);
  handleLikeButton(likeButton);

  // GPIO1 reserved:
  // No action intentionally assigned.
}

// =============================================
// DEBUG SERIAL COMMANDS
// Compiled only when DEBUG_MODE = 1
// =============================================
#if DEBUG_MODE

void printHelp() {
  Serial.println();
  Serial.println("====== SCROLLER V3.8 ======");
  Serial.println("s       -> UP SHORT");
  Serial.println("d       -> UP LONG");
  Serial.println("x       -> DOWN SHORT");
  Serial.println("c       -> DOWN LONG");
  Serial.println("r       -> LIKE");
  Serial.println("status  -> OLED STATUS");
  Serial.println("e       -> EDGE CAL");
  Serial.println("cal     -> STANDARD CAL");
  Serial.println("auto on -> ENABLE AUTO CAL");
  Serial.println("auto off-> DISABLE AUTO CAL");
  Serial.println("up 120 8");
  Serial.println("down 180 10");
  Serial.println("100,200 -> CURSOR MOVE");
  Serial.println("help    -> COMMANDS");
  Serial.println("===========================");
}

void processCommand(String input) {
  input.trim();
  input.toLowerCase();

  if (input.length() == 0) return;

  Serial.print("[SERIAL] ");
  Serial.println(input);

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

    Serial.println("[AUTO CAL] ON");

    if (bleMouse.isConnected()) {
      showAction(SCREEN_STATUS, "AUTO CAL ON");
    }
    return;
  }

  if (input == "auto off") {
    autoCalibrate = false;

    Serial.println("[AUTO CAL] OFF");

    if (bleMouse.isConnected()) {
      showAction(SCREEN_STATUS, "AUTO CAL OFF");
    }
    return;
  }

  if (!bleMouse.isConnected()) {
    Serial.println("[ERROR] BLE not connected");
    drawScreen(SCREEN_SEARCHING);
    return;
  }

  if (input == "s") {
    simulateSwipe(100, 6, true,
                  "SERIAL S", "UP SHORT");
  }
  else if (input == "d") {
    simulateSwipe(150, 12, true,
                  "SERIAL D", "UP LONG");
  }
  else if (input == "x") {
    simulateSwipe(100, 6, false,
                  "SERIAL X", "DOWN SHORT");
  }
  else if (input == "c") {
    simulateSwipe(150, 12, false,
                  "SERIAL C", "DOWN LONG");
  }
  else if (input == "r") {
    simulateDoubleClick();
  }
  else if (input == "e") {
    lastAction = "Edge calibration";

    if (calibrateEdge()) {
      showAction(SCREEN_CALIBRATE);
    }
  }
  else if (input == "cal") {
    lastAction = "Standard calibration";

    if (calibrateStandard()) {
      showAction(SCREEN_CALIBRATE);
    }
  }
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
      showAction(SCREEN_ERROR);
    }
  }
  else if (input.indexOf(',') >= 0) {
    int x = 0;
    int y = 0;

    if (sscanf(input.c_str(), "%d,%d", &x, &y) == 2) {
      lastAction = "Move cursor";

      drawScreen(SCREEN_MOVE);

      if (moveCursor(x, y)) {
        showAction(SCREEN_MOVE);
      }
    } else {
      Serial.println("[ERROR] Invalid coordinates");
      showAction(SCREEN_ERROR);
    }
  }
  else {
    Serial.println("[ERROR] Unknown command");
    showAction(SCREEN_ERROR);
  }
}

void executeSerialBuffer() {
  if (serialIndex == 0) return;

  serialBuffer[serialIndex] = '\0';

  String input(serialBuffer);
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

  // Also works with "No line ending"
  if (serialIndex > 0 &&
      millis() - lastSerialTime >= SERIAL_TIMEOUT_MS) {
    executeSerialBuffer();
  }

  if (serialOverflow &&
      millis() - lastSerialTime >= SERIAL_TIMEOUT_MS) {
    serialOverflow = false;
    serialIndex = 0;
  }
}

#endif

// =============================================
// SETUP
// =============================================
void setup() {
#if DEBUG_MODE
  Serial.begin(115200);

  unsigned long startWait = millis();

  while (!Serial &&
         millis() - startWait < 3000) {
    delay(10);
  }
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_LIKE, INPUT_PULLUP);
  pinMode(BTN_RESERVED, INPUT_PULLUP);

  syncButton(downButton);
  syncButton(upButton);
  syncButton(likeButton);
  syncButton(reservedButton);

  u8g2.begin();
  drawScreen(SCREEN_SEARCHING);

  bleMouse.begin();

#if DEBUG_MODE
  Serial.println();
  Serial.println("ESP32-C3 SCROLLER V3.8");
  Serial.println("DEBUG MODE: ON");
  printHelp();
#endif
}

// =============================================
// LOOP
// =============================================
void loop() {
  bool connected = bleMouse.isConnected();

  updateLED();

  if (connected != wasConnected) {
    wasConnected = connected;

    if (connected) {
      DEBUG_PRINTLN("[BLE] Connected");

      actionScreenActive = false;
      drawScreen(SCREEN_READY);
    } else {
      DEBUG_PRINTLN("[BLE] Disconnected");

      actionScreenActive = false;
      drawScreen(SCREEN_SEARCHING);
    }

    syncButton(downButton);
    syncButton(upButton);
    syncButton(likeButton);
    syncButton(reservedButton);
  }

#if DEBUG_MODE
  handleSerial();
#endif

  handleButtons();
  updateDisplay();

  delay(1);
}
