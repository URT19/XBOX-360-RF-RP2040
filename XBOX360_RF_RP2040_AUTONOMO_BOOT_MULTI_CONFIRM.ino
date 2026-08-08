/*
  XBOX 360 RF + RP2040 - AUTONOMO V23 / BOOT + MULTI CONFIRM
  ====================================================

  Arquitetura desta versao:

  - BOOT e estado P1..P4: base V6 comprovada no hardware.
  - Botao SYNC: detectado internamente pelo RP2040 via IRQ + polling.
  - Comando de pairing: frame/timing original/V6.
  - Giro: controlado pelo RP2040 em ~100 ms/quadrante.
  - D+/D-: parser V6 continua sendo a autoridade de P1..P4.
  - Assim que a V6 identifica o Player durante a sessao:
        corta o giro -> TODOS 3x -> Players conectados 4x -> fixa pela mascara V6.

  Pinos:
    GPIO2 = SYNC
    GPIO3 = DATA
    GPIO4 = CLOCK
    GPIO10 = D+ passivo
    GPIO11 = D- passivo

  Dependencia:
    USBSnifferPIO_RP2040 1.0.0
*/

#include <Arduino.h>
#include "x360_rf_config.h"
#include "rf_bus.h"
#include "sync_button.h"
#include "x360_usb_monitor.h"
#include "ring_logic.h"

#if !__has_include(<USBSnifferPIO_RP2040.h>)
#error "Instale USBSnifferPIO_RP2040 1.0.0 no Arduino IDE."
#endif

void setup() {
#if X360_RF_DEBUG
  Serial.begin(115200);
#endif

  X360RfBus::beginPinsOriginal();
  delay(2500);

  // Recupera a animacao de boot: 0x012 -> 0x085 agora; o 0x084 so e
  // enviado pelo Ring depois que a animacao original terminar.
  (void)X360RfBus::startBootAnimationOriginal();

  x360RingBegin();
}

void loop() {
  x360RingService();
  // Menor que a V6 para reduzir a latencia Core1->Core0 sem afetar DATA/CLOCK.
  delay(1);
}

void setup1() {
  x360UsbMonitorSetupCore1();
}

void loop1() {
  x360UsbMonitorLoopCore1();
}
