
#include <BleMouse.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// =============================================
// ESP32-C3 SCROLLER V4.2 - PRODUCTION
// =============================================

// PRODUCTION: Serial Monitor completely disabled
#define DEBUG_MODE 0

#if DEBUG_MODE
  #define DBG(x) Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x) do {} while (0)
  #define DBGLN(x) do {} while (0)
#endif

// OLED - confirmed working configuration
// SCL = GPIO6, SDA = GPIO5
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, 6, 5
);

// Bluetooth mouse
BleMouse bleMouse("scroller", "Custom IoT", 100);

// =============================================
// GPIO PINS
// =============================================
const uint8_t LED_PIN = 8;

const uint8_t PIN_UP = 3;
const uint8_t PIN_DOWN = 4;
const uint8_t PIN_ESCAPE = 10;
const uint8_t PIN_LIKE = 1;

// =============================================
// SETTINGS
// =============================================
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 500;
const unsigned long COMBO_HOLD_MS = 300;
const unsigned long COMBO_WINDOW_MS = 180;
const unsigned long DISPLAY_MS = 2000;
const unsigned long LED_BLINK_MS = 500;

const int SHORT_DISTANCE = 100;
const int LONG_DISTANCE = 150;

const int SHORT_SPEED_MS = 6;
const int LONG_SPEED_MS = 12;

// Fast ESCAPE gesture
const int ESCAPE_PREMOVE_DOWN = 200;
const int ESCAPE_DRAG_UP = 150;
const int ESCAPE_SPEED_MS = 4;

// OLED placement
const int ICON_X = 64;
const int ICON_Y = 39;
const int TEXT_Y = 56;

// =============================================
// SYSTEM STATE
// =============================================
bool autoCalibrate = true;
bool wasConnected = false;

bool ledOn = false;
unsigned long lastLedBlink = 0;

enum ScreenType {
  SCREEN_SEARCHING,
  SCREEN_READY,
  SCREEN_UP,
  SCREEN_DOWN,
  SCREEN_LIKE,
  SCREEN_ESCAPE,
  SCREEN_CAL,
  SCREEN_ERROR
};

ScreenType currentScreen = SCREEN_SEARCHING;

bool actionScreenActive = false;
unsigned long actionFinishedAt = 0;

// =============================================
// BUTTON STATE
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
bool suppressUntilRelease = false;

unsigned long allPressedAt = 0;

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

// ESCAPE: Running person + exit arrow
void iconEscape(int x, int y) {
  u8g2.drawDisc(x - 4, y - 9, 3);

  u8g2.drawLine(x - 4, y - 5, x + 2, y + 2);

  u8g2.drawLine(x - 2, y - 3, x + 6, y - 6);
  u8g2.drawLine(x - 2, y - 2, x - 10, y + 1);

  u8g2.drawLine(x + 2, y + 2, x + 9, y + 8);
  u8g2.drawLine(x + 2, y + 2, x - 7, y + 9);

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
// STATUS LED
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

    // Active LOW LED
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
// EDGE CALIBRATION
// =============================================

bool calibrateEdge(bool displayIcon = true) {
  if (!bleMouse.isConnected()) return false;

  if (displayIcon) {
    drawScreen(SCREEN_CAL);
  }

  if (!moveCursor(5000, 5000)) return false;

  delay(100);

  if (!moveCursor(0, -100)) return false;
  if (!moveCursor(0, -70)) return false;

  return true;
}

// =============================================
// SWIPE
// =============================================

void simulateSwipe(
  int distance,
  int speedDelay,
  bool swipeUp,
  const char* label
) {
  if (!bleMouse.isConnected()) return;

  if (distance < 1 || distance > 2000 ||
      speedDelay < 1 || speedDelay > 100) {

    showAction(SCREEN_ERROR);
    return;
  }

  ScreenType screen =
    swipeUp ? SCREEN_UP : SCREEN_DOWN;

  drawScreen(screen, label);

  // Press and hold left mouse button
  bleMouse.press(MOUSE_LEFT);
  delay(50);

  // Drag
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

  // Release
  delay(20);
  bleMouse.release(MOUSE_LEFT);
  delay(100);

  // Auto calibration
  if (autoCalibrate && bleMouse.isConnected()) {
    if (!calibrateEdge(false)) return;
  }

  if (bleMouse.isConnected()) {
    showAction(screen, label);
  }
}

// =============================================
// FAST ESCAPE GESTURE
// =============================================

void performEscapeGesture() {
  if (!bleMouse.isConnected()) return;

  drawScreen(SCREEN_ESCAPE);

  // 1. Move DOWN 200, no click
  if (!moveCursor(0, ESCAPE_PREMOVE_DOWN)) return;

  delay(10);

  if (!bleMouse.isConnected()) return;

  // 2. Press left mouse button
  bleMouse.press(MOUSE_LEFT);
  delay(20);

  // 3. Drag UP 150 quickly
  int remaining = ESCAPE_DRAG_UP;

  while (remaining > 0) {
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
}

// =============================================
// DOUBLE CLICK / LIKE
// =============================================

void simulateDoubleClick() {
  if (!bleMouse.isConnected()) return;

  drawScreen(SCREEN_LIKE);

  // First click
  bleMouse.press(MOUSE_LEFT);
  delay(50);
  bleMouse.release(MOUSE_LEFT);

  delay(100);

  if (!bleMouse.isConnected()) return;

  // Second click
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
// BUTTON INPUT
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

  // Ignore button releases after combo
  if (suppressUntilRelease) {
    clearPendingActions();

    if (!anyPressed) {
      suppressUntilRelease = false;
      comboLatched = false;
    }

    return;
  }

  // All four held -> edge calibration
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

      if (calibrateEdge()) {
        showAction(SCREEN_CAL);
      }

      return;
    }
  } else {
    comboCandidate = false;
  }

  // Don't execute actions while all four
  // buttons are held
  if (allPressed || comboCandidate) return;

  // Long UP
  if (upBtn.stable == LOW &&
      !upBtn.longFired &&
      now - upBtn.pressedAt >= LONG_PRESS_MS) {

    upBtn.longFired = true;
    upBtn.pendingShort = false;

    simulateSwipe(
      LONG_DISTANCE,
      LONG_SPEED_MS,
      true,
      "UP LONG"
    );

    return;
  }

  // Long DOWN
  if (downBtn.stable == LOW &&
      !downBtn.longFired &&
      now - downBtn.pressedAt >= LONG_PRESS_MS) {

    downBtn.longFired = true;
    downBtn.pendingShort = false;

    simulateSwipe(
      LONG_DISTANCE,
      LONG_SPEED_MS,
      false,
      "DOWN LONG"
    );

    return;
  }

  // Short UP
  if (upBtn.pendingShort) {
    upBtn.pendingShort = false;

    simulateSwipe(
      SHORT_DISTANCE,
      SHORT_SPEED_MS,
      true,
      "UP SHORT"
    );

    return;
  }

  // Short DOWN
  if (downBtn.pendingShort) {
    downBtn.pendingShort = false;

    simulateSwipe(
      SHORT_DISTANCE,
      SHORT_SPEED_MS,
      false,
      "DOWN SHORT"
    );

    return;
  }

  // GPIO10 -> ESCAPE
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
// SETUP
// =============================================

void setup() {
#if DEBUG_MODE
  Serial.begin(115200);
  DBGLN("ESP32-C3 SCROLLER V4.2");
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
      drawScreen(SCREEN_READY);
    } else {
      drawScreen(SCREEN_SEARCHING);
    }

    for (int i = 0; i < BUTTON_COUNT; i++) {
      syncButton(*buttons[i]);
    }

    comboCandidate = false;
    comboLatched = false;
    suppressUntilRelease = false;
  }

  handleButtons();
  updateDisplay();

  delay(1);
}
