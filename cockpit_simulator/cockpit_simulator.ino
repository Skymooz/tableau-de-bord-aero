// ============================================================
//  COCKPIT SIMULATOR v2.1 - Arduino MEGA
//  Corrections :
//    - TFT : init robuste (tab configurable), SPI matériel
//    - LCD vitesse : broche contraste PWM ajoutée
//    - vitToAngle() : calcul linéaire correct (map int→float)
//    - Horizon : réduction du ghosting
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LiquidCrystal.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ============================================================
// CONFIGURATION DES BROCHES (Arduino MEGA)
// ============================================================

#define TRIG_PIN   22
#define ECHO_PIN   23

#define LED_VERT    24
#define LED_JAUNE   26
#define LED_ROUGE   28
#define BUZZER_PIN  30

// TFT 1.8" — SPI MATÉRIEL MEGA : MOSI=51, SCK=52 (câbler là !)
#define TFT18_CS   53
#define TFT18_DC   48
#define TFT18_RST  49

// ⚠️  ADAPTER SELON VOTRE MODULE :
//   INITR_BLACKTAB  → PCB noir
//   INITR_GREENTAB  → PCB vert  (le plus courant en clone)
//   INITR_REDTAB    → PCB rouge
#define TFT_TAB  INITR_GREENTAB

// LCD 16×2 Altimètre
#define LCD_RS   7
#define LCD_EN   6
#define LCD_D4   5
#define LCD_D5   4
#define LCD_D6   3
#define LCD_D7   2
#define LCD_CONTRASTE_PIN  8   // PWM → V0 du LCD altimètre

// LCD 16×2 Vitesse
#define LCD_VIT_RS  34
#define LCD_VIT_EN  35
#define LCD_VIT_D4  36
#define LCD_VIT_D5  37
#define LCD_VIT_D6  38
#define LCD_VIT_D7  39
#define LCD_VIT_CONTRASTE_PIN  9  // ← NOUVEAU : PWM → V0 du LCD vitesse
                                   //   câbler la broche 9 sur V0 du 2ème LCD

// ============================================================
// INITIALISATION DES OBJETS
// ============================================================

LiquidCrystal_I2C lcdGPWS(0x27, 16, 2);
LiquidCrystal    lcdAltimetre(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
LiquidCrystal    lcdVitesse(LCD_VIT_RS, LCD_VIT_EN, LCD_VIT_D4, LCD_VIT_D5, LCD_VIT_D6, LCD_VIT_D7);
Adafruit_ST7735  tft = Adafruit_ST7735(TFT18_CS, TFT18_DC, TFT18_RST);

// ============================================================
// DIMENSIONS & COULEURS
// ============================================================
#define SCR_W  160
#define SCR_H  128
#define CX     80
#define CY     64
#define R_EXT  60

#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_ORANGE  0xFD20
#define C_DKGRAY  0x4208
#define C_LTGRAY  0xBDF7
#define C_SKY     0x051F
#define C_GROUND  0x8200

// ============================================================
// VARIABLES DE VOL
// ============================================================
float altitude         = 5000.0;
float vitesse          = 150.0;
float pitch            = 0.0;
float roll             = 0.0;
float cap              = 90.0;
float vitesseVerticale = 0.0;
float tauxVirage       = 0.0;

float phaseAlt   = 0.0, phaseVit   = 1.2, phasePitch = 0.7;
float phaseRoll  = 2.1, phaseCap   = 0.0, phaseVario = 0.3;
float phaseTurn  = 1.5;

// ============================================================
// GESTION DU TEMPS & PAGES
// ============================================================
unsigned long tPrev       = 0;
unsigned long tBuzzer     = 0;
unsigned long tPageChange = 0;
bool          buzzerState = false;

int  page        = 0;
bool pageChanged = true;
const int  NB_PAGES       = 6;
const unsigned long PAGE_INTERVAL = 5000;

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN,  OUTPUT);
  pinMode(ECHO_PIN,  INPUT);
  pinMode(LED_VERT,  OUTPUT);
  pinMode(LED_JAUNE, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // --- LCD GPWS (I2C) ---
  lcdGPWS.init();
  lcdGPWS.backlight();
  lcdGPWS.setCursor(0, 0); lcdGPWS.print("GPWS INIT...");

  // --- LCD Altimètre ---
  pinMode(LCD_CONTRASTE_PIN, OUTPUT);
  analogWrite(LCD_CONTRASTE_PIN, 90);   // ajuster entre 0-255 si besoin
  lcdAltimetre.begin(16, 2);
  lcdAltimetre.print("ALTIMETRE OK");

  // --- LCD Vitesse (CORRECTION : contraste ajouté) ---
  pinMode(LCD_VIT_CONTRASTE_PIN, OUTPUT);
  analogWrite(LCD_VIT_CONTRASTE_PIN, 90); // même valeur de départ, ajustable
  lcdVitesse.begin(16, 2);
  lcdVitesse.print("ANEMOMETRE OK");

  // --- TFT 1.8" ---
  tft.initR(TFT_TAB);      // ← utilise la constante définie en haut
  tft.setRotation(1);      // paysage 160×128
  tft.fillScreen(C_BLACK);

  // Test visuel de démarrage : rectangle coloré 2 s
  // Si vous voyez ce rectangle, le TFT fonctionne.
  // Sinon → problème de câblage (MOSI/SCK) ou de tension (5V au lieu de 3.3V)
  tft.fillRect(10, 10, 140, 108, C_BLUE);
  tft.setTextColor(C_WHITE, C_BLUE);
  tft.setTextSize(1);
  tft.setCursor(30, 55);
  tft.print("TFT OK - v2.1");
  delay(2000);
  tft.fillScreen(C_BLACK);

  delay(500);
  lcdGPWS.clear();
  lcdAltimetre.clear();
  lcdVitesse.clear();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long tNow = millis();

  if (tNow - tPageChange >= PAGE_INTERVAL) {
    tPageChange = tNow;
    page = (page + 1) % NB_PAGES;
    tft.fillScreen(C_BLACK);
    pageChanged = true;
  }

  if (tNow - tPrev >= 100) {
    tPrev = tNow;

    simulerVol(tNow);
    gererGPWS(mesurerDistance(), tNow);
    afficherAltimetreLCD();
    afficherVitesseLCD();

    switch (page) {
      case 0: afficherHorizon();                                                break;
      case 1: if (pageChanged) { dessinerFondAnemometre(); pageChanged = false; }
              majAnemometre();                                                   break;
      case 2: if (pageChanged) { dessinerFondCompas();     pageChanged = false; }
              majCompas();                                                       break;
      case 3: if (pageChanged) { dessinerFondAltimetreTFT(); pageChanged = false; }
              majAltimetreTFT();                                                 break;
      case 4: if (pageChanged) { dessinerFondVario();      pageChanged = false; }
              majVario();                                                        break;
      case 5: if (pageChanged) { dessinerFondVirage();     pageChanged = false; }
              majVirage();                                                       break;
    }
  }
}

// ============================================================
//  SIMULATION
// ============================================================
void simulerVol(unsigned long tMs) {
  phaseAlt   += 0.008; phaseVit   += 0.005; phasePitch += 0.011;
  phaseRoll  += 0.007; phaseCap   += 0.004; phaseVario += 0.012;
  phaseTurn  += 0.009;

  float altBase = 8000.0 + sin(phaseAlt) * 2000.0;
  altitude      = altBase + sin(phaseAlt * 3.7) * 150.0 + sin(phaseAlt * 7.1) * 50.0;

  vitesse = 180.0 + sin(phaseVit) * 60.0 + sin(phaseVit * 4.3) * 15.0;
  vitesse = constrain(vitesse, 80.0, 280.0);

  pitch = sin(phasePitch) * 10.0 + sin(phasePitch * 2.9) * 3.0;
  roll  = sin(phaseRoll)  * 20.0 + sin(phaseRoll  * 3.1) * 6.0;

  cap = fmod(cap + sin(phaseCap) * 0.8, 360.0);
  if (cap < 0) cap += 360.0;

  vitesseVerticale = constrain(
    sin(phaseVario) * 800.0 + sin(phaseVario * 2.5) * 300.0,
    -2000.0, 2000.0);

  tauxVirage = sin(phaseTurn) * 25.0;
}

// ============================================================
//  GPWS
// ============================================================
long mesurerDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duree = pulseIn(ECHO_PIN, HIGH, 30000);
  return (duree * 0.034) / 2;
}

void gererGPWS(long dist, unsigned long tNow) {
  digitalWrite(LED_VERT,  LOW);
  digitalWrite(LED_JAUNE, LOW);
  digitalWrite(LED_ROUGE, LOW);
  lcdGPWS.setCursor(0, 0); lcdGPWS.print("GPWS  DIST:     ");
  lcdGPWS.setCursor(0, 1);

  if (dist == 0 || dist > 100) {
    digitalWrite(LED_VERT, HIGH);
    lcdGPWS.print("TERRAIN OK      ");
    noTone(BUZZER_PIN);
  } else if (dist > 30) {
    digitalWrite(LED_JAUNE, HIGH);
    lcdGPWS.print("CAUTION TERRAIN ");
    if (tNow - tBuzzer > 500) {
      tBuzzer = tNow; buzzerState = !buzzerState;
      buzzerState ? tone(BUZZER_PIN, 1000) : noTone(BUZZER_PIN);
    }
  } else {
    digitalWrite(LED_ROUGE, HIGH);
    lcdGPWS.print("!PULL UP PULL UP");
    if (tNow - tBuzzer > 120) {
      tBuzzer = tNow; buzzerState = !buzzerState;
      buzzerState ? tone(BUZZER_PIN, 1500) : noTone(BUZZER_PIN);
    }
  }
}

// ============================================================
//  LCD AFFICHAGE
// ============================================================
void afficherAltimetreLCD() {
  lcdAltimetre.setCursor(0, 0); lcdAltimetre.print("ALT MSL (ft):   ");
  lcdAltimetre.setCursor(0, 1);
  char buf[16];
  snprintf(buf, sizeof(buf), "%-7ld         ", (long)altitude);
  lcdAltimetre.print(buf);
}

void afficherVitesseLCD() {
  lcdVitesse.setCursor(0, 0); lcdVitesse.print("IAS (kts):      ");
  lcdVitesse.setCursor(0, 1);
  char buf[16];
  snprintf(buf, sizeof(buf), "%-5d           ", (int)vitesse);
  lcdVitesse.print(buf);
}

// ============================================================
//  UTILITAIRES TFT
// ============================================================
void drawTick(int cx, int cy, float angleDeg, int r_int, int r_ext, uint16_t col) {
  float rad = (angleDeg - 90.0) * PI / 180.0;
  tft.drawLine(cx + r_int * cos(rad), cy + r_int * sin(rad),
               cx + r_ext * cos(rad), cy + r_ext * sin(rad), col);
}

void drawNeedle(int cx, int cy, float angleDeg, int length, uint16_t col) {
  float rad = (angleDeg - 90.0) * PI / 180.0;
  tft.drawLine(cx, cy, cx + length * cos(rad), cy + length * sin(rad), col);
}

void printCentered(const char* txt, int x, int y, uint8_t sz, uint16_t col) {
  tft.setTextSize(sz);
  tft.setTextColor(col, C_BLACK);
  int tw = strlen(txt) * 6 * sz;
  tft.setCursor(x - tw / 2, y - 4 * sz);
  tft.print(txt);
}

// ============================================================
//  PAGE 0 — HORIZON ARTIFICIEL
// ============================================================
void afficherHorizon() {
  float radRoll  = roll * PI / 180.0;
  float cosR     = cos(radRoll);
  float sinR     = sin(radRoll);
  float pitchOff = pitch * 2.0;

  // Évite la division par zéro si roll ≈ ±90°
  float cosRsafe = (fabs(cosR) < 0.01) ? 0.01 : cosR;

  for (int x = 0; x < SCR_W; x++) {
    int yH = (int)(CY - pitchOff * cosR + (x - CX) * sinR / cosRsafe);
    yH = constrain(yH, 0, SCR_H);
    tft.drawFastVLine(x, 0,   yH,          C_SKY);
    tft.drawFastVLine(x, yH,  SCR_H - yH,  C_GROUND);
  }

  // Ligne d'horizon
  int y1H = constrain((int)(CY - pitchOff * cosR + (0       - CX) * sinR / cosRsafe), 0, SCR_H - 1);
  int y2H = constrain((int)(CY - pitchOff * cosR + (SCR_W   - CX) * sinR / cosRsafe), 0, SCR_H - 1);
  tft.drawLine(0, y1H, SCR_W, y2H, C_WHITE);

  // Échelle de pitch
  for (int p = -20; p <= 20; p += 10) {
    if (p == 0) continue;
    float off     = (pitch - p) * 2.0;
    int halfLen   = (p % 20 == 0) ? 20 : 12;
    int x1 = CX + (-halfLen) * cosR - off * sinR;
    int y1 = CY + (-halfLen) * sinR + off * cosR;
    int x2 = CX + ( halfLen) * cosR - off * sinR;
    int y2 = CY + ( halfLen) * sinR + off * cosR;
    tft.drawLine(x1, y1, x2, y2, C_WHITE);
  }

  // Réticule fixe
  tft.drawFastHLine(CX - 35, CY, 25, C_YELLOW);
  tft.drawFastHLine(CX + 10, CY, 25, C_YELLOW);
  tft.fillRect(CX - 3, CY - 3, 7, 7, C_YELLOW);

  // Valeurs numériques
  tft.setTextColor(C_WHITE, C_BLACK); tft.setTextSize(1);
  tft.setCursor(2, 2);  tft.print("P:"); tft.print((int)pitch); tft.print("  ");
  tft.setCursor(2, 12); tft.print("R:"); tft.print((int)roll);  tft.print("  ");
}

// ============================================================
//  PAGE 1 — ANÉMOMÈTRE
//  CORRECTION : vitToAngle() utilise un calcul flottant direct
//  (map() Arduino ne fonctionne qu'avec des entiers long)
// ============================================================
float vitToAngle(float v) {
  v = constrain(v, 60.0, 300.0);
  // 60 kts → 225°  |  300 kts → 225+270 = 495° (≡ 135° après fmod)
  return 225.0 + (v - 60.0) / (300.0 - 60.0) * 270.0;
}

void dessinerFondAnemometre() {
  tft.drawCircle(CX, CY, R_EXT,     C_WHITE);
  tft.drawCircle(CX, CY, R_EXT - 1, C_DKGRAY);

  for (int v = 60; v <= 300; v += 20) {
    float a   = vitToAngle(v);
    int   len = (v % 100 == 0) ? 10 : 6;
    drawTick(CX, CY, a, R_EXT - len - 1, R_EXT - 1, C_WHITE);
    if (v % 100 == 0) {
      float rad = (a - 90.0) * PI / 180.0;
      char buf[4]; itoa(v / 10, buf, 10);
      tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BLACK);
      tft.setCursor(CX + (R_EXT - 20) * cos(rad) - 3,
                    CY + (R_EXT - 20) * sin(rad) - 4);
      tft.print(buf);
    }
  }
  // Arc vert 80–200 kts
  for (float v = 80; v <= 200; v += 5.0) {
    float rad = (vitToAngle(v) - 90.0) * PI / 180.0;
    tft.drawPixel(CX + (R_EXT - 3) * cos(rad), CY + (R_EXT - 3) * sin(rad), C_GREEN);
  }
  // Arc rouge 220–300 kts
  for (float v = 220; v <= 300; v += 5.0) {
    float rad = (vitToAngle(v) - 90.0) * PI / 180.0;
    tft.drawPixel(CX + (R_EXT - 3) * cos(rad), CY + (R_EXT - 3) * sin(rad), C_RED);
  }
  printCentered("IAS",   CX, CY + 20, 1, C_CYAN);
  printCentered("KNOTS", CX, CY + 30, 1, C_LTGRAY);
}

static float vit_old = 0;
void majAnemometre() {
  float oldA = vitToAngle(vit_old);
  float newA = vitToAngle(vitesse);
  float radO = (oldA - 90.0) * PI / 180.0;
  float radN = (newA - 90.0) * PI / 180.0;
  tft.drawLine(CX, CY, CX + 48 * cos(radO), CY + 48 * sin(radO), C_BLACK);
  tft.drawLine(CX, CY, CX + 48 * cos(radN), CY + 48 * sin(radN), C_RED);
  tft.fillCircle(CX, CY, 4, C_WHITE);
  vit_old = vitesse;

  char buf[8];
  snprintf(buf, sizeof(buf), "%3d ", (int)vitesse);
  tft.setTextSize(2); tft.setTextColor(C_GREEN, C_BLACK);
  tft.setCursor(CX - 20, CY + 38);
  tft.print(buf);
}

// ============================================================
//  PAGE 2 — COMPAS
// ============================================================
void dessinerFondCompas() {
  tft.drawCircle(CX, CY, R_EXT,     C_WHITE);
  tft.drawCircle(CX, CY, R_EXT - 1, C_DKGRAY);

  for (int a = 0; a < 360; a += 10) {
    int len = (a % 30 == 0) ? 10 : 5;
    drawTick(CX, CY, a, R_EXT - len - 1, R_EXT - 1, C_WHITE);
  }
  const char* cards[]  = { "N", "E", "S", "W" };
  int cardAngles[]     = { 0, 90, 180, 270 };
  for (int i = 0; i < 4; i++) {
    float rad = (cardAngles[i] - 90.0) * PI / 180.0;
    tft.setTextSize(1);
    tft.setTextColor((i == 0) ? C_RED : C_WHITE, C_BLACK);
    tft.setCursor(CX + (R_EXT - 16) * cos(rad) - 3,
                  CY + (R_EXT - 16) * sin(rad) - 4);
    tft.print(cards[i]);
  }
  tft.fillTriangle(CX - 5, CY - R_EXT + 12, CX + 5, CY - R_EXT + 12,
                   CX,     CY - R_EXT + 2,   C_YELLOW);
}

static float cap_old = 0;
void majCompas() {
  float radO = (cap_old - 90.0) * PI / 180.0;
  float radN = (cap     - 90.0) * PI / 180.0;
  tft.drawLine(CX, CY, CX + 45 * cos(radO), CY + 45 * sin(radO), C_BLACK);
  tft.drawLine(CX, CY, CX - 20 * cos(radO), CY - 20 * sin(radO), C_BLACK);
  tft.drawLine(CX, CY, CX + 45 * cos(radN), CY + 45 * sin(radN), C_WHITE);
  tft.drawLine(CX, CY, CX - 20 * cos(radN), CY - 20 * sin(radN), C_RED);
  tft.fillCircle(CX, CY, 4, C_WHITE);
  cap_old = cap;

  char buf[5];
  snprintf(buf, sizeof(buf), "%03d", (int)cap);
  tft.setTextSize(2); tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(CX - 18, CY + 30);
  tft.print(buf);
}

// ============================================================
//  PAGE 3 — ALTIMÈTRE TFT
// ============================================================
void dessinerFondAltimetreTFT() {
  tft.drawCircle(CX, CY, R_EXT,     C_WHITE);
  tft.drawCircle(CX, CY, R_EXT - 1, C_DKGRAY);

  for (int i = 0; i < 10; i++) {
    int len = (i % 5 == 0) ? 10 : 6;
    drawTick(CX, CY, i * 36.0, R_EXT - len - 1, R_EXT - 1, C_WHITE);
    if (i % 5 == 0) {
      float rad = (i * 36.0 - 90.0) * PI / 180.0;
      tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BLACK);
      char buf[3]; itoa((i == 0) ? 10 : i, buf, 10);
      tft.setCursor(CX + (R_EXT - 20) * cos(rad) - 3,
                    CY + (R_EXT - 20) * sin(rad) - 4);
      tft.print(buf);
    }
  }
  printCentered("ALT",     CX, CY - 15, 1, C_CYAN);
  printCentered("x100 FT", CX, 8,       1, C_LTGRAY);
}

static float altOldAngle = 0;
void majAltimetreTFT() {
  int   altMod   = (int)altitude % 1000;
  float newAngle = (float)altMod / 1000.0 * 360.0;

  float radO = (altOldAngle - 90.0) * PI / 180.0;
  float radN = (newAngle    - 90.0) * PI / 180.0;
  tft.drawLine(CX, CY, CX + 48 * cos(radO), CY + 48 * sin(radO), C_BLACK);
  tft.drawLine(CX, CY, CX + 48 * cos(radN), CY + 48 * sin(radN), C_WHITE);
  tft.fillCircle(CX, CY, 5, C_WHITE);
  altOldAngle = newAngle;

  char buf[8];
  snprintf(buf, sizeof(buf), "%5d", (int)altitude);
  tft.setTextSize(2); tft.setTextColor(C_GREEN, C_BLACK);
  tft.setCursor(CX - 30, CY + 28);
  tft.print(buf);
}

// ============================================================
//  PAGE 4 — VARIOMÈTRE
// ============================================================
float vsToAngle(float vs) {
  vs = constrain(vs, -2000, 2000);
  return 270.0 - (vs / 2000.0) * 120.0;
}

void dessinerFondVario() {
  tft.drawCircle(CX, CY, R_EXT,     C_WHITE);
  tft.drawCircle(CX, CY, R_EXT - 1, C_DKGRAY);

  float labels[] = { -2000, -1500, -1000, -500, 0, 500, 1000, 1500, 2000 };
  for (int i = 0; i < 9; i++) {
    float a   = vsToAngle(labels[i]);
    int   len = (((int)labels[i]) % 1000 == 0) ? 10 : 5;
    drawTick(CX, CY, a, R_EXT - len - 1, R_EXT - 1, C_WHITE);
    if (((int)labels[i]) % 1000 == 0) {
      float rad = (a - 90.0) * PI / 180.0;
      tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BLACK);
      char buf[5];
      snprintf(buf, sizeof(buf), "%d", (int)(labels[i] / 1000));
      tft.setCursor(CX + (R_EXT - 20) * cos(rad) - 3,
                    CY + (R_EXT - 20) * sin(rad) - 4);
      tft.print(buf);
    }
  }
  printCentered("VSI",     CX, CY - 18, 1, C_CYAN);
  printCentered("x100FPM", CX, 8,       1, C_LTGRAY);

  for (float v = 0;     v <= 2000; v += 50) {
    float rad = (vsToAngle(v) - 90.0) * PI / 180.0;
    tft.drawPixel(CX + (R_EXT - 3) * cos(rad), CY + (R_EXT - 3) * sin(rad), C_GREEN);
  }
  for (float v = -2000; v <= 0;    v += 50) {
    float rad = (vsToAngle(v) - 90.0) * PI / 180.0;
    tft.drawPixel(CX + (R_EXT - 3) * cos(rad), CY + (R_EXT - 3) * sin(rad), C_RED);
  }
}

static float vsOldAngle = 270;
void majVario() {
  float newAngle = vsToAngle(vitesseVerticale);
  float radO = (vsOldAngle - 90.0) * PI / 180.0;
  float radN = (newAngle   - 90.0) * PI / 180.0;
  tft.drawLine(CX, CY, CX + 48 * cos(radO), CY + 48 * sin(radO), C_BLACK);
  tft.drawLine(CX, CY, CX + 48 * cos(radN), CY + 48 * sin(radN), C_WHITE);
  tft.fillCircle(CX, CY, 4, C_WHITE);
  vsOldAngle = newAngle;

  char buf[8];
  snprintf(buf, sizeof(buf), "%+5d", (int)vitesseVerticale);
  tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(CX - 18, CY + 28);
  tft.print(buf);
}

// ============================================================
//  PAGE 5 — INDICATEUR DE VIRAGE
// ============================================================
void dessinerFondVirage() {
  tft.drawCircle(CX, CY, R_EXT, C_WHITE);
  drawTick(CX, CY - 2, 340, R_EXT - 12, R_EXT - 1, C_YELLOW);
  drawTick(CX, CY - 2, 20,  R_EXT - 12, R_EXT - 1, C_YELLOW);
  tft.drawFastHLine(CX - R_EXT + 5, CY + 20, 2 * (R_EXT - 5), C_WHITE);
  tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(10,  CY - 4); tft.print("L");
  tft.setCursor(145, CY - 4); tft.print("R");
  printCentered("TURN",  CX, 10,  1, C_CYAN);
  printCentered("DEG/S", CX, 110, 1, C_LTGRAY);
}

static float turnOld = 0;
void majVirage() {
  float radO = turnOld    * PI / 180.0;
  float radN = tauxVirage * PI / 180.0;

  // Efface ancienne silhouette
  tft.drawLine(CX - 28*cos(radO), CY - 28*sin(radO),
               CX + 28*cos(radO), CY + 28*sin(radO), C_BLACK);
  tft.drawLine(CX -  5*cos(radO + PI/2), CY -  5*sin(radO + PI/2),
               CX + 10*cos(radO + PI/2), CY + 10*sin(radO + PI/2), C_BLACK);

  // Dessine nouvelle silhouette
  tft.drawLine(CX - 28*cos(radN), CY - 28*sin(radN),
               CX + 28*cos(radN), CY + 28*sin(radN), C_YELLOW);
  tft.drawLine(CX -  5*cos(radN + PI/2), CY -  5*sin(radN + PI/2),
               CX + 10*cos(radN + PI/2), CY + 10*sin(radN + PI/2), C_YELLOW);
  tft.fillCircle(CX, CY, 3, C_WHITE);
  turnOld = tauxVirage;

  char buf[8];
  snprintf(buf, sizeof(buf), "%+3d", (int)tauxVirage);
  tft.setTextSize(2); tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(CX - 18, CY + 30);
  tft.print(buf);
}
