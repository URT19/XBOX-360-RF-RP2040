#include "ring_logic.h"
#include "rf_bus.h"
#include "sync_button.h"
#include "x360_usb_monitor.h"
#include "x360_rf_config.h"
#include <Arduino.h>

namespace {
enum class Mode : uint8_t {
  BOOT_GUARD,
  NORMAL,
  SYNC_ROTATE,
  HANDOFF_ALL,
  FLASH_ALL,
  GAP_TO_PLAYER,
  FLASH_PLAYER
};

enum class FlashPhase : uint8_t { NEED_ON, HOLD_ON, NEED_OFF, HOLD_OFF };

Mode g_mode = Mode::BOOT_GUARD;
uint32_t g_modeSinceMs = 0;

uint8_t g_appliedMask = 0xFF;
uint8_t g_seenMask = 0;
uint8_t g_pendingMask = 0;
uint32_t g_maskSinceMs = 0;

uint8_t g_syncBaselineMask = 0;
uint8_t g_confirmPlayer = 0;
uint8_t g_confirmMask = 0;
uint8_t g_pendingSyncPlayer = 0;
uint8_t g_holdMask = 0;
uint32_t g_holdUntilMs = 0;

uint8_t g_rotateIndex = 0;
uint32_t g_nextRotateUs = 0;
bool g_rotationHasFrame = false;

// Handoff fluido V25: mede quanto um frame SET_GREEN realmente consome no
// DATA/CLOCK e usa essa duracao para iniciar a transicao perto da borda
// natural do proximo passo do giro. Assim o quadrante atual nao fica
// visualmente "congelado" alem dos ~100 ms normais.
uint32_t g_rotateTxEstimateUs = 0;
uint32_t g_lastRotateDoneUs = 0;
uint32_t g_handoffTargetUs = 0;
bool g_handoffScheduled = false;

uint8_t g_flashCount = 0;
FlashPhase g_flashPhase = FlashPhase::NEED_ON;
uint32_t g_flashAtMs = 0;

static uint8_t firstBit(uint8_t mask) {
  if (mask & 0x01) return 0x01;
  if (mask & 0x02) return 0x02;
  if (mask & 0x04) return 0x04;
  if (mask & 0x08) return 0x08;
  return 0;
}

static bool hasExactlyOneBit(uint8_t x) {
  x &= 0x0F;
  return x && ((x & static_cast<uint8_t>(x - 1u)) == 0);
}

static bool applyMask(uint8_t mask, bool force = false) {
  mask &= 0x0F;
  if (!force && mask == g_appliedMask) return true;
  if (!X360RfBus::setGreenMask(mask)) return false;
  g_appliedMask = mask;
  return true;
}

static uint8_t effectiveNormalMask(uint8_t liveMask, uint32_t nowMs) {
  liveMask &= 0x0F;
  if (g_holdMask && static_cast<int32_t>(nowMs - g_holdUntilMs) >= 0) g_holdMask = 0;
  return static_cast<uint8_t>((liveMask | g_holdMask) & 0x0F);
}

static void enterNormal(uint8_t liveMask, bool force) {
  x360UsbMonitorEndSyncWatch();
  const uint32_t now = millis();
  const uint8_t mask = effectiveNormalMask(liveMask, now);
  g_mode = Mode::NORMAL;
  g_modeSinceMs = now;
  g_seenMask = mask;
  g_pendingMask = mask;
  g_maskSinceMs = now;
  (void)applyMask(mask, force);
}

static uint8_t resolvePlayerAgainstV6(uint8_t proposed, uint8_t liveMask) {
  proposed = firstBit(proposed);
  liveMask &= 0x0F;

  // A mascara V6 e a autoridade final. Se surgiu exatamente um Player novo,
  // ele vence qualquer indicacao lateral do evento.
  const uint8_t newBits = static_cast<uint8_t>(liveMask & ~g_syncBaselineMask) & 0x0F;
  if (hasExactlyOneBit(newBits)) return newBits;

  // Se a V6 ja contem o Player proposto, ele esta coerente.
  if (proposed && (liveMask & proposed)) return proposed;

  // Ressincronizacao do mesmo Player pode nao alterar a mascara V6; nesse caso
  // mantemos o Player confirmado pelo evento 08/host-assign coerente.
  return proposed;
}

static void enterFlashAllAlreadyOn(uint8_t resolved) {
  g_confirmPlayer = resolved;
  g_holdMask = resolved;
  g_holdUntilMs = millis() + X360_DETECTED_HOLD_MS;
  g_mode = Mode::FLASH_ALL;
  g_modeSinceMs = millis();
  g_flashCount = 0;
  g_flashPhase = FlashPhase::HOLD_ON;
  g_flashAtMs = g_modeSinceMs;
  g_handoffScheduled = false;
}

static bool resolvePendingPlayer(uint8_t liveMask, uint8_t& resolved) {
  resolved = resolvePlayerAgainstV6(g_pendingSyncPlayer, liveMask);
  return resolved != 0;
}

static uint32_t handoffLeadUs() {
  // Usa a medicao real do ultimo frame de giro. Limites apenas evitam que um
  // outlier de timeout desloque a transicao inteira. Se ainda nao houve frame,
  // nao antecipamos: a confirmacao comeca imediatamente.
  uint32_t lead = g_rotateTxEstimateUs;
  if (lead < 1000u) return 0u;
  if (lead > 40000u) lead = 40000u;
  return lead;
}

static bool timeReachedUs(uint32_t nowUs, uint32_t deadlineUs) {
  return static_cast<int32_t>(nowUs - deadlineUs) >= 0;
}

static void scheduleFluidHandoff() {
  if (g_handoffScheduled) return;
  const uint32_t nowUs = micros();

  // A borda VISUAL natural e calculada a partir do instante em que o ultimo
  // quadro realmente terminou de ser transmitido, nao do inicio da transmissao.
  // Portanto o quadrante permanece visivel por ~100 ms completos. A previsao
  // da duracao do proximo SET_GREEN e usada apenas para iniciar a transmissao
  // cedo o bastante para o 0x00 terminar nessa borda visual.
  g_handoffTargetUs = g_rotationHasFrame
      ? (g_lastRotateDoneUs + X360_SYNC_ROTATE_STEP_US)
      : nowUs;
  g_handoffScheduled = true;
}

static bool serviceFluidHandoff(uint8_t liveMask) {
  uint8_t resolved = 0;
  if (!resolvePendingPlayer(liveMask, resolved)) return false;

  scheduleFluidHandoff();

  const uint32_t nowUs = micros();
  const uint32_t lead = handoffLeadUs();
  const uint32_t startUs = g_handoffTargetUs - lead;
  if (!timeReachedUs(nowUs, startUs)) return false;

  // TRANSICAO NEUTRA V25:
  // 1) remove o quadrante residual com 0x00;
  // 2) imediatamente assume 0x0F.
  // Se 0x0F encontrar o barramento ocupado, permanecemos apagados em vez de
  // deixar um P1/P2/P3/P4 aleatorio parado.
  if (!applyMask(0x00, true)) return false;

  if (!applyMask(0x0F, true)) {
    g_confirmPlayer = resolved;
    g_mode = Mode::HANDOFF_ALL;
    g_modeSinceMs = millis();
    g_handoffScheduled = false;
    return true;
  }

  enterFlashAllAlreadyOn(resolved);
  return true;
}

static void startSyncSession(const X360UsbSnapshot& usb) {
  const uint8_t baseline = usb.playerMask & 0x0F;
  g_syncBaselineMask = baseline;
  g_confirmPlayer = 0;
  g_confirmMask = 0;
  g_pendingSyncPlayer = 0;
  g_handoffTargetUs = 0;
  g_handoffScheduled = false;
  g_holdMask = 0;
  g_holdUntilMs = 0;

  // O RP2040 sabe internamente que uma sessao de sincronismo comecou assim que
  // o botao foi reconhecido. O watch e armado ANTES do comando para nao perder
  // um controle que conclua o link durante DATA/CLOCK.
  x360UsbMonitorBeginSyncWatch(baseline);

  // Usa literalmente o frame/timing do INO original/V6 que funcionou no RF.
  // O retorno nao decide se a animacao manual deve existir: o clique sempre
  // cria uma sessao visual deterministica.
  (void)X360RfBus::startSyncOriginal();

  g_mode = Mode::SYNC_ROTATE;
  g_modeSinceMs = millis();
  g_rotateIndex = 0;
  g_nextRotateUs = micros();
  g_rotationHasFrame = false;
  g_lastRotateDoneUs = 0;
}

static void serviceRotation(const X360UsbSnapshot& usb) {
  // PRIORIDADE ABSOLUTA: detectou -> agenda o handoff para TODOS. O Player fica
  // latched ate o 0x0F entrar com sucesso, portanto o evento nao pode se perder.
  uint8_t player = x360UsbMonitorConsumeSyncPlayer();

  // Fallback 100% V6: se a mascara consolidada ganhou exatamente um bit,
  // tambem e uma deteccao valida e imediata.
  if (!player) {
    const uint8_t newBits = static_cast<uint8_t>((usb.playerMask & 0x0F) & ~g_syncBaselineMask);
    if (hasExactlyOneBit(newBits)) player = newBits;
  }

  if (player && !g_pendingSyncPlayer) {
    g_pendingSyncPlayer = firstBit(player);
  }

  if (g_pendingSyncPlayer) {
    // Detectou: NAO mostra mais nenhum novo quadrante. O quadro atual termina
    // somente o seu periodo normal e a V25 faz a troca neutra 0x00 -> 0x0F.
    // Isso elimina o "LED aleatorio parado" entre o giro e a confirmacao.
    if (serviceFluidHandoff(usb.playerMask & 0x0F)) {
      g_pendingSyncPlayer = 0;
      return;
    }
    return;
  }

  if (millis() - g_modeSinceMs >= X360_SYNC_TIMEOUT_MS) {
    enterNormal(usb.playerMask & 0x0F, true);
    return;
  }

  const uint32_t nowUs = micros();
  if (static_cast<int32_t>(nowUs - g_nextRotateUs) < 0) return;

  const uint8_t mask = X360_SYNC_ROTATE_MASKS[g_rotateIndex & 0x03u];
  const uint32_t txStartUs = micros();
  if (!applyMask(mask, true)) {
    // Falhou: NAO avanca a animacao. Tenta o mesmo quadro no proximo loop.
    return;
  }
  const uint32_t txDoneUs = micros();
  const uint32_t txUs = txDoneUs - txStartUs;
  if (txUs > 0u && txUs < 100000u) {
    // Filtro IIR simples: reage a mudancas reais do clock sem deixar um unico
    // frame anormal dominar a previsao.
    if (g_rotateTxEstimateUs == 0u) g_rotateTxEstimateUs = txUs;
    else g_rotateTxEstimateUs = static_cast<uint32_t>((g_rotateTxEstimateUs * 3u + txUs) / 4u);
  }

  g_rotationHasFrame = true;
  g_lastRotateDoneUs = txDoneUs;
  g_rotateIndex = static_cast<uint8_t>((g_rotateIndex + 1u) & 0x03u);

  // Deadline absoluto: o tempo gasto transmitindo nao deixa o giro mais lento.
  g_nextRotateUs += X360_SYNC_ROTATE_STEP_US;
  const uint32_t afterUs = micros();
  if (static_cast<int32_t>(afterUs - g_nextRotateUs) >= static_cast<int32_t>(X360_SYNC_ROTATE_STEP_US)) {
    // Se houve um atraso anormal grande, evita rajada de varios quadros atrasados.
    g_nextRotateUs = afterUs + X360_SYNC_ROTATE_STEP_US;
  }
}

static void serviceHandoffAll() {
  // O quadrante ja foi neutralizado para 0x00. Enquanto o RF nao aceitar
  // 0x0F, permanecemos apagados; nunca voltamos a um quadrante aleatorio.
  if (!g_confirmPlayer) {
    enterNormal(x360UsbMonitorSnapshot().playerMask & 0x0F, true);
    return;
  }
  if (applyMask(0x0F, true)) enterFlashAllAlreadyOn(g_confirmPlayer);
}

static void serviceFlashAll(uint32_t nowMs) {
  switch (g_flashPhase) {
    case FlashPhase::NEED_ON:
      if (applyMask(0x0F, true)) {
        g_flashAtMs = millis();
        g_flashPhase = FlashPhase::HOLD_ON;
      }
      return;

    case FlashPhase::HOLD_ON:
      if (nowMs - g_flashAtMs >= X360_CONFIRM_ALL_ON_MS) g_flashPhase = FlashPhase::NEED_OFF;
      return;

    case FlashPhase::NEED_OFF:
      if (applyMask(0x00, true)) {
        g_flashAtMs = millis();
        g_flashPhase = FlashPhase::HOLD_OFF;
      }
      return;

    case FlashPhase::HOLD_OFF:
      if (nowMs - g_flashAtMs < X360_CONFIRM_ALL_OFF_MS) return;
      ++g_flashCount;
      if (g_flashCount >= X360_CONFIRM_ALL_FLASH_COUNT) {
        g_mode = Mode::GAP_TO_PLAYER;
        g_modeSinceMs = nowMs;
      } else {
        g_flashPhase = FlashPhase::NEED_ON;
      }
      return;
  }
}

static void startPlayerFlash() {
  // Revalida o Player novo contra a mascara V6 e monta a mascara de
  // confirmacao com TODOS os controles atualmente conectados.
  // Ex.: P1+P2 -> os dois piscam juntos 4x e depois ficam os dois fixos.
  const uint8_t live = x360UsbMonitorSnapshot().playerMask & 0x0F;
  g_confirmPlayer = resolvePlayerAgainstV6(g_confirmPlayer, live);
  if (!g_confirmPlayer) {
    enterNormal(live, true);
    return;
  }

  g_confirmMask = static_cast<uint8_t>((live | g_confirmPlayer) & 0x0F);
  if (!g_confirmMask) {
    enterNormal(live, true);
    return;
  }

  g_mode = Mode::FLASH_PLAYER;
  g_modeSinceMs = millis();
  g_flashCount = 0; // conta pulsos ON realmente mostrados
  g_flashPhase = FlashPhase::NEED_ON;
  g_flashAtMs = 0;
}

static void serviceFlashPlayer(uint32_t nowMs) {
  switch (g_flashPhase) {
    case FlashPhase::NEED_ON:
      if (applyMask(g_confirmMask, true)) {
        ++g_flashCount;
        g_flashAtMs = millis();
        g_flashPhase = FlashPhase::HOLD_ON;
      }
      return;

    case FlashPhase::HOLD_ON:
      if (nowMs - g_flashAtMs < X360_CONFIRM_PLAYER_ON_MS) return;

      // A quarta acendida vira diretamente o estado fixo. Assim o olho ve
      // exatamente 4 pulsos, e nao 4 pulsos + uma quinta acendida de fixacao.
      if (g_flashCount >= X360_CONFIRM_PLAYER_FLASH_COUNT) {
        const uint8_t live = x360UsbMonitorSnapshot().playerMask & 0x0F;
        g_holdMask = g_confirmMask;
        g_holdUntilMs = millis() + X360_DETECTED_HOLD_MS;
        enterNormal(static_cast<uint8_t>(live | g_confirmMask), false);
      } else {
        g_flashPhase = FlashPhase::NEED_OFF;
      }
      return;

    case FlashPhase::NEED_OFF:
      if (applyMask(0x00, true)) {
        g_flashAtMs = millis();
        g_flashPhase = FlashPhase::HOLD_OFF;
      }
      return;

    case FlashPhase::HOLD_OFF:
      if (nowMs - g_flashAtMs >= X360_CONFIRM_PLAYER_OFF_MS) {
        g_flashPhase = FlashPhase::NEED_ON;
      }
      return;
  }
}
}

void x360RingBegin() {
  g_mode = Mode::BOOT_GUARD;
  g_modeSinceMs = millis();
  g_appliedMask = 0xFF;
  g_seenMask = 0;
  g_pendingMask = 0;
  g_maskSinceMs = g_modeSinceMs;
  g_syncBaselineMask = 0;
  g_confirmPlayer = 0;
  g_confirmMask = 0;
  g_pendingSyncPlayer = 0;
  g_handoffTargetUs = 0;
  g_handoffScheduled = false;
  g_holdMask = 0;
  g_holdUntilMs = 0;
  g_rotateIndex = 0;
  g_nextRotateUs = 0;
  g_rotationHasFrame = false;
  g_rotateTxEstimateUs = 0;
  g_lastRotateDoneUs = 0;
  x360SyncButtonBegin();
}

void x360RingService() {
  const uint32_t nowMs = millis();
  const X360UsbSnapshot usb = x360UsbMonitorSnapshot();
  const uint8_t rawMask = usb.playerMask & 0x0F;

  // Toda pressao valida reinicia uma sessao, mesmo durante outra animacao.
  if (x360SyncButtonConsumePress()) {
    startSyncSession(usb);
    return;
  }

  switch (g_mode) {
    case Mode::BOOT_GUARD:
      if (nowMs - g_modeSinceMs >= X360_POST_ORIGINAL_BOOT_GUARD_MS) {
        // So agora rearma o controlador de LEDs. Isso preserva o BOOTANIM
        // completo e, em seguida, restaura P1..P4 pela mesma mascara da V6.
        (void)X360RfBus::finishBootLedInitOriginal();
        enterNormal(rawMask, true);
      }
      return;

    case Mode::SYNC_ROTATE:
      serviceRotation(usb);
      return;

    case Mode::HANDOFF_ALL:
      serviceHandoffAll();
      return;

    case Mode::FLASH_ALL:
      serviceFlashAll(nowMs);
      return;

    case Mode::GAP_TO_PLAYER:
      if (nowMs - g_modeSinceMs >= X360_CONFIRM_STAGE_GAP_MS) startPlayerFlash();
      return;

    case Mode::FLASH_PLAYER:
      serviceFlashPlayer(nowMs);
      return;

    case Mode::NORMAL:
      break;
  }

  // Estado normal: exatamente a estrategia V6, com hold curto somente para
  // evitar apagar o Player entre o evento de sync e a convergencia da mascara.
  const uint8_t mask = effectiveNormalMask(rawMask, nowMs);
  if (mask != g_seenMask) {
    g_seenMask = mask;
    g_pendingMask = mask;
    g_maskSinceMs = nowMs;
  }

  if (g_pendingMask != g_appliedMask &&
      nowMs - g_maskSinceMs >= X360_PLAYER_MASK_STABLE_MS) {
    (void)applyMask(g_pendingMask, false);
  }
}
