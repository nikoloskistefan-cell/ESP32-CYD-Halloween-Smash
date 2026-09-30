#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// =====================================================
// HALLOWEEN SMASH! - ESP32 CYD
// Whack-a-Mole style viral arcade game
//
// Fixed scene. No full-screen redraw during gameplay.
// Only individual holes/targets/HUD regions are updated.
// =====================================================

// ---------------- TFT ----------------
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21

// ---------------- TOUCH ----------------
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define TOUCH_CS   33

#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3900
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3900

// ---------------- BUZZER ----------------
#define BUZZER_PIN 22

// ---------------- SCREEN ----------------
#define SCREEN_W 320
#define SCREEN_H 240
#define HUD_H 28

SPIClass tftSPI(HSPI);
SPIClass touchSPI(VSPI);

Adafruit_ILI9341 tft(&tftSPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

// =====================================================
// COLORS
// =====================================================

#define BG_NIGHT      0x080D
#define PURPLE_DARK   0x3008
#define PURPLE        0x780F
#define ORANGE        0xFD20
#define ORANGE_DARK   0xB300
#define GHOST_WHITE   0xFFFF
#define ZOMBIE_GREEN  0x87E0
#define STEM_GREEN    0x03E0
#define RED_BLOOD     0xF800
#define GOLD          0xFFE0
#define HOLE_DARK     0x2104
#define DIRT          0x7A40
#define MOON          0xFFE0

// =====================================================
// GAME STATE
// =====================================================

enum GameState {
  TITLE,
  PLAYING,
  LEVEL_COMPLETE,
  GAME_OVER
};

GameState gameState = TITLE;

// =====================================================
// TARGET TYPES
// =====================================================

enum TargetType {
  TARGET_NONE,
  TARGET_PUMPKIN,
  TARGET_GHOST,
  TARGET_ZOMBIE,
  TARGET_BLACK_CAT,
  TARGET_GOLDEN,
  TARGET_BOMB,
  TARGET_BOSS
};

// =====================================================
// HOLES
// =====================================================

#define HOLE_COUNT 9

const int holeX[HOLE_COUNT] = {
  54, 160, 266,
  54, 160, 266,
  54, 160, 266
};

const int holeY[HOLE_COUNT] = {
  75, 75, 75,
  130,130,130,
  185,185,185
};

struct HoleState {
  TargetType type;
  bool active;
  unsigned long shownAt;
  unsigned long duration;
};

HoleState holes[HOLE_COUNT];

// =====================================================
// GAME DATA
// =====================================================

int score = 0;
int lives = 3;
int level = 1;
int combo = 0;
int bestCombo = 0;

int hitsThisLevel = 0;
int targetHitsForLevel = 18;

unsigned long nextSpawnAt = 0;
unsigned long spawnInterval = 950;

unsigned long gameStarted = 0;
unsigned long levelStarted = 0;

bool touchWasDown = false;

// boss
int bossHP = 0;
int bossHole = -1;

// =====================================================
// NON-BLOCKING SOUND
// =====================================================

unsigned long toneUntil = 0;

void playToneFor(int freq, int ms) {
  ledcWriteTone(BUZZER_PIN, freq);
  toneUntil = millis() + ms;
}

void updateTone() {
  if (toneUntil > 0 && millis() >= toneUntil) {
    ledcWriteTone(BUZZER_PIN, 0);
    toneUntil = 0;
  }
}

// =====================================================
// INTRO MUSIC - original spooky motif
// =====================================================

const int introNotes[] = {
  262, 311, 370, 311,
  247, 294, 349, 294,
  220, 262, 330, 262,
  196, 247, 294, 392
};

const int introDur[] = {
  180,180,220,180,
  180,180,220,180,
  180,180,220,180,
  180,180,220,340
};

const int INTRO_COUNT = sizeof(introNotes) / sizeof(introNotes[0]);

int introIndex = 0;
unsigned long introStarted = 0;
bool introPlaying = false;

void startIntroMusic() {
  introIndex = 0;
  introStarted = millis();
  introPlaying = true;
  ledcWriteTone(BUZZER_PIN, introNotes[0]);
}

void stopIntroMusic() {
  introPlaying = false;
  ledcWriteTone(BUZZER_PIN, 0);
  toneUntil = 0;
}

void updateIntroMusic() {
  if (!introPlaying) return;

  unsigned long now = millis();

  if (now - introStarted >= (unsigned long)introDur[introIndex]) {
    introIndex++;
    if (introIndex >= INTRO_COUNT) introIndex = 0;

    introStarted = now;
    ledcWriteTone(BUZZER_PIN, introNotes[introIndex]);
  }
}

// =====================================================
// LEVEL COMPLETE MELODY
// =====================================================

void playLevelCompleteMelody() {
  const int notes[] = {
    523, 659, 784, 988,
    784, 1047, 1319
  };

  const int dur[] = {
    80, 80, 100, 120,
    80, 100, 220
  };

  for (int i = 0; i < 7; i++) {
    ledcWriteTone(BUZZER_PIN, notes[i]);
    delay(dur[i]);

    ledcWriteTone(BUZZER_PIN, 0);
    delay(22);
  }
}

// =====================================================
// TOUCH
// =====================================================

bool getNewTap(int &sx, int &sy) {
  TS_Point p = ts.getPoint();

  bool down = p.z > 200;
  bool newTap = down && !touchWasDown;

  touchWasDown = down;

  if (!newTap) return false;

  sx = map(
    p.x,
    TOUCH_MIN_X,
    TOUCH_MAX_X,
    0,
    SCREEN_W
  );

  sy = map(
    p.y,
    TOUCH_MIN_Y,
    TOUCH_MAX_Y,
    0,
    SCREEN_H
  );

  sx = constrain(sx, 0, SCREEN_W - 1);
  sy = constrain(sy, 0, SCREEN_H - 1);

  return true;
}

// =====================================================
// TEXT
// =====================================================

void centerText(
  const char* text,
  int y,
  int size,
  uint16_t color
) {
  tft.setTextSize(size);
  tft.setTextColor(color);

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(
    text,
    0,
    y,
    &x1,
    &y1,
    &w,
    &h
  );

  tft.setCursor(
    (SCREEN_W - w) / 2,
    y
  );

  tft.print(text);
}

// =====================================================
// BACKGROUND
// =====================================================

void drawMoon() {
  tft.fillCircle(282, 38, 18, MOON);
  tft.fillCircle(290, 32, 18, BG_NIGHT);
}

void drawGrave(int x, int y) {
  tft.fillRoundRect(x, y, 18, 25, 6, 0x8410);
  tft.fillRect(x, y + 12, 18, 13, 0x8410);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_BLACK);
  tft.setCursor(x + 3, y + 9);
  tft.print("RIP");
}

void drawStaticScene() {
  tft.fillScreen(BG_NIGHT);

  drawMoon();

  // stars
  const int stars[][2] = {
    {15,38},{35,52},{76,31},{115,44},
    {144,33},{201,41},{232,29},{307,56},
    {17,95},{302,105}
  };

  for (int i = 0; i < 10; i++) {
    tft.drawPixel(stars[i][0], stars[i][1], ILI9341_WHITE);
  }

  // ground
  tft.fillRect(0, 212, SCREEN_W, 28, PURPLE_DARK);

  drawGrave(5, 185);
  drawGrave(295, 188);

  // holes
  for (int i = 0; i < HOLE_COUNT; i++) {
    int hx = holeX[i];
    int hy = holeY[i] + 8;

    tft.fillRoundRect(
      hx - 28,
      hy,
      56,
      15,
      7,
      DIRT
    );

    tft.fillRoundRect(
      hx - 24,
      hy + 3,
      48,
      11,
      5,
      HOLE_DARK
    );

    tft.drawFastHLine(
      hx - 20,
      hy + 13,
      40,
      0x51E0
    );
  }
}

// =====================================================
// HUD
// =====================================================

void drawHUD() {
  tft.fillRect(
    0,
    0,
    SCREEN_W,
    HUD_H,
    ILI9341_BLACK
  );

  tft.setTextSize(1);

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(4, 7);
  tft.print("SCORE ");
  tft.print(score);

  tft.setCursor(115, 7);
  tft.print("LV ");
  tft.print(level);

  tft.setCursor(170, 7);
  tft.print("COMBO ");

  if (combo >= 10) {
    tft.setTextColor(GOLD);
  } else {
    tft.setTextColor(ILI9341_WHITE);
  }

  tft.print(combo);

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(260, 7);
  tft.print("HP ");

  tft.setTextColor(ILI9341_RED);

  for (int i = 0; i < lives; i++) {
    tft.print("*");
  }
}

// =====================================================
// TARGET DRAWING
// =====================================================

void drawPumpkin(int x, int y, bool golden) {
  uint16_t c = golden ? GOLD : ORANGE;

  tft.fillCircle(x - 5, y, 8, c);
  tft.fillCircle(x + 5, y, 8, c);
  tft.fillCircle(x, y, 9, c);

  tft.fillRect(x - 1, y - 12, 3, 5, STEM_GREEN);

  // eyes
  tft.fillTriangle(
    x - 7, y - 2,
    x - 3, y - 6,
    x - 1, y - 1,
    ILI9341_BLACK
  );

  tft.fillTriangle(
    x + 7, y - 2,
    x + 3, y - 6,
    x + 1, y - 1,
    ILI9341_BLACK
  );

  // grin
  tft.drawFastHLine(x - 6, y + 5, 12, ILI9341_BLACK);
}

void drawGhost(int x, int y) {
  tft.fillCircle(x, y - 4, 9, GHOST_WHITE);
  tft.fillRect(x - 9, y - 4, 18, 13, GHOST_WHITE);

  tft.fillTriangle(
    x - 9, y + 9,
    x - 3, y + 4,
    x, y + 9,
    GHOST_WHITE
  );

  tft.fillTriangle(
    x, y + 9,
    x + 4, y + 4,
    x + 9, y + 9,
    GHOST_WHITE
  );

  tft.fillCircle(x - 4, y - 5, 2, ILI9341_BLACK);
  tft.fillCircle(x + 4, y - 5, 2, ILI9341_BLACK);
  tft.fillCircle(x, y + 1, 2, ILI9341_BLACK);
}

void drawZombie(int x, int y) {
  tft.fillRoundRect(
    x - 9,
    y - 10,
    18,
    21,
    4,
    ZOMBIE_GREEN
  );

  tft.fillCircle(x - 4, y - 4, 2, ILI9341_WHITE);
  tft.fillCircle(x + 4, y - 4, 2, ILI9341_WHITE);

  tft.drawFastHLine(x - 5, y + 5, 10, ILI9341_BLACK);

  tft.drawLine(x - 8, y - 10, x - 3, y - 15, ILI9341_BLACK);
  tft.drawLine(x + 8, y - 10, x + 3, y - 15, ILI9341_BLACK);
}

void drawCat(int x, int y) {
  tft.fillCircle(x, y, 9, ILI9341_BLACK);

  tft.fillTriangle(
    x - 8, y - 5,
    x - 4, y - 14,
    x, y - 6,
    ILI9341_BLACK
  );

  tft.fillTriangle(
    x + 8, y - 5,
    x + 4, y - 14,
    x, y - 6,
    ILI9341_BLACK
  );

  tft.fillCircle(x - 3, y - 1, 1, GOLD);
  tft.fillCircle(x + 3, y - 1, 1, GOLD);
}

void drawBomb(int x, int y) {
  tft.fillCircle(x, y, 9, ILI9341_BLACK);
  tft.drawCircle(x, y, 9, ILI9341_RED);

  tft.drawLine(
    x + 5,
    y - 7,
    x + 10,
    y - 13,
    GOLD
  );

  tft.fillCircle(
    x + 11,
    y - 14,
    2,
    ORANGE
  );
}

void drawBoss(int x, int y) {
  // giant pumpkin monster
  tft.fillCircle(x - 8, y, 12, ORANGE_DARK);
  tft.fillCircle(x + 8, y, 12, ORANGE_DARK);
  tft.fillCircle(x, y, 14, ORANGE);

  tft.fillTriangle(
    x - 9, y - 3,
    x - 4, y - 8,
    x - 2, y - 1,
    ILI9341_BLACK
  );

  tft.fillTriangle(
    x + 9, y - 3,
    x + 4, y - 8,
    x + 2, y - 1,
    ILI9341_BLACK
  );

  tft.drawFastHLine(x - 8, y + 7, 16, ILI9341_BLACK);

  // horns
  tft.fillTriangle(
    x - 10, y - 9,
    x - 17, y - 17,
    x - 4, y - 11,
    PURPLE
  );

  tft.fillTriangle(
    x + 10, y - 9,
    x + 17, y - 17,
    x + 4, y - 11,
    PURPLE
  );
}

void clearHole(int index) {
  int x = holeX[index];
  int y = holeY[index];

  // cover only target area, not whole screen
  tft.fillRect(
    x - 22,
    y - 20,
    44,
    33,
    BG_NIGHT
  );

  // restore hole
  int hy = y + 8;

  tft.fillRoundRect(
    x - 28,
    hy,
    56,
    15,
    7,
    DIRT
  );

  tft.fillRoundRect(
    x - 24,
    hy + 3,
    48,
    11,
    5,
    HOLE_DARK
  );

  tft.drawFastHLine(
    x - 20,
    hy + 13,
    40,
    0x51E0
  );
}

void drawTarget(int index) {
  if (!holes[index].active) return;

  int x = holeX[index];
  int y = holeY[index];

  TargetType t = holes[index].type;

  if (t == TARGET_PUMPKIN) {
    drawPumpkin(x, y, false);
  }
  else if (t == TARGET_GHOST) {
    drawGhost(x, y);
  }
  else if (t == TARGET_ZOMBIE) {
    drawZombie(x, y);
  }
  else if (t == TARGET_BLACK_CAT) {
    drawCat(x, y);
  }
  else if (t == TARGET_GOLDEN) {
    drawPumpkin(x, y, true);
  }
  else if (t == TARGET_BOMB) {
    drawBomb(x, y);
  }
  else if (t == TARGET_BOSS) {
    drawBoss(x, y);
  }
}

// =====================================================
// TITLE SCREEN
// =====================================================

void drawTitle() {
  tft.fillScreen(BG_NIGHT);

  drawMoon();

  centerText(
    "HALLOWEEN",
    28,
    3,
    ORANGE
  );

  centerText(
    "SMASH!",
    62,
    4,
    PURPLE
  );

  centerText(
    "TAP FAST. TRUST NOTHING.",
    112,
    1,
    ILI9341_WHITE
  );

  drawPumpkin(90, 155, false);
  drawGhost(160, 155);
  drawBomb(230, 155);

  centerText(
    "GOLD = JACKPOT",
    182,
    1,
    GOLD
  );

  centerText(
    "BOMB = BAD IDEA",
    195,
    1,
    ILI9341_RED
  );

  tft.fillRoundRect(
    80,
    211,
    160,
    24,
    7,
    ORANGE_DARK
  );

  centerText(
    "TAP TO PLAY",
    218,
    1,
    ILI9341_WHITE
  );
}

// =====================================================
// TARGET SPAWN
// =====================================================

TargetType randomNormalTarget() {
  int roll = random(0, 100);

  // viral/risk-reward mix
  if (roll < 42) return TARGET_PUMPKIN;
  if (roll < 64) return TARGET_GHOST;
  if (roll < 80) return TARGET_ZOMBIE;
  if (roll < 89) return TARGET_BLACK_CAT;
  if (roll < 95) return TARGET_BOMB;

  return TARGET_GOLDEN;
}

void spawnTarget() {
  if (
    level % 4 == 0 &&
    bossHP <= 0
  ) {
    // boss round
    bossHole = 4;
    bossHP = 6 + level;

    holes[bossHole].active = true;
    holes[bossHole].type = TARGET_BOSS;
    holes[bossHole].shownAt = millis();
    holes[bossHole].duration = 4500;

    drawTarget(bossHole);
    return;
  }

  int candidates[HOLE_COUNT];
  int count = 0;

  for (int i = 0; i < HOLE_COUNT; i++) {
    if (!holes[i].active) {
      candidates[count++] = i;
    }
  }

  if (count == 0) return;

  int idx = candidates[random(0, count)];

  holes[idx].type =
    randomNormalTarget();

  holes[idx].active =
    true;

  holes[idx].shownAt =
    millis();

  unsigned long baseDuration =
    1350 - min(level * 75, 650);

  if (holes[idx].type == TARGET_GOLDEN) {
    baseDuration = 850;
  }

  if (holes[idx].type == TARGET_BOMB) {
    baseDuration = 1050;
  }

  holes[idx].duration =
    baseDuration;

  drawTarget(idx);
}

// =====================================================
// FEEDBACK
// =====================================================

void showFloatingText(
  int x,
  int y,
  const char* txt,
  uint16_t color
) {
  tft.setTextSize(1);
  tft.setTextColor(color, BG_NIGHT);
  tft.setCursor(x, y);
  tft.print(txt);
}

// =====================================================
// HIT LOGIC
// =====================================================

void hitTarget(int index) {
  if (!holes[index].active) return;

  TargetType type =
    holes[index].type;

  // boss
  if (type == TARGET_BOSS) {
    bossHP--;

    score += 150;
    combo++;

    if (combo > bestCombo) bestCombo = combo;

    clearHole(index);

    if (bossHP <= 0) {
      holes[index].active = false;
      bossHole = -1;

      score += 1500;
      hitsThisLevel = targetHitsForLevel;

      playToneFor(1760, 100);
      drawHUD();

      return;
    }

    holes[index].shownAt = millis();
    holes[index].duration = 3000;

    drawTarget(index);

    playToneFor(880 + bossHP * 40, 45);

    drawHUD();
    return;
  }

  // bomb = penalty
  if (type == TARGET_BOMB) {
    clearHole(index);

    holes[index].active = false;

    lives--;
    combo = 0;

    playToneFor(120, 220);

    drawHUD();

    if (lives <= 0) {
      gameState = GAME_OVER;
    }

    return;
  }

  // black cat = DON'T HIT
  if (type == TARGET_BLACK_CAT) {
    clearHole(index);

    holes[index].active = false;

    score = max(0, score - 250);
    combo = 0;

    playToneFor(180, 160);

    drawHUD();

    return;
  }

  // good targets
  int points = 100;

  if (type == TARGET_GHOST) points = 120;
  if (type == TARGET_ZOMBIE) points = 150;

  if (type == TARGET_GOLDEN) {
    points = 700;

    // special jackpot sound
    ledcWriteTone(BUZZER_PIN, 1319);
    delay(50);
    ledcWriteTone(BUZZER_PIN, 1760);
    delay(50);
    ledcWriteTone(BUZZER_PIN, 2093);
    delay(90);
    ledcWriteTone(BUZZER_PIN, 0);
  }
  else {
    playToneFor(
      900 + combo * 25,
      38
    );
  }

  combo++;

  if (combo > bestCombo) {
    bestCombo = combo;
  }

  int multiplier =
    1 + combo / 8;

  if (multiplier > 4) {
    multiplier = 4;
  }

  score +=
    points * multiplier;

  hitsThisLevel++;

  clearHole(index);

  holes[index].active =
    false;

  drawHUD();
}

// =====================================================
// MISSED TARGETS
// =====================================================

void updateTargets() {
  unsigned long now = millis();

  for (int i = 0; i < HOLE_COUNT; i++) {
    if (!holes[i].active) continue;

    if (
      now - holes[i].shownAt >=
      holes[i].duration
    ) {
      TargetType type =
        holes[i].type;

      clearHole(i);

      holes[i].active =
        false;

      // missing good target breaks combo
      if (
        type == TARGET_PUMPKIN ||
        type == TARGET_GHOST ||
        type == TARGET_ZOMBIE ||
        type == TARGET_GOLDEN
      ) {
        combo = 0;
        drawHUD();
      }

      // Cat/bomb disappearing is good: no penalty.
      // Boss escaping costs a life.
      if (type == TARGET_BOSS) {
        bossHP = 0;
        bossHole = -1;

        lives--;
        combo = 0;

        playToneFor(170, 170);

        drawHUD();

        if (lives <= 0) {
          gameState = GAME_OVER;
        }
      }
    }
  }

  if (
    now >= nextSpawnAt
  ) {
    spawnTarget();

    // Schedule the next appearance from NOW.
    // Small random variation makes the game feel less robotic.
    unsigned long variation =
      random(0, 180);

    nextSpawnAt =
      now +
      spawnInterval +
      variation;
  }
}

// =====================================================
// TAP DETECTION
// =====================================================

int holeFromTap(int x, int y) {
  for (int i = 0; i < HOLE_COUNT; i++) {
    int dx = x - holeX[i];
    int dy = y - holeY[i];

    if (
      dx * dx + dy * dy <
      29 * 29
    ) {
      return i;
    }
  }

  return -1;
}

// =====================================================
// LEVEL MANAGEMENT
// =====================================================

void resetHoles() {
  for (int i = 0; i < HOLE_COUNT; i++) {
    holes[i].active = false;
    holes[i].type = TARGET_NONE;
    holes[i].shownAt = 0;
    holes[i].duration = 0;
  }
}

void startLevel() {
  hitsThisLevel = 0;

  targetHitsForLevel =
    15 + level * 3;

  spawnInterval =
    max(
      420UL,
      950UL -
      (unsigned long)(level - 1) * 70UL
    );

  bossHP = 0;
  bossHole = -1;

  resetHoles();

  drawStaticScene();
  drawHUD();

  levelStarted = millis();

  // First target: fixed center pumpkin, visible long enough to verify gameplay.
  holes[4].type = TARGET_PUMPKIN;
  holes[4].active = true;
  holes[4].shownAt = millis();
  holes[4].duration = 2200;

  drawTarget(4);

  nextSpawnAt =
    millis() +
    1200;
}

void nextLevel() {
  gameState =
    LEVEL_COMPLETE;

  playLevelCompleteMelody();

  level++;

  if (level > 8) {
    level = 8;
  }

  combo = 0;

  startLevel();

  gameState =
    PLAYING;
}

// =====================================================
// GAME OVER
// =====================================================

void drawGameOver() {
  tft.fillScreen(BG_NIGHT);

  centerText(
    "YOU GOT",
    42,
    3,
    ILI9341_WHITE
  );

  centerText(
    "SPOOKED!",
    78,
    4,
    ORANGE
  );

  centerText(
    "FINAL SCORE",
    135,
    1,
    ILI9341_WHITE
  );

  char buf[20];
  sprintf(buf, "%d", score);

  centerText(
    buf,
    151,
    2,
    GOLD
  );

  centerText(
    "BEST COMBO",
    181,
    1,
    ILI9341_WHITE
  );

  sprintf(buf, "%d", bestCombo);

  centerText(
    buf,
    195,
    2,
    PURPLE
  );

  centerText(
    "TAP TO RESTART",
    224,
    1,
    ILI9341_WHITE
  );
}

// =====================================================
// NEW GAME
// =====================================================

void startGame() {
  stopIntroMusic();

  score = 0;
  lives = 3;
  level = 1;
  combo = 0;
  bestCombo = 0;

  gameStarted = millis();

  startLevel();

  gameState =
    PLAYING;
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  ledcAttach(
    BUZZER_PIN,
    2000,
    8
  );

  ledcWriteTone(
    BUZZER_PIN,
    0
  );

  pinMode(
    TFT_BL,
    OUTPUT
  );

  digitalWrite(
    TFT_BL,
    HIGH
  );

  tftSPI.begin(
    TFT_SCLK,
    TFT_MISO,
    TFT_MOSI,
    TFT_CS
  );

  tft.begin();

  tft.setRotation(1);
  tft.setTextWrap(false);

  touchSPI.begin(
    TOUCH_CLK,
    TOUCH_MISO,
    TOUCH_MOSI,
    TOUCH_CS
  );

  ts.begin(
    touchSPI
  );

  ts.setRotation(1);

  randomSeed(
    micros()
  );

  drawTitle();

  startIntroMusic();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  updateTone();

  // ===================================================
  // TITLE
  // ===================================================

  if (gameState == TITLE) {
    updateIntroMusic();

    int x, y;

    if (getNewTap(x, y)) {
      startGame();
    }

    return;
  }

  // ===================================================
  // GAME OVER
  // ===================================================

  if (gameState == GAME_OVER) {
    static bool drawn = false;

    if (!drawn) {
      drawGameOver();
      drawn = true;
    }

    int x, y;

    if (getNewTap(x, y)) {
      drawn = false;

      drawTitle();

      gameState = TITLE;
      startIntroMusic();
    }

    return;
  }

  // ===================================================
  // PLAYING
  // ===================================================

  if (gameState == PLAYING) {
    int x, y;

    if (getNewTap(x, y)) {
      int h =
        holeFromTap(x, y);

      if (
        h >= 0 &&
        holes[h].active
      ) {
        hitTarget(h);
      }
      else {
        // missed tap breaks combo slightly but no harsh penalty
        if (combo > 0) {
          combo--;
          drawHUD();
        }

        playToneFor(240, 25);
      }
    }

    updateTargets();

    if (
      hitsThisLevel >=
      targetHitsForLevel
    ) {
      nextLevel();
    }
  }
}
