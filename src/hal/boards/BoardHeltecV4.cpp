#if defined(BOARD_HELTEC_V4_2) || defined(BOARD_HELTEC_V4_3) || \
    defined(BOARD_HELTEC_V4_R8)
// =====================================================================
// Heltec WiFi LoRa 32 V4 — trois déclinaisons partagent cette carte :
//  - BOARD_HELTEC_V4_2 : révision ≤ 4.2 (ESP32-S3R2 + FEM GC1109) ;
//  - BOARD_HELTEC_V4_3 : révision 4.3 (ESP32-S3R2 + FEM KCT8103L) ;
//  - BOARD_HELTEC_V4_R8 : série "R8" (ESP32-S3R8, 8 Mo PSRAM), vendue
//    ensuite, qui ne diffère de la 4.3 que par la broche de son rail
//    Vext (GPIO40 au lieu de GPIO36).
//
// Commun : SX1262 (brochage LoRa et OLED identique au V3), OLED 128x64
// piloté en SSD1306, un seul bouton utilisateur (PRG), et un FEM entre
// le SX1262 et l'antenne (~12 dB en émission). Valeurs reprises du
// firmware MeshCore (variants/heltec_v4{,_r8}).
//
// Différence FEM : le GC1109 (V4 ≤ 4.2) se pilote via CSD (GPIO2) + CPS
// (GPIO46) ; le KCT8103L (V4.3 / R8) via CSD (GPIO2) + CTX (GPIO5), avec
// LNA RX débrayable. Le type est vérifié au démarrage (niveau de repos
// de CSD) : un binaire flashé sur la mauvaise révision s'arrête sans
// émettre. Les déclinaisons TFT et e-ink ne sont pas gérées.
// =====================================================================
#include <U8g2lib.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "../Board.h"
#include "../U8g2Display.h"

namespace {

constexpr uint8_t kPinLoraNss = 8;
constexpr uint8_t kPinLoraSck = 9;
constexpr uint8_t kPinLoraMosi = 10;
constexpr uint8_t kPinLoraMiso = 11;
constexpr uint8_t kPinLoraReset = 12;
constexpr uint8_t kPinLoraBusy = 13;
constexpr uint8_t kPinLoraDio1 = 14;

// FEM : LDO d'alimentation commun ; ensuite GC1109 (EN + CPS) ou
// KCT8103L (CSD + CTX). Gain PA : MeshCore documente 10 dBm demandés au
// SX1262 pour 22 dBm mesurés à l'antenne.
constexpr uint8_t kPinFemLdo = 7;
constexpr uint8_t kPinFemCsd = 2;       // GC1109 EN / KCT8103L CSD
constexpr uint8_t kPinFemGc1109Cps = 46;  // HIGH = full PA
constexpr uint8_t kPinFemKctCtx = 5;   // HIGH = TX / LNA bypass
constexpr int8_t kFemTxGainDb = 12;

constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
constexpr uint8_t kPinOledReset = 21;
constexpr uint8_t kPinButtonPrg = 0;  // relié à la masse quand pressé
constexpr uint8_t kPinLed = 35;      // LED blanche (actif haut)
// Connecteur GNSS : enable actif bas — on le force OFF au démarrage et
// avant deep sleep (MeshCaching n'utilise pas le GPS).
constexpr uint8_t kPinGpsEn = 34;
constexpr uint8_t kGpsEnOffLevel = HIGH;

// Vext (alim de l'OLED) : actif à l'état BAS sur toute la série — le
// rail est commuté par un MOSFET canal P (schéma officiel HTIT-WB32LAF
// V4.3, transistor Q2 AO3401A), comme sur le V3. Ne pas se fier au
// PIN_VEXT_EN_ACTIVE=HIGH de la variante heltec_v4 de MeshCore : leur
// écran est construit sans référence au rail, ce niveau n'est jamais
// appliqué (et leur variante R8, plus récente, dit bien LOW).
#ifdef BOARD_HELTEC_V4_R8
constexpr char kBoardName[] = "Heltec WiFi LoRa 32 V4 R8";
constexpr uint8_t kPinVext = 40;
#elif defined(BOARD_HELTEC_V4_2)
constexpr char kBoardName[] = "Heltec WiFi LoRa 32 V4.2";
constexpr uint8_t kPinVext = 36;
#else
constexpr char kBoardName[] = "Heltec WiFi LoRa 32 V4.3";
constexpr uint8_t kPinVext = 36;
#endif
constexpr uint8_t kVextOnLevel = LOW;

#ifdef BOARD_HELTEC_V4_2
constexpr bool kExpectGc1109 = true;
#else
constexpr bool kExpectGc1109 = false;
#endif

const ButtonSpec kButtons[] = {
    {Key::Ok, kPinButtonPrg, /*activeLow=*/true, /*internalPullup=*/true},
};

class HeltecV4Board : public Board {
public:
  const char *name() const override { return kBoardName; }

  void initPower() override {
    pinMode(kPinVext, OUTPUT);
    digitalWrite(kPinVext, kVextOnLevel);  // allume le rail Vext (OLED)

    pinMode(kPinLed, OUTPUT);
    digitalWrite(kPinLed, LOW);
    pinMode(kPinGpsEn, OUTPUT);
    digitalWrite(kPinGpsEn, kGpsEnOffLevel);

    // Alimente le FEM puis identifie sa référence par le niveau de repos
    // de CSD (astuce reprise de MeshCore) : pull-up interne sur le
    // KCT8103L (V4.3 et R8) -> HIGH, pull-down sur le GC1109 (V4 <= 4.2)
    // -> LOW. Le binaire ne configure que le FEM qu'il attend.
    pinMode(kPinFemLdo, OUTPUT);
    digitalWrite(kPinFemLdo, HIGH);
    delay(1);  // temps de démarrage du FEM
    pinMode(kPinFemCsd, INPUT);
    delay(1);
    const bool isGc1109 = (digitalRead(kPinFemCsd) == LOW);

    if (isGc1109 != kExpectGc1109) {
      digitalWrite(kPinFemLdo, LOW);
      _selfCheckError =
          kExpectGc1109 ? "FEM KCT8103L (V4.3+)" : "FEM GC1109 (V4<=4.2)";
    } else if (isGc1109) {
      // GC1109 : EN haut = actif ; CPS haut = PA plein (TX), bas en RX
      // (CTX aiguillé par DIO2 du SX1262). Pas de LNA logiciel.
      pinMode(kPinFemCsd, OUTPUT);
      digitalWrite(kPinFemCsd, HIGH);
      pinMode(kPinFemGc1109Cps, OUTPUT);
      digitalWrite(kPinFemGc1109Cps, LOW);
    } else {
      // KCT8103L : CSD haut = actif ; CTX haut au départ = PA dans le
      // chemin d'émission, LNA contourné en réception. L'aiguillage RX
      // est ensuite géré par radioRxMode() selon setFemLna(). Attention
      // si le LNA est activé : son gain s'ajoute au RSSI mesuré par le
      // SX1262, précisément la donnée que cet appareil affiche.
      pinMode(kPinFemCsd, OUTPUT);
      digitalWrite(kPinFemCsd, HIGH);
      pinMode(kPinFemKctCtx, OUTPUT);
      digitalWrite(kPinFemKctCtx, HIGH);
    }

    delay(150);  // stabilisation du Vext avant l'init de l'OLED
  }

  const char *selfCheckError() const override { return _selfCheckError; }

  void radioTxMode() override {
    if (kExpectGc1109) {
      digitalWrite(kPinFemGc1109Cps, HIGH);
    } else {
      digitalWrite(kPinFemKctCtx, HIGH);
    }
  }

  void radioRxMode() override {
    if (kExpectGc1109) {
      digitalWrite(kPinFemGc1109Cps, LOW);
    } else {
      digitalWrite(kPinFemKctCtx, _femLnaEnabled ? LOW : HIGH);
    }
  }

  bool hasFemLna() const override { return !kExpectGc1109; }

  void setFemLna(bool enabled) override {
    if (!kExpectGc1109) {
      _femLnaEnabled = enabled;
    }
  }

  Display &display() override { return _display; }

  void beginDisplay() override {
    _display.begin();
    _u8g2.setContrast(255);
    _displayPowered = true;
  }

  bool canPowerDisplay() const override { return true; }

  bool isDisplayPowered() const override { return _displayPowered; }

  // Pas de coupure Vext : on efface seulement la trame (pixels éteints)
  // pour limiter le burn-in. Rallumer = redessiner côté App.
  void setDisplayPowered(bool on) override {
    if (on == _displayPowered) {
      return;
    }
    if (!on) {
      _u8g2.clearBuffer();
      _u8g2.sendBuffer();
    }
    _displayPowered = on;
  }

  bool hasActivityLed() const override { return true; }

  void setActivityLed(bool on) override { digitalWrite(kPinLed, on ? HIGH : LOW); }

  bool canDeepSleep() const override { return true; }

  void enterDeepSleep() override {
    // LED forcée OFF et verrouillée : sans hold, la broche peut flotter
    // en deep sleep et laisser la LED allumée (fuite batterie).
    digitalWrite(kPinLed, LOW);
    gpio_hold_en((gpio_num_t)kPinLed);
    gpio_deep_sleep_hold_en();

    // Blank puis coupe Vext / FEM / GPS — réveil uniquement par Reset.
    _u8g2.clearBuffer();
    _u8g2.sendBuffer();
    digitalWrite(kPinVext, kVextOnLevel == LOW ? HIGH : LOW);
    _displayPowered = false;
    pinMode(kPinFemCsd, OUTPUT);
    digitalWrite(kPinFemCsd, LOW);
    if (kExpectGc1109) {
      pinMode(kPinFemGc1109Cps, OUTPUT);
      digitalWrite(kPinFemGc1109Cps, LOW);
    } else {
      pinMode(kPinFemKctCtx, OUTPUT);
      digitalWrite(kPinFemKctCtx, LOW);
    }
    digitalWrite(kPinFemLdo, LOW);
    digitalWrite(kPinGpsEn, kGpsEnOffLevel);
    esp_deep_sleep_start();
  }

  RadioTraits radio() const override {
    RadioTraits t;
    t.pins.nss = kPinLoraNss;
    t.pins.dio1 = kPinLoraDio1;
    t.pins.reset = kPinLoraReset;
    t.pins.busy = kPinLoraBusy;
    t.pins.sck = kPinLoraSck;
    t.pins.miso = kPinLoraMiso;
    t.pins.mosi = kPinLoraMosi;
    t.dio2AsRfSwitch = true;
    t.tcxoVoltage = 1.8f;
    t.currentLimitmA = 140;
    t.femTxGainDb = kFemTxGainDb;
    t.femRxPatch = true;
    return t;
  }

  // Plafond « à l'antenne » : même politique que la 4.3 (gain FEM ~12 dB
  // déjà retranché avant consigne SX1262).
  int8_t txPowerMaxDbm() const override { return 20; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  const char *_selfCheckError = nullptr;
  bool _femLnaEnabled = false;
  bool _displayPowered = false;
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, kPinOledReset,
                                            kPinOledScl, kPinOledSda};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static HeltecV4Board instance;
  return instance;
}
#endif  // BOARD_HELTEC_V4_2 || BOARD_HELTEC_V4_3 || BOARD_HELTEC_V4_R8
