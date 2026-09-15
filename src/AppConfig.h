#pragma once
#include <stddef.h>
#include <stdint.h>

// Version du firmware, injectée par scripts/version.py (git describe)
#ifndef MESHCACHING_VERSION
#define MESHCACHING_VERSION "dev"
#endif

// =====================================================================
// Configuration de l'application, identique pour toutes les cartes.
// Ce qui dépend du matériel vit dans src/hal/boards/.
// =====================================================================
namespace config {

// Durée de l'écran de démarrage (MESHCACHING + version)
constexpr uint32_t kSplashMs = 3000;

// --- Paramètres radio : preset MeshCore Île-de-France ---
constexpr float kLoraFreqMhz = 869.618f;
constexpr float kLoraBwKhz = 62.5f;
constexpr uint8_t kLoraSf = 8;
constexpr uint8_t kLoraCr = 8;
// La puissance TX (défaut et maxi) est propre à chaque carte : cf. Board.

// Preset MeshCore US (à activer à la place du bloc ci-dessus) /
// MeshCore US preset (uncomment and use instead of the block above):
// constexpr float kLoraFreqMhz = 910.525f;
// constexpr float kLoraBwKhz = 62.5f;
// constexpr uint8_t kLoraSf = 7;
// constexpr uint8_t kLoraCr = 5;

// Préfixe de la clé publique du répéteur MeshCore visé — valeur d'usine
// au premier démarrage, modifiable ensuite via le menu (persisté).
constexpr uint8_t kTargetPubkeyPrefix[] = { 0x4F, 0x3D };

// On n'accepte une réponse TRACE que dans les 10 s suivant notre ping
constexpr uint32_t kTraceReplyTimeoutMs = 10000;

// Délai minimal entre deux émissions TRACE
constexpr uint32_t kTxCooldownMs = 5000;

// Durée d'affichage du témoin d'émission "TX"
constexpr uint32_t kTxIndicatorMs = 700;

// LBT (écoute avant émission, CAD du SX126x) : canal occupé -> nouvel
// essai après un slot court aléatoire, abandon à la deadline — pas de
// TX forcé, contrairement à MeshCore (même deadline qu'eux).
constexpr uint32_t kLbtDeadlineMs = 4000;
constexpr uint32_t kLbtSlotMinMs = 100;
constexpr uint32_t kLbtSlotMaxMs = 300;
// Durée d'affichage du témoin "OCCUPÉ" après un abandon LBT
constexpr uint32_t kLbtBusyMsgMs = 2000;

// Bruit de fond : cadence d'échantillonnage du RSSI instantané (la
// médiane par cycle de 64 est dans NoiseFloor)
constexpr uint32_t kNoiseSampleIntervalMs = 20;

// Clignotement quand une réponse valide vient de rafraîchir le RSSI :
// une seule inversion de l'écran, de cette durée.
constexpr uint32_t kRxFlashMs = 150;

// Cadence de rafraîchissement de l'écran principal (animations, barre)
constexpr uint32_t kDisplayRefreshMs = 100;

// Timeout d'écran (cartes qui peuvent couper Vext, p. ex. Heltec V4) :
// un seul échéancier — appui bouton = 60 s depuis maintenant ; paquet
// cible = au moins 5 s (n'abrège jamais une fenêtre bouton plus longue).
constexpr uint32_t kDisplayTimeoutButtonMs = 60000;
constexpr uint32_t kDisplayTimeoutPacketMs = 5000;

// Après le timeout d'écran : arrêt profond 10 min après le dernier
// appui bouton (pas pendant LBT/TX/menu). Réveil = Reset matériel.
// measured consumption after sleep: 750uA
constexpr uint32_t kDeepSleepIdleMs = 600000;

// LED blanche (cartes qui en ont une) : témoin de vie, même écran off.
constexpr uint32_t kActivityLedOnMs = 20;
constexpr uint32_t kActivityLedOffMs = 2000;

}  // namespace config
