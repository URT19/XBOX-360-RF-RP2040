#pragma once
#include <Arduino.h>

// ==================== PINAGEM ====================
static constexpr uint8_t X360_RF_SYNC_PIN  = 2;
static constexpr uint8_t X360_RF_DATA_PIN  = 3;
static constexpr uint8_t X360_RF_CLOCK_PIN = 4;

// Tap USB passivo: D+ no GPIO10, D- no GPIO11.
static constexpr uint8_t X360_USB_DP_PIN = 10;
static constexpr uint8_t X360_USB_DM_PIN = 11;

// ==================== BOOT / ESTADO NORMAL ====================
// Deixa o 0x085 executar a animacao de boot completa antes do 0x084.
// Depois disso, o estado P1..P4 volta exatamente para a logica V6.
static constexpr uint32_t X360_POST_ORIGINAL_BOOT_GUARD_MS = 7000;
static constexpr uint32_t X360_PLAYER_MASK_STABLE_MS = 40;

// ==================== BOTAO SYNC ====================
// IRQ + polling redundante: o RP2040 detecta internamente toda pressao.
static constexpr uint32_t X360_SYNC_IRQ_DEBOUNCE_US = 12000;
static constexpr uint32_t X360_SYNC_POLL_STABLE_MS  = 12;
static constexpr uint32_t X360_SYNC_REARM_HIGH_MS   = 25;

// ==================== SESSAO DE SINCRONISMO ====================
static constexpr uint32_t X360_SYNC_TIMEOUT_MS = 15000;

// Xbox 360: alvo de ~100 ms entre posicoes do giro.
// O firmware usa deadline absoluto em microssegundos para que o tempo gasto
// transmitindo DATA/CLOCK nao seja somado ao periodo visual.
static constexpr uint32_t X360_SYNC_ROTATE_STEP_US = 100000;

// Ordem fisica horaria: P1 (sup.esq) -> P2 (sup.dir) -> P4 (inf.dir) -> P3 (inf.esq).
static constexpr uint8_t X360_SYNC_ROTATE_MASKS[4] = {0x01, 0x02, 0x08, 0x04};

// Confirmacao: todos 3x; depois TODOS os Players conectados 4x e ficam fixos.
static constexpr uint8_t  X360_CONFIRM_ALL_FLASH_COUNT    = 3;
static constexpr uint8_t  X360_CONFIRM_PLAYER_FLASH_COUNT = 4;
static constexpr uint32_t X360_CONFIRM_ALL_ON_MS          = 300;
static constexpr uint32_t X360_CONFIRM_ALL_OFF_MS         = 300;
static constexpr uint32_t X360_CONFIRM_PLAYER_ON_MS       = 300;
static constexpr uint32_t X360_CONFIRM_PLAYER_OFF_MS      = 300;
static constexpr uint32_t X360_CONFIRM_STAGE_GAP_MS       = 20;

// Se o evento de conexao chegar antes da mascara V6 consolidar, mantemos o
// Player detectado visualmente por um curto periodo; a V6 continua sendo a
// autoridade do estado final.
static constexpr uint32_t X360_DETECTED_HOLD_MS = 1800;

#define X360_RF_DEBUG 0
