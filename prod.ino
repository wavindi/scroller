
#include <Arduino.h>
#include <BleMouse.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// =============================================
// ESP32-C3 SCROLLER V4.5 - PRODUCTION
// P / ESCAPE: PREMOVE DOWN 500 UNITS
// =============================================

#define DEBUG_MODE 0

#if DEBUG_MODE
  #define DBG(x) Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x) do {} while (0)
  #define DBGLN(x) do {} while (0)
#endif

// =============================================
// TYPES
// Keep before function prototypes
// =============================================

enum GestureType {
  G_UP_SHORT,
  G_UP_LONG,
  G_DOWN_SHORT,
  G_DOWN_LONG,
  G_ESCAPE,
  G_LIKE,
  G_CALIBRATE
};

enum EngineState {
  ENGINE_IDLE,
  ENGINE_ESCAPE_PREMOVE,
  ENGINE_ESCAPE_PAUSE,
  ENGINE_PRESS,
  ENGINE_PRESS_WAIT,
  ENGINE_DRAG,
  ENGINE_RELEASE_WAIT,
  ENGINE_POST_RELEASE,
  ENGINE_LIKE_PRESS_1,
  ENGINE_LIKE_RELEASE_1,
  ENGINE_LIKE_GAP,
  ENGINE_LIKE_PRESS_2,
  ENGINE_LIKE_RELEASE_2,
  ENGINE_LIKE_POST_RELEASE,
  ENGINE_CAL_MOVE_CORNER,
  ENGINE_CAL_PAUSE,
  ENGINE_CAL_MOVE_UP,
  ENGINE_DONE
};

struct Button {
  uint8_t pin;
  bool raw;
  bool stable;
  uint32_t changedAt;
  uint32_t pressedAt;
  bool longFired;
  bool pendingShort;
};

// Explicit prototypes prevent Arduino IDE type errors
bool enqueueGesture(GestureType gesture);
bool dequeueGesture(GestureType& gesture);
void setEngineState(EngineState state, uint32_t now);
void startGesture(GestureType gesture, uint32_t now);
void syncButton(Button& b);
void updateButton(Button& b, uint32_t now);

// =============================================
// HARDWARE
// =============================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, 6, 5
);

BleMouse bleMouse("scroller", "Custom IoT", 100);

const uint8_t LED_PIN = 8;
const uint8_t PIN_UP = 3;
const uint8_t PIN_DOWN = 4;
const uint8_t PIN_ESCAPE = 10;
const uint8_t PIN_LIKE = 1;

// =============================================
// SETTINGS
// =============================================

const uint32_t DEBOUNCE_MS = 40;
const uint32_t LONG_PRESS_MS = 500;
const uint32_t COMBO_HOLD_MS = 300;
const uint32_t DISPLAY_MS = 2000;
const uint32_t LED_BLINK_MS = 500;

const int SHORT_DISTANCE = 100;
const int LONG_DISTANCE = 150;

const uint32_t SHORT_SPEED_MS = 6;
const uint32_t LONG_SPEED_MS = 12;

// P / ESCAPE SETTINGS
// Changed from 200 to 500
const int ESCAPE_PREMOVE_DOWN = 500;
const int ESCAPE_DRAG_UP = 150;
const uint32_t ESCAPE_SPEED_MS = 4;

const uint32_t MOVE_INTERVAL_MS = 5;
const uint8_t QUEUE_SIZE = 8;

const int ICON_X = 64;
const int ICON_Y = 39;
const int TEXT_Y = 56;

// =============================================
// SYSTEM STATE
// =============================================

bool autoCalibrate = true;
bool wasConnected = false;
bool ledOn = false;

uint32_t lastLedBlink = 0;

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
uint32_t actionFinishedAt = 0;

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

// Running person + exit arrow
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

void updateDisplay(uint32_t now) {
  if (!bleMouse.isConnected()) {
    actionScreenActive = false;

    if (currentScreen != SCREEN_SEARCHING) {
      drawScreen(SCREEN_SEARCHING);
    }

    return;
  }

  if (actionScreenActive &&
      now - actionFinishedAt >= DISPLAY_MS) {

    actionScreenActive = false;
    drawScreen(SCREEN_READY);
  }
}

// =============================================
// LED
// =============================================

void updateLED(uint32_t now) {
  if (bleMouse.isConnected()) {
    ledOn = false;
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  if (now - lastLedBlink >= LED_BLINK_MS) {
    lastLedBlink = now;
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
  }
}

// =============================================
// GESTURE QUEUE
// =============================================

GestureType gestureQueue[QUEUE_SIZE];

uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

bool enqueueGesture(GestureType gesture) {
  if (queueCount >= QUEUE_SIZE) {
    return false;
  }

  gestureQueue[queueTail] = gesture;
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  queueCount++;

  return true;
}

bool dequeueGesture(GestureType& gesture) {
  if (queueCount == 0) return false;

  gesture = gestureQueue[queueHead];
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  queueCount--;

  return true;
}

void clearQueue() {
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
}

// =============================================
// NON-BLOCKING ENGINE
// =============================================

EngineState engineState = ENGINE_IDLE;
GestureType activeGesture = G_UP_SHORT;

uint32_t stateStartedAt = 0;
uint32_t lastMoveAt = 0;

int remainingX = 0;
int remainingY = 0;

int dragRemaining = 0;
int dragDirection = -1;
uint32_t dragSpeed = SHORT_SPEED_MS;

ScreenType activeScreen = SCREEN_READY;
const char* activeLabel = "READY";

bool mouseHeld = false;

void setEngineState(EngineState state, uint32_t now) {
  engineState = state;
  stateStartedAt = now;
}

void setMovement(int x, int y, uint32_t now) {
  remainingX = x;
  remainingY = y;
  lastMoveAt = now - MOVE_INTERVAL_MS;
}

bool tickMovement(uint32_t now) {
  if (remainingX == 0 && remainingY == 0) {
    return true;
  }

  if (now - lastMoveAt < MOVE_INTERVAL_MS) {
    return false;
  }

  int dx = constrain(remainingX, -127, 127);
  int dy = constrain(remainingY, -127, 127);

  bleMouse.move(dx, dy);

  remainingX -= dx;
  remainingY -= dy;
  lastMoveAt = now;

  return remainingX == 0 && remainingY == 0;
}

void beginCalibration(uint32_t now) {
  setMovement(5000, 5000, now);
  setEngineState(ENGINE_CAL_MOVE_CORNER, now);
}

void finishGesture(uint32_t now) {
  if (mouseHeld) {
    bleMouse.release(MOUSE_LEFT);
    mouseHeld = false;
  }

  if (autoCalibrate || activeGesture == G_CALIBRATE) {
    beginCalibration(now);
  } else {
    setEngineState(ENGINE_DONE, now);
  }
}

// =============================================
// START GESTURE
// =============================================

void startGesture(GestureType gesture, uint32_t now) {
  activeGesture = gesture;

  switch (gesture) {
    case G_UP_SHORT:
      activeScreen = SCREEN_UP;
      activeLabel = "UP SHORT";
      dragRemaining = SHORT_DISTANCE;
      dragDirection = -1;
      dragSpeed = SHORT_SPEED_MS;
      setEngineState(ENGINE_PRESS, now);
      break;

    case G_UP_LONG:
      activeScreen = SCREEN_UP;
      activeLabel = "UP LONG";
      dragRemaining = LONG_DISTANCE;
      dragDirection = -1;
      dragSpeed = LONG_SPEED_MS;
      setEngineState(ENGINE_PRESS, now);
      break;

    case G_DOWN_SHORT:
      activeScreen = SCREEN_DOWN;
      activeLabel = "DOWN SHORT";
      dragRemaining = SHORT_DISTANCE;
      dragDirection = 1;
      dragSpeed = SHORT_SPEED_MS;
      setEngineState(ENGINE_PRESS, now);
      break;

    case G_DOWN_LONG:
      activeScreen = SCREEN_DOWN;
      activeLabel = "DOWN LONG";
      dragRemaining = LONG_DISTANCE;
      dragDirection = 1;
      dragSpeed = LONG_SPEED_MS;
      setEngineState(ENGINE_PRESS, now);
      break;

    case G_ESCAPE:
      activeScreen = SCREEN_ESCAPE;
      activeLabel = "ESCAPE";
      dragRemaining = ESCAPE_DRAG_UP;
      dragDirection = -1;
      dragSpeed = ESCAPE_SPEED_MS;

      // NEW: Move DOWN 500 without clicking
      setMovement(0, ESCAPE_PREMOVE_DOWN, now);
      setEngineState(ENGINE_ESCAPE_PREMOVE, now);
      break;

    case G_LIKE:
      activeScreen = SCREEN_LIKE;
      activeLabel = "LIKE!";
      setEngineState(ENGINE_LIKE_PRESS_1, now);
      break;

    case G_CALIBRATE:
      activeScreen = SCREEN_CAL;
      activeLabel = "CALIBRATE";
      beginCalibration(now);
      break;
  }

  drawScreen(activeScreen, activeLabel);
  actionScreenActive = false;
}

// =============================================
// ENGINE TICK
// =============================================

void tickEngine(uint32_t now) {
  if (!bleMouse.isConnected()) return;

  if (engineState == ENGINE_IDLE) {
    GestureType nextGesture;

    if (dequeueGesture(nextGesture)) {
      startGesture(nextGesture, now);
    }

    return;
  }

  switch (engineState) {

    // ESCAPE: Move DOWN 500 without clicking
    case ENGINE_ESCAPE_PREMOVE:
      if (tickMovement(now)) {
        setEngineState(ENGINE_ESCAPE_PAUSE, now);
      }
      break;

    case ENGINE_ESCAPE_PAUSE:
      if (now - stateStartedAt >= 10) {
        setEngineState(ENGINE_PRESS, now);
      }
      break;

    case ENGINE_PRESS:
      bleMouse.press(MOUSE_LEFT);
      mouseHeld = true;
      setEngineState(ENGINE_PRESS_WAIT, now);
      break;

    case ENGINE_PRESS_WAIT: {
      uint32_t waitMs =
        (activeGesture == G_ESCAPE) ? 20 : 50;

      if (now - stateStartedAt >= waitMs) {
        lastMoveAt = now - dragSpeed;
        setEngineState(ENGINE_DRAG, now);
      }
      break;
    }

    case ENGINE_DRAG:
      if (now - lastMoveAt >= dragSpeed) {
        int step = min(dragRemaining, 10);

        if (step > 0) {
          bleMouse.move(0, dragDirection * step);
          dragRemaining -= step;
          lastMoveAt = now;
        }

        if (dragRemaining <= 0) {
          setEngineState(ENGINE_RELEASE_WAIT, now);
        }
      }
      break;

    case ENGINE_RELEASE_WAIT: {
      uint32_t waitMs =
        (activeGesture == G_ESCAPE) ? 5 : 20;

      if (now - stateStartedAt >= waitMs) {
        bleMouse.release(MOUSE_LEFT);
        mouseHeld = false;
        setEngineState(ENGINE_POST_RELEASE, now);
      }
      break;
    }

    case ENGINE_POST_RELEASE: {
      uint32_t waitMs =
        (activeGesture == G_ESCAPE) ? 20 : 100;

      if (now - stateStartedAt >= waitMs) {
        finishGesture(now);
      }
      break;
    }

    case ENGINE_LIKE_PRESS_1:
      bleMouse.press(MOUSE_LEFT);
      mouseHeld = true;
      setEngineState(ENGINE_LIKE_RELEASE_1, now);
      break;

    case ENGINE_LIKE_RELEASE_1:
      if (now - stateStartedAt >= 50) {
        bleMouse.release(MOUSE_LEFT);
        mouseHeld = false;
        setEngineState(ENGINE_LIKE_GAP, now);
      }
      break;

    case ENGINE_LIKE_GAP:
      if (now - stateStartedAt >= 100) {
        setEngineState(ENGINE_LIKE_PRESS_2, now);
      }
      break;

    case ENGINE_LIKE_PRESS_2:
      bleMouse.press(MOUSE_LEFT);
      mouseHeld = true;
      setEngineState(ENGINE_LIKE_RELEASE_2, now);
      break;

    case ENGINE_LIKE_RELEASE_2:
      if (now - stateStartedAt >= 50) {
        bleMouse.release(MOUSE_LEFT);
        mouseHeld = false;
        setEngineState(ENGINE_LIKE_POST_RELEASE, now);
      }
      break;

    case ENGINE_LIKE_POST_RELEASE:
      if (now - stateStartedAt >= 100) {
        finishGesture(now);
      }
      break;

    case ENGINE_CAL_MOVE_CORNER:
      if (tickMovement(now)) {
        setEngineState(ENGINE_CAL_PAUSE, now);
      }
      break;

    case ENGINE_CAL_PAUSE:
      if (now - stateStartedAt >= 100) {
        setMovement(0, -170, now);
        setEngineState(ENGINE_CAL_MOVE_UP, now);
      }
      break;

    case ENGINE_CAL_MOVE_UP:
      if (tickMovement(now)) {
        setEngineState(ENGINE_DONE, now);
      }
      break;

    case ENGINE_DONE:
      showAction(activeScreen, activeLabel);
      setEngineState(ENGINE_IDLE, now);
      break;

    default:
      break;
  }
}

// =============================================
// BUTTONS
// =============================================

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

bool comboCandidate = false;
bool suppressUntilRelease = false;
uint32_t allPressedAt = 0;

void syncButton(Button& b) {
  bool state = digitalRead(b.pin);

  b.raw = state;
  b.stable = state;
  b.changedAt = millis();
  b.pressedAt = millis();
  b.longFired = false;
  b.pendingShort = false;
}

void updateButton(Button& b, uint32_t now) {
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

void handleButtons(uint32_t now) {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    updateButton(*buttons[i], now);
  }

  if (!bleMouse.isConnected()) return;

  bool anyPressed = anyButtonPressed();
  bool allPressed = allButtonsPressed();

  if (suppressUntilRelease) {
    clearPendingActions();

    if (!anyPressed) {
      suppressUntilRelease = false;
    }

    return;
  }

  if (allPressed) {
    if (!comboCandidate) {
      comboCandidate = true;
      allPressedAt = now;
    }

    if (now - allPressedAt >= COMBO_HOLD_MS) {
      clearPendingActions();
      suppressUntilRelease = true;
      comboCandidate = false;
      enqueueGesture(G_CALIBRATE);
    }

    return;
  }

  comboCandidate = false;

  if (upBtn.stable == LOW &&
      !upBtn.longFired &&
      now - upBtn.pressedAt >= LONG_PRESS_MS) {

    upBtn.longFired = true;
    upBtn.pendingShort = false;
    enqueueGesture(G_UP_LONG);
  }

  if (downBtn.stable == LOW &&
      !downBtn.longFired &&
      now - downBtn.pressedAt >= LONG_PRESS_MS) {

    downBtn.longFired = true;
    downBtn.pendingShort = false;
    enqueueGesture(G_DOWN_LONG);
  }

  if (upBtn.pendingShort) {
    upBtn.pendingShort = false;
    enqueueGesture(G_UP_SHORT);
  }

  if (downBtn.pendingShort) {
    downBtn.pendingShort = false;
    enqueueGesture(G_DOWN_SHORT);
  }

  if (escapeBtn.pendingShort) {
    escapeBtn.pendingShort = false;
    enqueueGesture(G_ESCAPE);
  }

  if (likeBtn.pendingShort) {
    likeBtn.pendingShort = false;
    enqueueGesture(G_LIKE);
  }
}

// =============================================
// SETUP
// =============================================

void setup() {
#if DEBUG_MODE
  Serial.begin(115200);
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
  uint32_t now = millis();
  bool connected = bleMouse.isConnected();

  updateLED(now);

  if (connected != wasConnected) {
    wasConnected = connected;
    actionScreenActive = false;

    if (mouseHeld) {
      bleMouse.release(MOUSE_LEFT);
      mouseHeld = false;
    }

    engineState = ENGINE_IDLE;
    clearQueue();

    comboCandidate = false;
    suppressUntilRelease = false;

    for (int i = 0; i < BUTTON_COUNT; i++) {
      syncButton(*buttons[i]);
    }

    if (connected) {
      drawScreen(SCREEN_READY);
    } else {
      drawScreen(SCREEN_SEARCHING);
    }
  }

  handleButtons(now);
  tickEngine(now);
  updateDisplay(now);

  delay(1);
}
