#include "x360_usb_monitor.h"
#include "x360_rf_config.h"
#include <USBSnifferPIO_RP2040.h>
#include <USBProtocol.h>
#include <pico/stdlib.h>

namespace {
USBSnifferPIO g_sniffer;
volatile bool g_ready = false;

// ==================== ESTADO V6 (preservado) ====================
// V29: estado visual SOLIDO orientado por eventos reais. Nenhum timeout de
// silencio pode desligar/religar Players em uso normal.
// low byte: playerMask; byte1 explicit; byte2 data; byte3 host assign.
volatile uint32_t g_masksWord = 0;
volatile uint32_t g_generation = 0;
volatile uint32_t g_slotPackets[4] = {0,0,0,0};

struct Transaction {
  bool valid;
  uint8_t pid;
  uint8_t addr;
  uint8_t ep;
  uint32_t ts;
};
Transaction g_txn{false,0,0,0,0};

uint8_t g_explicitMask = 0;
uint8_t g_dataMask = 0;
uint8_t g_hostAssignMask = 0;

// ==================== LATCH DE SESSAO SYNC ====================
// Este bloco NAO participa do calculo playerMask. Ele so fornece um evento
// instantaneo ao Core0 quando a propria semantica V6 identifica o Player.
volatile bool g_syncWatchActive = false;
volatile uint8_t g_syncBaselineMask = 0;
volatile uint8_t g_syncDisconnectedMask = 0;
volatile uint8_t g_syncPlayerMailbox = 0;

static int8_t slotFromEndpoint(uint8_t ep) {
  // Xbox Wireless Receiver: endpoints 1,3,5,7 = P1..P4.
  switch (ep) {
    case 1: return 0;
    case 3: return 1;
    case 5: return 2;
    case 7: return 3;
    default: return -1;
  }
}

static uint8_t bitForSlot(uint8_t slot) {
  return slot < 4 ? static_cast<uint8_t>(1u << slot) : 0u;
}

static void latchSyncPlayer(uint8_t bit) {
  bit &= 0x0F;
  if (!bit || !__atomic_load_n(&g_syncWatchActive, __ATOMIC_ACQUIRE)) return;
  uint8_t expected = 0;
  (void)__atomic_compare_exchange_n(&g_syncPlayerMailbox, &expected, bit, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void publish() {
  // EXATAMENTE a regra V6: explicit OR data OR host assignment.
  const uint8_t playerMask = static_cast<uint8_t>(g_explicitMask | g_dataMask | g_hostAssignMask);
  const uint32_t word = static_cast<uint32_t>(playerMask) |
                        (static_cast<uint32_t>(g_explicitMask) << 8) |
                        (static_cast<uint32_t>(g_dataMask) << 16) |
                        (static_cast<uint32_t>(g_hostAssignMask) << 24);
  __atomic_store_n(&g_masksWord, word, __ATOMIC_RELEASE);
  __atomic_add_fetch(&g_generation, 1u, __ATOMIC_ACQ_REL);
}

static void setExplicit(uint8_t slot, bool connected) {
  const uint8_t bit = bitForSlot(slot);
  const uint8_t before = g_explicitMask;

  if (connected) {
    g_explicitMask |= bit;

    // Novo slot: 08 80/08 C0 confirma imediatamente.
    // Slot que ja existia: so confirma se caiu durante esta sessao. Isso evita
    // um 08 80 de polling antigo encerrar o giro assim que o botao e pressionado.
    if (__atomic_load_n(&g_syncWatchActive, __ATOMIC_ACQUIRE)) {
      const uint8_t baseline = __atomic_load_n(&g_syncBaselineMask, __ATOMIC_ACQUIRE);
      const uint8_t disconnected = __atomic_load_n(&g_syncDisconnectedMask, __ATOMIC_ACQUIRE);
      if (!(baseline & bit) || (disconnected & bit)) latchSyncPlayer(bit);
    }
  } else {
    const bool syncActive = __atomic_load_n(&g_syncWatchActive, __ATOMIC_ACQUIRE);
    const uint8_t baseline = __atomic_load_n(&g_syncBaselineMask, __ATOMIC_ACQUIRE);

    // V29: nenhum Player ja existente e removido visualmente enquanto uma nova
    // sessao de SYNC esta em andamento. Alguns receivers produzem um 08 00
    // transitorio ao entrar em binding/adicionar outro controle. Esse evento
    // continua sendo registrado para a logica de rebind, mas NAO derruba P1..P4.
    if (syncActive && (baseline & bit)) {
      __atomic_fetch_or(&g_syncDisconnectedMask, bit, __ATOMIC_ACQ_REL);
      return;
    }

    // Fora de SYNC, somente o evento REAL 08 00 pode remover o Player.
    // Nao existe mais watchdog/timeout por silencio: isso era a origem do
    // pisca lento (OFF inferido -> report posterior ON -> OFF...).
    g_explicitMask &= static_cast<uint8_t>(~bit);
    g_dataMask &= static_cast<uint8_t>(~bit);
    g_hostAssignMask &= static_cast<uint8_t>(~bit);

    if (syncActive) {
      __atomic_fetch_or(&g_syncDisconnectedMask, bit, __ATOMIC_ACQ_REL);
    }
  }

  if (g_explicitMask != before || !connected) publish();
}

static void confirmByData(uint8_t slot) {
  const uint8_t bit = bitForSlot(slot);
  __atomic_add_fetch(&g_slotPackets[slot], 1u, __ATOMIC_RELAXED);

  const bool wasPresent = (g_dataMask & bit) != 0;
  if (!wasPresent) {
    g_dataMask |= bit;
    publish();
  }

  if (!__atomic_load_n(&g_syncWatchActive, __ATOMIC_ACQUIRE)) return;
  const uint8_t baseline = __atomic_load_n(&g_syncBaselineMask, __ATOMIC_ACQUIRE);
  const uint8_t disconnected = __atomic_load_n(&g_syncDisconnectedMask, __ATOMIC_ACQUIRE);

  // Mesma logica V6: o primeiro report real de um slot que nao fazia parte do
  // baseline confirma o Player. Para um Player antigo, exigimos queda/retorno.
  if (!(baseline & bit) || (disconnected & bit)) latchSyncPlayer(bit);
}

static void confirmHostAssign(uint8_t slot) {
  const uint8_t bit = bitForSlot(slot);
  if ((g_hostAssignMask & bit) == 0) {
    g_hostAssignMask |= bit;
    publish();
  }
}

static void processIn(uint8_t ep, const USBPacket& pkt) {
  const int8_t slot = slotFromEndpoint(ep);
  if (slot < 0 || pkt.data_length < 2) return;
  const uint8_t s = static_cast<uint8_t>(slot);

  // V6/XBOXRECV: 08 80=controller, 08 40=headset, 08 C0=ambos, 08 00=off.
  if (pkt.data[0] == 0x08) {
    const uint8_t status = pkt.data[1];
    setExplicit(s, (status & 0x80u) != 0);
    return;
  }

  // V6: report real do pad, readBuf[1] == 0x01.
  if (pkt.data_length >= 18 && pkt.data[1] == 0x01) {
    confirmByData(s);
  }
}

static void processOut(uint8_t ep, const USBPacket& pkt) {
  const int8_t slot = slotFromEndpoint(ep);
  if (slot < 0 || pkt.data_length < 4) return;

  // Mantem a mascara V6 exatamente baseada no endpoint.
  if (pkt.data[0] == 0x00 && pkt.data[1] == 0x00 && pkt.data[2] == 0x08 &&
      pkt.data[3] >= 0x42 && pkt.data[3] <= 0x45) {
    const uint8_t s = static_cast<uint8_t>(slot);
    confirmHostAssign(s);

    // Para encerrar o giro usamos o comando 42..45 SOMENTE quando ele concorda
    // com o endpoint V6. Assim um token mal associado pelo sniffer nao transforma
    // P1 em P2/P3/P4.
    const uint8_t cmdSlot = static_cast<uint8_t>(pkt.data[3] - 0x42u);
    if (cmdSlot == s) latchSyncPlayer(bitForSlot(s));
  }
}

static void onUsbPacket(const USBPacket& pkt) {
  if (!pkt.crc_valid) return;

  if (pkt.pid == USBPID::IN || pkt.pid == USBPID::OUT) {
    g_txn.valid = true;
    g_txn.pid = pkt.pid;
    g_txn.addr = pkt.addr;
    g_txn.ep = pkt.endp;
    g_txn.ts = pkt.timestamp_us;
    return;
  }

  if (pkt.pid == USBPID::SETUP || pkt.pid == USBPID::SOF ||
      pkt.pid == USBPID::NAK || pkt.pid == USBPID::STALL) {
    g_txn.valid = false;
    return;
  }

  if (pkt.pid != USBPID::DATA0 && pkt.pid != USBPID::DATA1) return;
  if (!g_txn.valid) return;

  const uint8_t tokenPid = g_txn.pid;
  const uint8_t ep = g_txn.ep;
  const uint32_t age = pkt.timestamp_us - g_txn.ts;
  g_txn.valid = false;

  if (age > 1500u) return;

  if (tokenPid == USBPID::IN) processIn(ep, pkt);
  else if (tokenPid == USBPID::OUT) processOut(ep, pkt);
}
}

void x360UsbMonitorSetupCore1() {
  __atomic_store_n(&g_masksWord, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_generation, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncWatchActive, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncBaselineMask, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncDisconnectedMask, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncPlayerMailbox, 0u, __ATOMIC_RELEASE);
  for (uint8_t i = 0; i < 4; ++i) __atomic_store_n(&g_slotPackets[i], 0u, __ATOMIC_RELEASE);

  g_ready = g_sniffer.begin(X360_USB_DP_PIN, USBSpeed::FULL_SPEED, 1);
  if (g_ready) g_sniffer.onPacket(onUsbPacket);
}

void x360UsbMonitorLoopCore1() {
  if (g_ready) g_sniffer.task();
  else tight_loop_contents();
}

X360UsbSnapshot x360UsbMonitorSnapshot() {
  X360UsbSnapshot out{};
  const uint32_t word = __atomic_load_n(&g_masksWord, __ATOMIC_ACQUIRE);
  out.playerMask = static_cast<uint8_t>(word & 0xFFu) & 0x0Fu;
  out.explicitMask = static_cast<uint8_t>((word >> 8) & 0x0Fu);
  out.dataMask = static_cast<uint8_t>((word >> 16) & 0x0Fu);
  out.hostLedAssignMask = static_cast<uint8_t>((word >> 24) & 0x0Fu);
  out.generation = __atomic_load_n(&g_generation, __ATOMIC_ACQUIRE);
  out.snifferReady = g_ready;
  for (uint8_t i = 0; i < 4; ++i) out.slotPackets[i] = __atomic_load_n(&g_slotPackets[i], __ATOMIC_ACQUIRE);
  if (g_ready) {
    const USBPacketDecoder& d = g_sniffer.decoder();
    out.packetsDecoded = d.packets_decoded;
    out.crcErrors = d.crc_errors;
  }
  return out;
}

void x360UsbMonitorBeginSyncWatch(uint8_t baselineMask) {
  __atomic_store_n(&g_syncBaselineMask, static_cast<uint8_t>(baselineMask & 0x0F), __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncDisconnectedMask, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncPlayerMailbox, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_syncWatchActive, true, __ATOMIC_RELEASE);
}

void x360UsbMonitorEndSyncWatch() {
  __atomic_store_n(&g_syncWatchActive, false, __ATOMIC_RELEASE);
}

uint8_t x360UsbMonitorConsumeSyncPlayer() {
  return __atomic_exchange_n(&g_syncPlayerMailbox, 0u, __ATOMIC_ACQ_REL) & 0x0Fu;
}
