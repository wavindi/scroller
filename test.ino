
#include <BleMouse.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>
#include <stdio.h>

// =============================================
// ESP32-C3 SCROLLER V4.1
// Fast ESCAPE Gesture + 4 Buttons + OLED
// =============================================

// 1 = Enable Serial Monitor
// 0 = Disable Serial Monitor
#define DEBUG_MODE 1

#if DEBUG_MODE
  #define DBG(x) Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x) do {} while (0)
  #define DBGLN(x) do {} while (0)
#endif

// OLED - SCL GPIO6, SDA GPIO5
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, 6, 5
);

BleMouse bleMouse("scroller", "Custom IoT", 100);

// =============================================
// GPIO CONFIGURATION
// =============================================
const uint8_t LED_PIN = 8;

const uint8_t PIN_UP = 3;
const uint8_t PIN_DOWN = 4;
const uint8_t PIN_ESCAPE = 10;
const uint8_t PIN_LIKE = 1;

// =============================================
// TIMING
// =============================================
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 500;
const unsigned long COMBO_HOLD_MS = 300;
const unsigned long COMBO_WINDOW_MS = 180;
const unsigned long DISPLAY_MS = 2000;
const unsigned long LED_BLINK_MS = 500;
const unsigned long SERIAL_IDLE_MS = 250;

// Swipe settings
const int SHORT_DISTANCE = 100;
const int LONG_DISTANCE = 150;
const int SHORT_SPEED_MS = 6;
const int LONG_SPEED_MS = 12;

// Fast ESCAPE gesture
const int ESCAPE_PREMOVE_DOWN = 200;
const int ESCAPE_DRAG_UP = 150;
const int ESCAPE_SPEED_MS = 4;

// OLED coordinates
const int ICON_X = 64;
const int ICON_Y = 39;
const int TEXT_Y = 56;

// =============================================
// STATE
// =============================================
bool autoCalibrate = true;
bool wasConnected = false;
bool ledOn = false;

unsigned long lastLedBlink = 0;

String lastAction = "None";

enum ScreenType {
  SCREEN_SEARCHING,
  SCREEN_READY,
  SCREEN_UP,
  SCREEN_DOWN,
  SCREEN_LIKE,
  SCREEN_ESCAPE,
  SCREEN_CAL,
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
  bool raw;
  bool stable;
  unsigned long changedAt;
  unsigned long pressedAt;
  bool longFired;
  bool pendingShort;
};

Button upBtn = {
  PIN_UP, HIGH, HIGH, 0, 0, false, false
};

Button downBtn = {
  PIN_DOWN, HIGH, HIGH, 0, 0, false, false
};

Button escapeBtn = {
  PIN_ESCAPE, HIGH, HIGH, 0, 0, false, false
};

Button likeBtn = {
  PIN_LIKE, HIGH, HIGH, 0, 0, false, false
};

Button* buttons[] = {
  &upBtn,
  &downBtn,
  &escapeBtn,
  &likeBtn
};

const int BUTTON_COUNT = 4;

// Combination state
bool comboCandidate = false;
bool comboLatched = false;
bool comboWindowOpen = false;
bool suppressUntilRelease = false;

unsigned long allPressedAt = 0;
unsigned long firstPressedAt = 0;

// Serial buffer
#if DEBUG_MODE
char serialBuffer[64];
uint8_t serialLength = 0;
unsigned long lastSerialByteAt = 0;
bool serialOverflow = false;
#endif

// =============================================
// OLED ICONS
// =============================================

void iconBluetooth(int x, int y) {
  u8g2.drawLine(x, y - 12, x, y + 12);
  u8g2.drawLine(x, y - 12, x + 7, y - 6);
  u8g2.drawLine(x + 7, y - 6, x - 7, y + 6);
  u8g2.drawLine(x - 7, y - 6, x + 7, y + 6);
  u8g2.drawLine(x + 7, y + 6, x, y + 12);
}

void iconSmile(int x, int y) {
  u8g2.drawCircle(x, y, 11);
  u8g2.drawDisc(x - 4, y - 3, 1);
  u8g2.drawDisc(x + 4, y - 3, 1);

  u8g2.drawLine(x - 5, y + 3, x - 2, y + 6);
  u8g2.drawLine(x - 2, y + 6, x + 2, y + 6);
  u8g2.drawLine(x + 2, y + 6, x + 5, y + 3);
}

void iconHeart(int x, int y) {
  u8g2.drawDisc(x - 5, y - 3, 5);
  u8g2.drawDisc(x + 5, y - 3, 5);

  u8g2.drawTriangle(
    x - 10, y - 1,
    x + 10, y - 1,
    x, y + 9
  );
}

void iconUp(int x, int y) {
  u8g2.drawLine(x, y - 11, x, y + 10);
  u8g2.drawLine(x, y - 11, x - 8, y - 3);
  u8g2.drawLine(x, y - 11, x + 8, y - 3);
}

void iconDown(int x, int y) {
  u8g2.drawLine(x, y - 10, x, y + 10);
  u8g2.drawLine(x, y + 10, x - 8, y + 2);
  u8g2.drawLine(x, y + 10, x + 8, y + 2);
}

// ESCAPE ICON: running person + exit arrow
void iconEscape(int x, int y) {
  // Head
  u8g2.drawDisc(x - 4, y - 9, 3);

  // Body leaning forward
  u8g2.drawLine(x - 4, y - 5, x + 2, y + 2);

  // Arms
  u8g2.drawLine(x - 2, y - 3, x + 6, y - 6);
  u8g2.drawLine(x - 2, y - 2, x - 10, y + 1);

  // Running legs
  u8g2.drawLine(x + 2, y + 2, x + 9, y + 8);
  u8g2.drawLine(x + 2, y + 2, x - 7, y + 9);

  // Exit arrow
  u8g2.drawLine(x + 11, y - 8, x + 18, y - 8);
  u8g2.drawLine(x + 18, y - 8, x + 14, y - 12);
  u8g2.drawLine(x + 18, y - 8, x + 14, y - 4);
}

void iconGear(int x, int y) {
  u8g2.drawCircle(x, y, 8);
  u8g2.drawCircle(x, y, 3);

  for (int i = 0; i < 8; i++) {
    float angle = i * 3.14159265f / 4.0f;

    int x1 = x + cosf(angle) * 9;
    int y1 = y + sinf(angle) * 9;
    int x2 = x + cosf(angle) * 12;
    int y2 = y + sinf(angle) * 12;

    u8g2.drawLine(x1, y1, x2, y2);
  }
}

void iconCheck(int x, int y) {
  u8g2.drawCircle(x, y, 11);
  u8g2.drawLine(x - 6, y, x - 1, y + 5);
  u8g2.drawLine(x - 1, y + 5, x + 7, y - 6);
}

void iconMove(int x, int y) {
  u8g2.drawLine(x - 10, y, x + 10, y);
  u8g2.drawLine(x, y - 10, x, y + 10);
}

void iconError(int x, int y) {
  u8g2.drawCircle(x, y, 11);
  u8g2.drawLine(x - 5, y - 5, x + 5, y + 5);
  u8g2.drawLine(x + 5, y - 5, x - 5, y + 5);
}

// =============================================
// OLED DISPLAY
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
      iconBluetooth(ICON_X, ICON_Y);
      label = "SEARCHING";
      break;

    case SCREEN_READY:
      iconSmile(ICON_X, ICON_Y);
      label = "READY";
      break;

    case SCREEN_UP:
      iconUp(ICON_X, ICON_Y);
      label = "SWIPE UP";
      break;

    case SCREEN_DOWN:
      iconDown(ICON_X, ICON_Y);
      label = "SWIPE DOWN";
      break;

    case SCREEN_LIKE:
      iconHeart(ICON_X, ICON_Y);
      label = "LIKE!";
      break;

    case SCREEN_ESCAPE:
      iconEscape(ICON_X, ICON_Y);
      label = "ESCAPE";
      break;

    case SCREEN_CAL:
      iconGear(ICON_X, ICON_Y);
      label = "CALIBRATE";
      break;

    case SCREEN_STATUS:
      iconCheck(ICON_X, ICON_Y);
      label = "STATUS";
      break;

    case SCREEN_MOVE:
      iconMove(ICON_X, ICON_Y);
      label = "CURSOR";
      break;

    case SCREEN_ERROR:
      iconError(ICON_X, ICON_Y);
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
      millis() - actionFinishedAt >= DISPLAY_MS) {

    actionScreenActive = false;
    drawScreen(SCREEN_READY);
  }
}

// =============================================
// LED
// =============================================

void updateLED() {
  if (bleMouse.isConnected()) {
    ledOn = false;
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  if (millis() - lastLedBlink >= LED_BLINK_MS) {
    lastLedBlink = millis();
    ledOn = !ledOn;

    // Active-low LED
    digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
  }
}

// =============================================
// BLE CURSOR
// =============================================

bool moveCursor(int x, int y) {
  if (!bleMouse.isConnected()) return false;

  while (x != 0 || y != 0) {
    if (!bleMouse.isConnected()) return false;

    int dx = constrain(x, -127, 127);
    int dy = constrain(y, -127, 127);

    bleMouse.move(dx, dy);

    x -= dx;
    y -= dy;

    delay(5);
  }

  return true;
}

// =============================================
// CALIBRATION
// =============================================

bool calibrateEdge(bool displayIcon = true) {
  if (!bleMouse.isConnected()) return false;

  if (displayIcon) {
    drawScreen(SCREEN_CAL);
  }

  DBGLN("[CAL] Edge calibration");

  if (!moveCursor(5000, 5000)) return false;

  delay(100);

  if (!moveCursor(0, -100)) return false;
  if (!moveCursor(0, -70)) return false;

  DBGLN("[CAL] Complete");

  return true;
}

bool calibrateStandard() {
  if (!bleMouse.isConnected()) return false;

  drawScreen(SCREEN_CAL);

  if (!moveCursor(3000, 3000)) return false;
  delay(100);
  if (!moveCursor(-500, -200)) return false;

  return true;
}

// =============================================
// NORMAL SWIPES
// =============================================

void simulateSwipe(
  int distance,
  int speedDelay,
  bool swipeUp,
  const String& source,
  const char* label = nullptr
) {
  if (!bleMouse.isConnected()) return;

  if (distance < 1 || distance > 2000 ||
      speedDelay < 1 || speedDelay > 100) {

    showAction(SCREEN_ERROR);
    return;
  }

  ScreenType screen =
    swipeUp ? SCREEN_UP : SCREEN_DOWN;

  if (label == nullptr) {
    label = swipeUp ? "SWIPE UP" : "SWIPE DOWN";
  }

  lastAction =
    String(swipeUp ? "UP " : "DOWN ") +
    String(distance) +
    " (" + source + ")";

  drawScreen(screen, label);

  DBG("[SWIPE] ");
  DBGLN(lastAction);

  bleMouse.press(MOUSE_LEFT);
  delay(50);

  int remaining = distance;

  while (remaining > 0) {
    if (!bleMouse.isConnected()) {
      bleMouse.release(MOUSE_LEFT);
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
    showAction(screen, label);
  }
}

// =============================================
// FAST ESCAPE / P GESTURE
// =============================================

void performEscapeGesture() {
  if (!bleMouse.isConnected()) return;

  lastAction = "Fast ESCAPE gesture";

  drawScreen(SCREEN_ESCAPE);

  DBGLN("[ESCAPE] Starting");

  // 1. Move DOWN 200 (no mouse button held)
  if (!moveCursor(0, ESCAPE_PREMOVE_DOWN)) return;

  delay(10);

  if (!bleMouse.isConnected()) return;

  // 2. Press left mouse button
  bleMouse.press(MOUSE_LEFT);
  delay(20);

  // 3. Drag UP 150 quickly
  for (int remaining = ESCAPE_DRAG_UP;
       remaining > 0;) {

    if (!bleMouse.isConnected()) {
      bleMouse.release(MOUSE_LEFT);
      return;
    }

    int step = min(remaining, 10);

    bleMouse.move(0, -step);
    remaining -= step;

    delay(ESCAPE_SPEED_MS);
  }

  // 4. Release
  delay(5);
  bleMouse.release(MOUSE_LEFT);
  delay(20);

  // 5. Auto calibration
  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge(false)) return;
  }

  if (bleMouse.isConnected()) {
    showAction(SCREEN_ESCAPE);
  }

  DBGLN("[ESCAPE] Complete");
}

// =============================================
// DOUBLE CLICK / LIKE
// =============================================

void simulateDoubleClick() {
  if (!bleMouse.isConnected()) return;

  lastAction = "Double click / Like";

  drawScreen(SCREEN_LIKE);

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
}

// =============================================
// STATUS
// =============================================

void showStatus() {
  bool connected = bleMouse.isConnected();

  DBG("[STATUS] BLE: ");
  DBGLN(connected ? "CONNECTED" : "DISCONNECTED");

  DBG("[STATUS] Auto calibration: ");
  DBGLN(autoCalibrate ? "ON" : "OFF");

  DBG("[STATUS] Last action: ");
  DBGLN(lastAction);

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
// BUTTONS
// =============================================

void syncButton(Button& b) {
  bool state = digitalRead(b.pin);

  b.raw = state;
  b.stable = state;
  b.changedAt = millis();
  b.pressedAt = millis();
  b.longFired = false;
  b.pendingShort = false;
}

void updateButton(Button& b) {
  unsigned long now = millis();
  bool reading = digitalRead(b.pin);

  if (reading != b.raw) {
    b.raw = reading;
    b.changedAt = now;
  }

  if (reading != b.stable &&
      now - b.changedAt >= DEBOUNCE_MS) {

    b.stable = reading;

    if (reading == LOW) {
      b.pressedAt = now;
      b.longFired = false;
      b.pendingShort = false;
    } else {
      if (!b.longFired) {
        b.pendingShort = true;
      }
    }
  }
}

bool allButtonsPressed() {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (buttons[i]->stable != LOW) return false;
  }

  return true;
}

bool anyButtonPressed() {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (buttons[i]->stable == LOW) return true;
  }

  return false;
}

void clearPendingActions() {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    buttons[i]->pendingShort = false;
    buttons[i]->longFired = true;
  }
}

void handleButtons() {
  unsigned long now = millis();

  for (int i = 0; i < BUTTON_COUNT; i++) {
    updateButton(*buttons[i]);
  }

  if (!bleMouse.isConnected()) return;

  bool anyPressed = anyButtonPressed();
  bool allPressed = allButtonsPressed();

  // Ignore actions after combo until all released
  if (suppressUntilRelease) {
    clearPendingActions();

    if (!anyPressed) {
      suppressUntilRelease = false;
      comboLatched = false;
      comboWindowOpen = false;
    }

    return;
  }

  if (anyPressed && !comboWindowOpen) {
    comboWindowOpen = true;
    firstPressedAt = now;
  }

  if (!anyPressed &&
      !upBtn.pendingShort &&
      !downBtn.pendingShort &&
      !escapeBtn.pendingShort &&
      !likeBtn.pendingShort) {

    comboWindowOpen = false;
  }

  // All four buttons -> edge calibration
  if (allPressed) {
    if (!comboCandidate) {
      comboCandidate = true;
      allPressedAt = now;
    }

    if (!comboLatched &&
        now - allPressedAt >= COMBO_HOLD_MS) {

      comboLatched = true;
      suppressUntilRelease = true;

      clearPendingActions();

      lastAction = "Four-button calibration";

      DBGLN("[COMBO] Edge calibration");

      if (calibrateEdge()) {
        showAction(SCREEN_CAL);
      }

      return;
    }
  } else {
    comboCandidate = false;
  }

  if (comboWindowOpen &&
      now - firstPressedAt < COMBO_WINDOW_MS) {
    return;
  }

  if (allPressed || comboCandidate) return;

  // GPIO3 long -> UP 150
  if (upBtn.stable == LOW &&
      !upBtn.longFired &&
      now - upBtn.pressedAt >= LONG_PRESS_MS) {

    upBtn.longFired = true;
    upBtn.pendingShort = false;

    simulateSwipe(
      LONG_DISTANCE,
      LONG_SPEED_MS,
      true,
      "GPIO3 LONG",
      "UP LONG"
    );

    return;
  }

  // GPIO4 long -> DOWN 150
  if (downBtn.stable == LOW &&
      !downBtn.longFired &&
      now - downBtn.pressedAt >= LONG_PRESS_MS) {

    downBtn.longFired = true;
    downBtn.pendingShort = false;

    simulateSwipe(
      LONG_DISTANCE,
      LONG_SPEED_MS,
      false,
      "GPIO4 LONG",
      "DOWN LONG"
    );

    return;
  }

  // GPIO3 short -> UP 100
  if (upBtn.pendingShort) {
    upBtn.pendingShort = false;

    simulateSwipe(
      SHORT_DISTANCE,
      SHORT_SPEED_MS,
      true,
      "GPIO3 SHORT",
      "UP SHORT"
    );

    return;
  }

  // GPIO4 short -> DOWN 100
  if (downBtn.pendingShort) {
    downBtn.pendingShort = false;

    simulateSwipe(
      SHORT_DISTANCE,
      SHORT_SPEED_MS,
      false,
      "GPIO4 SHORT",
      "DOWN SHORT"
    );

    return;
  }

  // GPIO10 -> FAST ESCAPE
  if (escapeBtn.pendingShort) {
    escapeBtn.pendingShort = false;
    performEscapeGesture();
    return;
  }

  // GPIO1 -> LIKE
  if (likeBtn.pendingShort) {
    likeBtn.pendingShort = false;
    simulateDoubleClick();
    return;
  }
}

// =============================================
// SERIAL DEBUG
// =============================================

#if DEBUG_MODE

void printHelp() {
  Serial.println();
  Serial.println("====== SCROLLER V4.1 ======");
  Serial.println("p        -> FAST ESCAPE");
  Serial.println("s        -> UP SHORT 100");
  Serial.println("d        -> UP LONG 150");
  Serial.println("x        -> DOWN SHORT 100");
  Serial.println("c        -> DOWN LONG 150");
  Serial.println("r        -> DOUBLE CLICK / LIKE");
  Serial.println("e        -> EDGE CALIBRATION");
  Serial.println("cal      -> STANDARD CAL");
  Serial.println("status   -> OLED STATUS");
  Serial.println("auto on  -> AUTO CAL ON");
  Serial.println("auto off -> AUTO CAL OFF");
  Serial.println("up 120 8");
  Serial.println("down 180 10");
  Serial.println("100,200  -> MOVE CURSOR");
  Serial.println("help     -> SHOW COMMANDS");
  Serial.println("===========================");
}

void processCommand(String input) {
  input.trim();
  input.toLowerCase();

  if (input.length() == 0) return;

  DBG("[SERIAL] ");
  DBGLN(input);

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

    DBGLN("[AUTO CAL] ON");

    if (bleMouse.isConnected()) {
      showAction(SCREEN_STATUS, "AUTO CAL ON");
    }

    return;
  }

  if (input == "auto off") {
    autoCalibrate = false;

    DBGLN("[AUTO CAL] OFF");

    if (bleMouse.isConnected()) {
      showAction(SCREEN_STATUS, "AUTO CAL OFF");
    }

    return;
  }

  if (!bleMouse.isConnected()) {
    DBGLN("[ERROR] BLE not connected");
    drawScreen(SCREEN_SEARCHING);
    return;
  }

  if (input == "p") {
    performEscapeGesture();
  }

  else if (input == "s") {
    simulateSwipe(
      100, 6, true,
      "SERIAL S", "UP SHORT"
    );
  }

  else if (input == "d") {
    simulateSwipe(
      150, 12, true,
      "SERIAL D", "UP LONG"
    );
  }

  else if (input == "x") {
    simulateSwipe(
      100, 6, false,
      "SERIAL X", "DOWN SHORT"
    );
  }

  else if (input == "c") {
    simulateSwipe(
      150, 12, false,
      "SERIAL C", "DOWN LONG"
    );
  }

  else if (input == "r") {
    simulateDoubleClick();
  }

  else if (input == "e") {
    lastAction = "Edge calibration";

    if (calibrateEdge()) {
      showAction(SCREEN_CAL);
    }
  }

  else if (input == "cal") {
    lastAction = "Standard calibration";

    if (calibrateStandard()) {
      showAction(SCREEN_CAL);
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
      DBGLN("[ERROR] Example: up 120 8");
      showAction(SCREEN_ERROR);
    }
  }

  else if (input.indexOf(',') >= 0) {
    int x = 0;
    int y = 0;

    if (sscanf(
      input.c_str(),
      "%d,%d",
      &x,
      &y
    ) == 2) {

      lastAction = "Move cursor";

      drawScreen(SCREEN_MOVE);

      if (moveCursor(x, y)) {
        showAction(SCREEN_MOVE);
      }
    } else {
      DBGLN("[ERROR] Invalid coordinates");
      showAction(SCREEN_ERROR);
    }
  }

  else {
    DBGLN("[ERROR] Unknown command");
    showAction(SCREEN_ERROR);
  }
}

void executeSerialBuffer() {
  if (serialLength == 0) return;

  serialBuffer[serialLength] = '\0';

  String command(serialBuffer);
  serialLength = 0;

  processCommand(command);
}

void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();

    lastSerialByteAt = millis();

    if (ch == '\r' || ch == '\n') {
      if (serialOverflow) {
        serialOverflow = false;
        serialLength = 0;
      } else {
        executeSerialBuffer();
      }
    }

    else if (!serialOverflow) {
      if (serialLength < sizeof(serialBuffer) - 1) {
        serialBuffer[serialLength++] = ch;
      } else {
        serialOverflow = true;
        serialLength = 0;

        DBGLN("[ERROR] Command too long");
      }
    }
  }

  if (serialLength > 0 &&
      millis() - lastSerialByteAt >= SERIAL_IDLE_MS) {

    executeSerialBuffer();
  }

  if (serialOverflow &&
      millis() - lastSerialByteAt >= SERIAL_IDLE_MS) {

    serialOverflow = false;
    serialLength = 0;
  }
}

#endif

// =============================================
// SETUP
// =============================================

void setup() {
#if DEBUG_MODE
  Serial.begin(115200);

  unsigned long waitStart = millis();

  while (!Serial &&
         millis() - waitStart < 3000) {
    delay(10);
  }
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_ESCAPE, INPUT_PULLUP);
  pinMode(PIN_LIKE, INPUT_PULLUP);

  for (int i = 0; i < BUTTON_COUNT; i++) {
    syncButton(*buttons[i]);
  }

  u8g2.begin();
  drawScreen(SCREEN_SEARCHING);

  bleMouse.begin();

#if DEBUG_MODE
  DBGLN("ESP32-C3 SCROLLER V4.1");
  DBGLN("DEBUG MODE ON");
  printHelp();
#endif
}

// =============================================
// MAIN LOOP
// =============================================

void loop() {
  bool connected = bleMouse.isConnected();

  updateLED();

  if (connected != wasConnected) {
    wasConnected = connected;

    actionScreenActive = false;

    if (connected) {
      DBGLN("[BLE] Connected");
      drawScreen(SCREEN_READY);
    } else {
      DBGLN("[BLE] Disconnected");
      drawScreen(SCREEN_SEARCHING);
    }

    for (int i = 0; i < BUTTON_COUNT; i++) {
      syncButton(*buttons[i]);
    }

    comboCandidate = false;
    comboLatched = false;
    comboWindowOpen = false;
    suppressUntilRelease = false;
  }

#if DEBUG_MODE
  handleSerial();
#endif

  handleButtons();
  updateDisplay();

  delay(1);
}
