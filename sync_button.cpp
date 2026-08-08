#include "sync_button.h"
#include "x360_rf_config.h"
#include <Arduino.h>

namespace {
volatile bool g_irqPending = false;
volatile uint32_t g_irqAtUs = 0;
uint32_t g_ignoreUntilMs = 0;
uint32_t g_lowSinceMs = 0;
uint32_t g_highSinceMs = 0;
bool g_latched = false;

void onSyncFalling() {
  // ISR minimo: nenhuma comunicacao RF aqui.
  g_irqAtUs = micros();
  g_irqPending = true;
}
}

void x360SyncButtonBegin() {
  g_irqPending = false;
  g_irqAtUs = 0;
  g_ignoreUntilMs = 0;
  g_lowSinceMs = 0;
  g_highSinceMs = millis();
  g_latched = false;

  // O pino ja foi configurado como INPUT_PULLUP pela inicializacao original.
  attachInterrupt(digitalPinToInterrupt(X360_RF_SYNC_PIN), onSyncFalling, FALLING);
}

bool x360SyncButtonConsumePress() {
  const uint32_t nowMs = millis();
  const bool low = (digitalRead(X360_RF_SYNC_PIN) == LOW);

  // Rearma somente depois de uma liberacao real e estavel.
  if (!low) {
    g_lowSinceMs = 0;
    if (g_highSinceMs == 0) g_highSinceMs = nowMs;
    if (g_latched && nowMs - g_highSinceMs >= X360_SYNC_REARM_HIGH_MS) {
      g_latched = false;
    }
  } else {
    g_highSinceMs = 0;
    if (g_lowSinceMs == 0) g_lowSinceMs = nowMs;
  }

  if (g_latched || nowMs < g_ignoreUntilMs) return false;

  // Caminho 1: IRQ. Mesmo um clique curto fica memorizado.
  if (g_irqPending) {
    const uint32_t irqUs = g_irqAtUs;
    if (static_cast<uint32_t>(micros() - irqUs) >= X360_SYNC_IRQ_DEBOUNCE_US) {
      g_irqPending = false;
      g_latched = true;
      g_ignoreUntilMs = nowMs + 120;
      return true;
    }
  }

  // Caminho 2: polling redundante. Se por qualquer motivo o IRQ nao disparar,
  // 12 ms em LOW sao suficientes para registrar o clique.
  if (low && g_lowSinceMs != 0 && nowMs - g_lowSinceMs >= X360_SYNC_POLL_STABLE_MS) {
    g_irqPending = false;
    g_latched = true;
    g_ignoreUntilMs = nowMs + 120;
    return true;
  }

  return false;
}
