#include "rf_bus.h"
#include "x360_rf_config.h"
#include <pico/stdlib.h>
#include <hardware/gpio.h>

namespace X360RfBus {

// Copia deliberadamente a mecanica do sendData() do INO ORIGINAL que o usuario
// confirmou funcionar no modulo fisico. Nao otimizar sem teste em hardware.
static bool sendDataOriginal(const int cmd_do[], int bits = 10) {
  pinMode(X360_RF_DATA_PIN, OUTPUT);
  digitalWrite(X360_RF_DATA_PIN, LOW);

  int prev = 1;
  unsigned long timeout = millis();

  for (int i = 0; i < bits; i++) {
    while (prev == digitalRead(X360_RF_CLOCK_PIN) && millis() - timeout < 100) {
      tight_loop_contents();
    }

    if (millis() - timeout >= 100) {
      digitalWrite(X360_RF_DATA_PIN, HIGH);
      pinMode(X360_RF_DATA_PIN, INPUT);
      gpio_set_function(X360_RF_DATA_PIN, GPIO_FUNC_SIO);
      gpio_pull_up(X360_RF_DATA_PIN);
      return false;
    }

    prev = digitalRead(X360_RF_CLOCK_PIN);
    digitalWrite(X360_RF_DATA_PIN, cmd_do[i]);

    timeout = millis();
    while (prev == digitalRead(X360_RF_CLOCK_PIN) && millis() - timeout < 100) {
      tight_loop_contents();
    }

    if (millis() - timeout >= 100) {
      // O original saia aqui sem restaurar a linha. Corrigimos SOMENTE a
      // recuperacao de erro; o timing de sucesso permanece identico.
      digitalWrite(X360_RF_DATA_PIN, HIGH);
      pinMode(X360_RF_DATA_PIN, INPUT);
      gpio_set_function(X360_RF_DATA_PIN, GPIO_FUNC_SIO);
      gpio_pull_up(X360_RF_DATA_PIN);
      return false;
    }

    prev = digitalRead(X360_RF_CLOCK_PIN);
  }

  digitalWrite(X360_RF_DATA_PIN, HIGH);
  pinMode(X360_RF_DATA_PIN, INPUT);
  gpio_set_function(X360_RF_DATA_PIN, GPIO_FUNC_SIO);
  gpio_set_input_enabled(X360_RF_DATA_PIN, true);
  gpio_pull_up(X360_RF_DATA_PIN);
  return true;
}

static void initPinExactlyLikeOriginal(uint8_t pin) {
  gpio_init(pin);
  gpio_set_function(pin, GPIO_FUNC_SIO);
  gpio_set_dir(pin, GPIO_IN);
  gpio_set_input_enabled(pin, true);

  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, 1);
  sleep_ms(1);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);
}

void beginPinsOriginal() {
  initPinExactlyLikeOriginal(X360_RF_SYNC_PIN);
  initPinExactlyLikeOriginal(X360_RF_DATA_PIN);
  initPinExactlyLikeOriginal(X360_RF_CLOCK_PIN);
}

bool startBootAnimationOriginal() {
  // Exatamente os dois primeiros comandos do INO original/V6.
  // O 0x084 NAO e enviado aqui para nao cortar a animacao 0x085.
  static const int start_cmd[10] = {0,0,0,0,0,1,0,0,1,0}; // 0x012
  static const int power_cmd[10] = {0,0,1,0,0,0,0,1,0,1}; // 0x085 BOOTANIM

  const bool okStart = sendDataOriginal(start_cmd, 10);
  delay(100);
  const bool okPower = sendDataOriginal(power_cmd, 10);
  delay(100);
  return okStart && okPower;
}

bool finishBootLedInitOriginal() {
  // Mesmo 0x084/retry da V6, mas somente DEPOIS da animacao de boot terminar.
  static const int led_init[10] = {0,0,1,0,0,0,0,1,0,0}; // 0x084 LED_INIT

  bool okLed = false;
  for (int retry = 0; retry < 3; retry++) {
    if (sendDataOriginal(led_init, 10)) {
      okLed = true;
      break;
    }
    delay(500);
  }
  delay(50);
  return okLed;
}

bool initializeModuleOriginal() {
  // Mantem a API e o comportamento historico da V6 para qualquer outro uso.
  const bool okBoot = startBootAnimationOriginal();
  const bool okLed = finishBootLedInitOriginal();
  return okBoot && okLed;
}

bool startSyncOriginal() {
  // LITERALMENTE o sync_cmd[11] do INO original que funciona no RF do usuario.
  // Os dez primeiros ciclos carregam 0x004; o ultimo HIGH e preservado porque
  // faz parte do comportamento fisicamente validado desse codigo/modulo.
  static const int sync_cmd[11] = {0,0,0,0,0,0,0,1,0,0,1};
  return sendDataOriginal(sync_cmd, 11);
}

bool sendCommand10(uint16_t command) {
  command &= 0x03FFu;
  int bits[10];
  for (int i = 0; i < 10; ++i) {
    bits[i] = (command >> (9 - i)) & 1u;
  }
  return sendDataOriginal(bits, 10);
}

bool setGreenMask(uint8_t mask) {
  return sendCommand10(static_cast<uint16_t>(0x0A0u | (mask & 0x0Fu)));
}

} // namespace X360RfBus
