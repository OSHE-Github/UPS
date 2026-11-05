/* SPDX-License-Identifier: GPL-3.0-or-later
 * Isolated low-voltage bench only; never connect to a live UPS power stage.
 * Uno: UPS_EMULATOR_PD=0 (BQ) or 1 (PD).
 * Mega2560: UPS_EMULATOR_DUAL=1. Address mask also ACKs six unused addresses.
 */
#include <Arduino.h>
#include "thermal.h"
#ifndef UPS_EMULATOR_PD
#define UPS_EMULATOR_PD 0
#endif
#ifndef UPS_EMULATOR_DUAL
#define UPS_EMULATOR_DUAL 0
#endif
#if UPS_EMULATOR_DUAL
#if !defined(__AVR_ATmega2560__)
#error "The combined profile requires an ATmega2560 Mega."
#endif
#include <util/twi.h>
#include <avr/interrupt.h>
#else
#include <Wire.h>
#endif

struct Device {
  volatile uint8_t selected;
  volatile uint16_t status1, status2, reads, unsupported, rejectedWrites;
};
static Device devices[2]; // 0=BQ, 1=PD: separate pointers and read-clear state.
static uint8_t consoleRole = UPS_EMULATOR_PD;
static bool thermalEnabled;
static ThermalState thermal;
static uint16_t adc;
static uint32_t nextSample;
static volatile uint16_t aliasHits, busErrors;

// Interrupt context: no printing, delay, or analog conversion.
static uint16_t readRegister(uint8_t role)
{
  Device &d = devices[role];
  ++d.reads;
  if (role) {
    switch (d.selected) {
      case 0x10: { uint16_t v = d.status1; d.status1 = 0; return v; }
      case 0x11: { uint16_t v = d.status2; d.status2 = 0; return v; }
      case 0x12: return 0x8000;
    }
  } else {
    switch (d.selected) {
      case 0x20: {
        uint16_t v = d.status1;
        d.status1 &= (uint16_t)~0x00e0;
        return v;
      }
      case 0x21: return d.status1 >> 8;
      case 0x2e: return 0xd540;
      case 0x2f: return 0x00d5;
    }
  }
  ++d.unsupported;
  return 0xffff;
}

#if UPS_EMULATOR_DUAL
static uint8_t active = 2, received;
static uint16_t replyWord;
static void twiContinue(bool ack)
{
  TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWIE) | (ack ? _BV(TWEA) : 0);
}
static uint8_t addressedRole(uint8_t address)
{
  if (address == 0x6b) return 0;
  if (address == 0x28) return 1;
  ++aliasHits;
  return 2;
}
ISR(TWI_vect)
{
  switch (TW_STATUS) {
    case TW_SR_SLA_ACK:
      active = addressedRole(TWDR >> 1);
      received = 0;
      twiContinue(active < 2);
      break;
    case TW_SR_DATA_ACK:
      if (active < 2) {
        if (!received) devices[active].selected = TWDR;
        else if (received == 1) ++devices[active].rejectedWrites;
      }
      if (received < 2) ++received;
      twiContinue(active < 2);
      break;
    case TW_SR_DATA_NACK:
    case TW_SR_STOP:
      twiContinue(true);
      break;
    case TW_ST_SLA_ACK:
      active = addressedRole(TWDR >> 1);
      replyWord = active < 2 ? readRegister(active) : 0xffff;
      TWDR = (uint8_t)replyWord;
      twiContinue(true);
      break;
    case TW_ST_DATA_ACK:
      TWDR = (uint8_t)(replyWord >> 8);
      twiContinue(false); // Last byte; unsupported burst reads are not modeled.
      break;
    case TW_ST_DATA_NACK:
    case TW_ST_LAST_DATA:
      twiContinue(true);
      break;
    default:
      ++busErrors;
      // Slave recovery releases SDA/SCL; no master STOP is generated.
      TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWIE) | _BV(TWEA) | _BV(TWSTO);
      break;
  }
}
#else
static void receiveCommand(int count)
{
  if (count < 1) return;
  devices[UPS_EMULATOR_PD].selected = (uint8_t)Wire.read();
  if (count > 1) ++devices[UPS_EMULATOR_PD].rejectedWrites;
  while (Wire.available()) (void)Wire.read();
}
static void sendRegister()
{
  const uint16_t value = readRegister(UPS_EMULATOR_PD);
  const uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8)};
  Wire.write(bytes, sizeof bytes);
}
#endif

static void scenario(char command)
{
  bool known = true;
  noInterrupts();
  Device &d = devices[consoleRole];
  if (consoleRole) {
    switch (command) {
      case 'a': d.status1 = 0x000a; d.status2 = 0x0400; break;
      case 'd': d.status1 = 0; d.status2 = 0; break;
      case 'm': d.status1 = 0x000a; d.status2 = 0x2400; break;
      case 'f': d.status1 = 0x0004; d.status2 = 0; break;
      default: known = false; break;
    }
  } else {
    switch (command) {
      case 'n': d.status1 = 0x8400; break;
      case 'b': d.status1 = 0; break;
      case 'f': d.status1 = 0x8040; break;
      default: known = false; break;
    }
  }
  interrupts();
  if (known) {
    Serial.print(consoleRole ? F("PD injected ") : F("BQ injected "));
    Serial.println(command);
  }
}

static void sampleThermistor()
{
  if (!(UPS_EMULATOR_DUAL || UPS_EMULATOR_PD)) return;
  const uint32_t now = millis();
  if ((int32_t)(now - nextSample) < 0) return;
  nextSample = now + 100;
  (void)analogRead(A0);
  uint32_t total = 0;
  for (uint8_t i = 0; i < 8; ++i) total += analogRead(A0);
  adc = (uint16_t)(total / 8);
  if (thermalEnabled && thermalUpdate(thermal, adc, now) == THERMAL_ENTER) {
    noInterrupts();
    // External NTC load shedding is bit 0, NOT internal ANA_OTP bit 2.
    devices[1].status1 |= 0x0001;
    interrupts();
    Serial.println(F("NTC: load-shedding event (model NTC_MODE=0)"));
  }
}

void setup()
{
  Serial.begin(115200);
  devices[0].status1 = 0x8400;
#if UPS_EMULATOR_DUAL
  // Ignoring address bits 6,1,0 ACKs 28..2B and 68..6B. Only 28 and 6B dispatch.
  // Do not put other devices on this bus and do not sleep (TWDR address needed).
  pinMode(20, INPUT); digitalWrite(20, LOW);
  pinMode(21, INPUT); digitalWrite(21, LOW);
  TWAR = 0x28 << 1;
  TWAMR = (0x28 ^ 0x6b) << 1;
  twiContinue(true);
  Serial.println(F("MEGA BOTH: B selects BQ; P selects PD; then scenario letter"));
  Serial.println(F("MASK ALSO ACKS 29 2A 2B 68 69 6A: isolated harness only"));
#else
  Wire.begin(UPS_EMULATOR_PD ? 0x28 : 0x6b);
  Wire.onReceive(receiveCommand);
  Wire.onRequest(sendRegister);
  Serial.println(UPS_EMULATOR_PD ? F("PD at 28") : F("BQ at 6B"));
#endif
  analogReference(DEFAULT); // Divider top and ADC reference share board 5 V.
  pinMode(A0, INPUT); digitalWrite(A0, LOW);
  Serial.println(F("BQ: n=charging b=no input f=BATOC; PD: a=attach d=zero m=mismatch f=internal OTP"));
  Serial.println(F("s=counters; PD: t=NTC reading e=toggle NTC model (starts OFF)"));
}

void loop()
{
  sampleThermistor();
  if (!Serial.available()) return;
  const char command = (char)Serial.read();
#if UPS_EMULATOR_DUAL
  if (command == 'B' || command == 'P') {
    consoleRole = command == 'P';
    Serial.println(consoleRole ? F("PD selected") : F("BQ selected"));
    return;
  }
#endif
  if (command == 's') {
    noInterrupts();
    const Device &d = devices[consoleRole];
    uint16_t r = d.reads, u = d.unsupported, w = d.rejectedWrites;
    uint16_t a = aliasHits, b = busErrors;
    interrupts();
    Serial.print(F("reads=")); Serial.print(r);
    Serial.print(F(" unsupported=")); Serial.print(u);
    Serial.print(F(" rejectedWrites=")); Serial.print(w);
    Serial.print(F(" aliases=")); Serial.print(a);
    Serial.print(F(" busErrors=")); Serial.println(b);
  } else if (consoleRole && command == 'e') {
    thermalEnabled = !thermalEnabled;
    thermal = ThermalState{};
    Serial.println(thermalEnabled ? F("NTC model ON") : F("NTC model OFF"));
  } else if (consoleRole && command == 't') {
    Serial.print(F("ADC=")); Serial.print(adc);
    Serial.print(F(" ratio_per_mille=")); Serial.print((uint32_t)adc * 1000 / 1023);
    Serial.print(F(" sense="));
    if (adc >= 1018) Serial.print(F("OPEN_OR_VERY_HIGH_R"));
    else if (adc <= 5) Serial.print(F("SHORT_OR_VERY_LOW_R"));
    else Serial.print(F("IN_RANGE"));
    Serial.print(F(" shed=")); Serial.print(thermal.shed);
    Serial.print(F(" enabled=")); Serial.println(thermalEnabled);
  } else {
    scenario(command);
  }
}
