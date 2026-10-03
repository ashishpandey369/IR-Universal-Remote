#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <EEPROM.h>

/* ===== Pins (ESP32 Dev) ===== */
#define IR_RECV_PIN     32
#define IR_SEND_PIN     23
#define BUTTON_LEARN    17
#define BUTTON_BLAST    5
#define LED_RED         14
#define LED_GREEN       19

/* ===== LCD ===== */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ===== EEPROM ===== */
#define EEPROM_SIZE     512
#define EEPROM_MAGIC    0x42
#define MAX_SAVED       15

/* ===== IR ===== */
IRrecv irrecv(IR_RECV_PIN);
IRsend irsend(IR_SEND_PIN);
decode_results results;

/* ===== State ===== */
uint8_t  savedCount = 0;
uint32_t savedCodes[MAX_SAVED];

bool learnArmed = false;
unsigned long learnPressMs = 0, blastPressMs = 0;
bool learnPressedLatch = false, blastPressedLatch = false;

unsigned long greenLedBlinkMs = 0;
bool greenLedState = false;

/* Noise filtering / double-confirm */
uint32_t lastSeenCode = 0;
uint32_t lastSeenTime = 0;
uint8_t  sameCount = 0;
const uint32_t CONFIRM_WINDOW_MS = 600;

/* ===== Preloads ===== */
const uint32_t PRELOADS[] = {
  0x02FD48B7, 0x02FD807F, 0x02FD00FF, 0x02FD58A7, 0x02FDA05F,
  0x02FD40BF, 0x02FD609F, 0x02FD708F, 0x02FD50AF, 0x02FD10EF,
  0x20DF10EF, 0x20DF23DC
};
const uint8_t PRELOAD_COUNT = sizeof(PRELOADS) / sizeof(PRELOADS[0]);

/* ===== Auto-home (new) ===== */
const uint32_t HOME_TIMEOUT_MS = 2000;
unsigned long lastUserActionMs = 0;
bool atHome = false;
bool blastingActive = false;

/* ===== Utility ===== */
void showLCD(const String &l1, const String &l2 = "") {
  lcd.clear(); lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
  // any screen update counts as UI activity
  lastUserActionMs = millis();
  atHome = false;
}

void showHome() {
  showLCD("Ready", String("Saved IR: ")+String(savedCount));
  atHome = true;
}

void maybeReturnHome() {
  if (!learnArmed && !blastingActive) {
    if (!atHome && (millis() - lastUserActionMs >= HOME_TIMEOUT_MS)) {
      showHome();
    }
  }
}

void loadEEP() {
  if (EEPROM.read(0) != EEPROM_MAGIC) {
    EEPROM.write(0, EEPROM_MAGIC);
    EEPROM.write(1, 0);
    EEPROM.commit();
    savedCount = 0;
    return;
  }
  savedCount = EEPROM.read(1);
  if (savedCount > MAX_SAVED) savedCount = 0;

  int addr = 2;
  for (uint8_t i=0;i<savedCount;i++){
    EEPROM.get(addr, savedCodes[i]); 
    addr += sizeof(uint32_t);
  }
}

void saveEEP() {
  EEPROM.write(0, EEPROM_MAGIC);
  EEPROM.write(1, savedCount);
  int addr = 2;
  for (uint8_t i=0;i<savedCount;i++){
    EEPROM.put(addr, savedCodes[i]); 
    addr += sizeof(uint32_t);
  }
  EEPROM.commit();
}

void clearSaved(){
  savedCount = 0;
  saveEEP();
}

bool isDuplicate(uint32_t code) {
  for (uint8_t i=0;i<savedCount;i++)
    if (savedCodes[i] == code) return true;
  return false;
}

/* ===== Browse (press-to-advance) ===== */
void browseSavedStepByStep() {
  static uint8_t index = 0;

  if (savedCount == 0) {
    showLCD("Saved: 0", "No IR stored");
    delay(700);
    return;
  }

  if (index >= savedCount) index = 0;

  String hex = "0x" + String(savedCodes[index], HEX);
  hex.toUpperCase();

  showLCD(String("Saved IR #") + String(index+1), hex);

  index++;    
  delay(300);
}

/* ===== Labeled sender (shows PRELOADED / SAVED) ===== */
void sendNEC32_labeled(uint32_t code, const char* label) {
  String hex = "0x" + String(code, HEX);
  hex.toUpperCase();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Sending "); lcd.print(label);
  lcd.setCursor(0, 1); lcd.print(hex);
  lastUserActionMs = millis(); // UI activity

  digitalWrite(LED_RED, HIGH);
  irsend.sendNEC(code, 32);
  digitalWrite(LED_RED, LOW);

  delay(160);
}

/* ===== Startup Animation (25% longer) ===== */
void startupSequence() {
  showLCD("System Booting...", "");

  digitalWrite(LED_RED, HIGH); delay(250);
  digitalWrite(LED_RED, LOW);  delay(250);

  digitalWrite(LED_GREEN, HIGH); delay(250);
  digitalWrite(LED_GREEN, LOW);  delay(250);

  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  delay(320);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  showLCD("Getting Ready...", "");
  delay(1125);
}

/* ===== Setup ===== */
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_LEARN, INPUT_PULLUP);
  pinMode(BUTTON_BLAST, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  EEPROM.begin(EEPROM_SIZE);
  loadEEP();

  irrecv.setUnknownThreshold(50);
  irrecv.enableIRIn();
  irsend.begin();

  startupSequence();

  showHome();
  lastUserActionMs = millis();
}

/* ===== Loop ===== */
void loop() {
  /* ---- Learn Button Handling ---- */
  bool lNow = (digitalRead(BUTTON_LEARN)==LOW);

  if (lNow && !learnPressedLatch){
    learnPressedLatch = true;
    learnPressMs = millis();
    lastUserActionMs = learnPressMs;
  }

  if (!lNow && learnPressedLatch){
    unsigned long held = millis()-learnPressMs;
    learnPressedLatch = false;
    lastUserActionMs = millis();

    if (held < 500) {
      browseSavedStepByStep();
    }
  }

  if (lNow && (millis()-learnPressMs >= 2000) && !learnArmed){
    learnArmed = true;
    sameCount = 0;
    lastSeenCode = 0;
    showLCD("Learning ON", "Press remote...");
    greenLedBlinkMs = millis();
    greenLedState = false;
  }

  /* ---- Green Heartbeat in Learn Mode ---- */
  if (learnArmed){
    if (millis()-greenLedBlinkMs > 300){
      greenLedBlinkMs = millis();
      greenLedState = !greenLedState;
      digitalWrite(LED_GREEN, greenLedState);
    }
  } else {
    digitalWrite(LED_GREEN, LOW);
  }

  /* ---- Capture Logic With Anti-Noise + Duplicate Check ---- */
  if (learnArmed && irrecv.decode(&results)) {

    bool ok = (results.decode_type == NEC && results.bits == 32 && !results.repeat);

    if (ok){
      uint32_t code = results.value;
      uint32_t now = millis();

      if (code == lastSeenCode && (now - lastSeenTime) <= CONFIRM_WINDOW_MS){
        sameCount++;
      } else {
        sameCount = 1;
        lastSeenCode = code;
      }

      lastSeenTime = now;

      if (sameCount >= 2){
        learnArmed = false;
        digitalWrite(LED_GREEN, LOW);

        String hex = "0x" + String(code, HEX);
        hex.toUpperCase();

        if (isDuplicate(code)) {
          showLCD("Already Saved", hex);
        } else if (savedCount < MAX_SAVED) {
          savedCodes[savedCount++] = code;
          saveEEP();
          showLCD("IR DETECTED!", hex);
          delay(900);
          showLCD(String("Saved #")+String(savedCount), hex);
        } else {
          showLCD("Save Failed", "Slots Full");
        }

        irrecv.resume();
        delay(900);
        showHome();
      } else {
        irrecv.resume();
      }
    } else {
      irrecv.resume();
    }
  }

  /* ---- BLAST button ---- */
  bool bNow = (digitalRead(BUTTON_BLAST)==LOW);

  if (bNow && !blastPressedLatch){
    blastPressedLatch = true;
    blastPressMs = millis();
    lastUserActionMs = blastPressMs;
  }

  if (!bNow && blastPressedLatch){
    unsigned long held = millis()-blastPressMs;
    blastPressedLatch = false;
    lastUserActionMs = millis();

    if (held >= 2000){
      clearSaved();
      showLCD("Saved cleared", "Preloads kept");
      delay(900);
      showHome();
    } else {
      if (PRELOAD_COUNT==0 && savedCount==0){
        showLCD("Nothing to send", "Learn first");
        delay(900);
        showHome();
      } else {
        blastingActive = true;
        // preloads first
        for (uint8_t i=0;i<PRELOAD_COUNT;i++)
          sendNEC32_labeled(PRELOADS[i], "PRELOADED");
        // then saved
        for (uint8_t i=0;i<savedCount;i++)
          sendNEC32_labeled(savedCodes[i], "SAVED");
        blastingActive = false;

        showLCD("Done", String("Sent ")+String(PRELOAD_COUNT+savedCount));
        // leave it on screen; auto-return will bring us home in 2s if no buttons
        // (but give a small grace so users can read)
        delay(200);
      }
    }
  }

  // NEW: auto-return to home after 2s of no activity (when safe)
  maybeReturnHome();
}
