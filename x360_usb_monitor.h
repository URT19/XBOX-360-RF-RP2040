#pragma once
#include <Arduino.h>

struct X360UsbSnapshot {
  uint8_t playerMask;          // V6: bit0=P1 ... bit3=P4
  uint8_t explicitMask;        // V6: report 08 xx
  uint8_t dataMask;            // V6: report de pad xx 01 ...
  uint8_t hostLedAssignMask;   // V6: host 42..45 no endpoint do slot
  uint32_t generation;
  bool snifferReady;
  uint32_t packetsDecoded;
  uint32_t crcErrors;
  uint32_t slotPackets[4];
};

void x360UsbMonitorSetupCore1();
void x360UsbMonitorLoopCore1();
X360UsbSnapshot x360UsbMonitorSnapshot();

// Canal lateral de sincronismo. NAO altera a mascara V6; apenas memoriza o
// primeiro Player confiavel visto durante a sessao iniciada pelo RP2040.
void x360UsbMonitorBeginSyncWatch(uint8_t baselineMask);
void x360UsbMonitorEndSyncWatch();
uint8_t x360UsbMonitorConsumeSyncPlayer();
