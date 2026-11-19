// T-Beam v1.2 (AXP2101 + SX1276) 868 MHz FSK sniffer + single-shot TX.
// Edge mode (default): bit sync off, timestamp raw demod edges on DIO2 while RSSI > threshold,
//   per burst print pulse widths and a frequency-error estimate (fei).
// Bit-sync mode ("b <bps>"): sample DIO2 on DIO1 (DCLK) rising edge, dump bits.
// Serial commands: t <dBm>  threshold | f <MHz> frequency | w <kHz> RX bandwidth (250/200/166/125/100/62)
//                  b <bps>  bit-sync mode | e  edge mode | r  print RSSI now
//                  x <hex>  send ONE packet: 8-byte 0xAA preamble + <hex> raw chips at 32768 chip/s, then back to RX
//                  d <kHz>  TX deviation | p <dBm> TX power (2..14, PA_BOOST)
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <XPowersLib.h>

#define PIN_SCK 5
#define PIN_MISO 19
#define PIN_MOSI 27
#define PIN_CS 18
#define PIN_RST 23
#define PIN_DIO1 33  // DCLK in continuous mode
#define PIN_DIO2 32  // DATA in continuous mode (GPIO_IN1 bit 0)

// SX1276 FSK registers
#define REG_OPMODE 0x01
#define REG_BITRATE_MSB 0x02
#define REG_FRF_MSB 0x06
#define REG_LNA 0x0C
#define REG_RXCONFIG 0x0D
#define REG_RSSIVALUE 0x11
#define REG_RXBW 0x12
#define REG_OOKPEAK 0x14
#define REG_PACKETCONFIG2 0x31
#define REG_IRQFLAGS1 0x3E
#define REG_DIOMAPPING1 0x40
#define REG_DIOMAPPING2 0x41

XPowersAXP2101 pmu;

float freqMHz = 868.3;
float threshDbm = -100;  // overwritten at boot: noise floor + 10 dB
uint32_t bitSyncBps = 0;  // 0 = edge mode
float txDevKHz = 20;      // ponytail: unknown CC1101 deviation, tune with 'd'
int txDbm = 7;
uint32_t lastTxMs = 0;

const int MAX_EDGES = 4096;
volatile uint32_t edgeTs[MAX_EDGES];
volatile uint8_t edgeLvl[MAX_EDGES];
const int MAX_BITS = 16384;
volatile uint8_t bits[MAX_BITS / 8];
volatile int nEdges = 0, nBits = 0;
int lastEdges = 0, lastBits = 0;  // counts at last above-threshold RSSI sample (drops the noise tail)
volatile bool armed = false;
uint32_t cpuMHz;

static inline uint32_t IRAM_ATTR ccount() {
  uint32_t c;
  asm volatile("rsr %0, ccount" : "=a"(c));
  return c;
}
static inline uint8_t IRAM_ATTR dio2() { return REG_READ(GPIO_IN1_REG) & 1; }

void IRAM_ATTR onEdge() {
  if (!armed || nEdges >= MAX_EDGES) return;
  edgeTs[nEdges] = ccount();
  edgeLvl[nEdges] = dio2();
  nEdges++;
}

void IRAM_ATTR onDclk() {
  if (!armed || nBits >= MAX_BITS) return;
  if (dio2()) bits[nBits >> 3] |= 0x80 >> (nBits & 7);
  nBits++;
}

uint8_t rd(uint8_t reg) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t v = SPI.transfer(0);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  return v;
}
void wr(uint8_t reg, uint8_t v) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(v);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

float rssi() { return -rd(REG_RSSIVALUE) / 2.0f; }

void setMode(uint8_t mode) {  // FSK, HF band (LowFrequencyModeOn=0)
  wr(REG_OPMODE, mode);
  uint32_t t0 = millis();
  while (!(rd(REG_IRQFLAGS1) & 0x80) && millis() - t0 < 50) {}  // ModeReady
}

void setFreq(float mhz) {
  uint32_t frf = ((uint64_t)(mhz * 1e6) << 19) / 32000000ULL;  // Fstep = 32 MHz / 2^19
  wr(REG_FRF_MSB, frf >> 16);
  wr(REG_FRF_MSB + 1, frf >> 8);
  wr(REG_FRF_MSB + 2, frf);
}

uint8_t rxBwReg(int khz) {  // RxBw = 32 MHz / (Mant * 2^(Exp+2)), Mant 16/20/24 -> 0/1/2
  switch (khz) {
    case 200: return 0x09;
    case 166: return 0x11;
    case 125: return 0x02;
    case 100: return 0x0A;
    case 62: return 0x03;
    default: return 0x01;  // 250
  }
}

void configure() {
  detachInterrupt(PIN_DIO1);
  detachInterrupt(PIN_DIO2);
  setMode(0x00);  // sleep, FSK (LongRangeMode only changeable in sleep)
  setMode(0x01);  // standby
  setFreq(freqMHz);
  wr(REG_LNA, 0x23);       // max gain, LnaBoostHf on
  wr(REG_RXCONFIG, 0x08);  // AGC on, AFC off, no restart
  wr(REG_PACKETCONFIG2, 0x00);  // DataMode = continuous
  wr(REG_DIOMAPPING1, 0x00);    // DIO1 = DCLK, DIO2 = DATA
  wr(REG_DIOMAPPING2, 0x00);
  if (bitSyncBps) {
    uint16_t br = 32000000UL / bitSyncBps;
    wr(REG_BITRATE_MSB, br >> 8);
    wr(REG_BITRATE_MSB + 1, br);
    wr(REG_OOKPEAK, 0x28);  // BitSyncOn
  } else {
    wr(REG_OOKPEAK, 0x08);  // BitSyncOff: raw demod on DIO2
  }
  setMode(0x05);  // RX
  if (bitSyncBps) attachInterrupt(PIN_DIO1, onDclk, RISING);
  else attachInterrupt(PIN_DIO2, onEdge, CHANGE);

  Serial.printf("# cfg: %.4f MHz, %s, thresh %.1f dBm\n", freqMHz,
                bitSyncBps ? "bit-sync" : "edge mode", threshDbm);
  if (bitSyncBps) Serial.printf("# bit-sync at %u bps\n", bitSyncBps);
  Serial.printf("# readback OPMODE=%02X FRF=%02X%02X%02X LNA=%02X RXCFG=%02X RXBW=%02X OOKPEAK=%02X PKT2=%02X DIO1=%02X DIO2=%02X\n",
                rd(REG_OPMODE), rd(0x06), rd(0x07), rd(0x08), rd(REG_LNA), rd(REG_RXCONFIG), rd(REG_RXBW),
                rd(REG_OOKPEAK), rd(REG_PACKETCONFIG2), rd(REG_DIOMAPPING1), rd(REG_DIOMAPPING2));
}

void flushEdges(float peak, float feiHz, uint32_t burstUs) {
  int n = lastEdges;
  if (n < 16) {
    Serial.printf("# blip edges=%d rssi=%.1f\n", n, peak);
    return;
  }
  static float d[MAX_EDGES];
  for (int i = 0; i < n - 1; i++) d[i] = (edgeTs[i + 1] - edgeTs[i]) / (float)cpuMHz;
  Serial.printf("BURST t=%lu edges=%d rssi=%.1f fei=%.0fHz dur=%uus%s\n", millis(), n, peak, feiHz, burstUs,
                n >= MAX_EDGES ? " TRUNCATED" : "");
  Serial.print("PULSES");
  for (int i = 0; i < n - 1; i++) Serial.printf(" %c%.1f", edgeLvl[i] ? 'H' : 'L', d[i]);
  Serial.println();
}

void flushBits(float peak, float feiHz, uint32_t burstUs) {
  int n = lastBits;
  if (n < 32) {
    Serial.printf("# blip bits=%d rssi=%.1f\n", n, peak);
    memset((void *)bits, 0, sizeof(bits));
    return;
  }
  Serial.printf("SYNCBURST t=%lu bits=%d rssi=%.1f fei=%.0fHz dur=%uus%s\nBITS ", millis(), n, peak, feiHz, burstUs, n >= MAX_BITS ? " TRUNCATED" : "");
  for (int i = 0; i < n; i++) Serial.write(bits[i >> 3] & (0x80 >> (i & 7)) ? '1' : '0');
  Serial.print("\nHEX ");
  for (int i = 0; i < (n + 7) / 8; i++) Serial.printf("%02X", bits[i]);
  Serial.println();
  memset((void *)bits, 0, sizeof(bits));
}


void txPacket(const String &hex) {
  int n = hex.length() / 2;
  if (n < 1 || n > 64 || hex.length() % 2) { Serial.println("# TX refused: need 1..64 bytes of hex"); return; }
  if (lastTxMs && millis() - lastTxMs < 1000) { Serial.println("# TX refused: <1 s since last TX"); return; }
  uint8_t buf[64];
  for (int i = 0; i < n; i++) buf[i] = strtoul(hex.substring(2 * i, 2 * i + 2).c_str(), nullptr, 16);

  detachInterrupt(PIN_DIO1);
  detachInterrupt(PIN_DIO2);
  armed = false;
  setMode(0x01);
  setFreq(freqMHz);
  wr(REG_BITRATE_MSB, 976 >> 8);  // 32 MHz / (976 + 9/16) = 32768 chip/s exactly
  wr(REG_BITRATE_MSB + 1, 976 & 0xFF);
  wr(0x5D, 9);
  uint16_t fd = txDevKHz * 1000 / 61.035f;
  wr(0x04, fd >> 8);
  wr(0x05, fd);
  int dbm = constrain(txDbm, 2, 14);  // 868.0-868.6 MHz: 25 mW ERP max
  wr(0x09, 0x80 | (dbm - 2));         // PA_BOOST, Pout = 2 + OutputPower dBm
  wr(0x0A, 0x09);                     // no shaping, 40 us ramp
  wr(0x25, 0);
  wr(0x26, 8);     // 8 preamble bytes 0xAA
  wr(0x27, 0x00);  // sync off (sync chips are in our payload), preamble polarity 0xAA
  wr(0x30, 0x00);  // fixed length, no DC-free, no CRC, no address filter
  wr(REG_PACKETCONFIG2, 0x40);  // packet mode
  wr(0x32, n);
  wr(0x35, 0x80 | 0x0F);  // TX starts on FIFO not empty
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(0x80);  // FIFO burst write
  for (int i = 0; i < n; i++) SPI.transfer(buf[i]);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  uint32_t t0 = micros();
  wr(REG_OPMODE, 0x03);  // TX
  bool sent = false;
  while (micros() - t0 < 100000) if (rd(0x3F) & 0x08) { sent = true; break; }  // PacketSent
  uint32_t air = micros() - t0;
  lastTxMs = millis();
  Serial.printf("TX t=%lu bytes=%d %.4fMHz dev=%.1fkHz pwr=%ddBm sent=%d air=%luus\n", lastTxMs, n, freqMHz, txDevKHz, dbm, sent, air);
  configure();  // straight back to RX (reply expected ~123 ms later)
}

void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') { line += c; continue; }
    line.trim();
    if (!line.length()) continue;
    char cmd = line[0];
    float v = line.substring(1).toFloat();
    if (cmd == 'x') { String h = line.substring(1); h.trim(); line = ""; txPacket(h); continue; }
    line = "";
    if (cmd == 't') threshDbm = v;
    else if (cmd == 'f') freqMHz = v;
    else if (cmd == 'b') bitSyncBps = (uint32_t)v;
    else if (cmd == 'e') bitSyncBps = 0;
    else if (cmd == 'w') wr(REG_RXBW, rxBwReg((int)v));
    else if (cmd == 'd') { txDevKHz = v; Serial.printf("# tx dev %.1f kHz\n", v); continue; }
    else if (cmd == 'p') { txDbm = constrain((int)v, 2, 14); Serial.printf("# tx power %d dBm\n", txDbm); continue; }
    else if (cmd == 'r') { Serial.printf("# rssi %.1f dBm\n", rssi()); continue; }
    else { Serial.println("# cmds: t <dBm> | f <MHz> | w <kHz> | b <bps> | e | r | x <hex> | d <kHz> | p <dBm>"); continue; }
    configure();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  cpuMHz = getCpuFrequencyMhz();
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 21, 22)) Serial.println("# WARN: AXP2101 not found");
  pmu.setALDO2Voltage(3300);
  pmu.enableALDO2();  // LoRa rail
  delay(50);

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DIO1, INPUT);
  pinMode(PIN_DIO2, INPUT);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(2);
  digitalWrite(PIN_RST, HIGH);
  delay(20);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  Serial.printf("\n# tbeam-sniff, SX127x ver 0x%02X, cpu %u MHz\n", rd(0x42), cpuMHz);

  configure();
  wr(REG_RXBW, rxBwReg(250));
  delay(20);
  float sum = 0, mx = -200;
  for (int i = 0; i < 500; i++) {
    float r = rssi();
    sum += r;
    mx = max(mx, r);
    delayMicroseconds(500);
  }
  threshDbm = sum / 500 + 10;  // ponytail: fixed +10 dB margin, tune with 't' if noisy/deaf
  Serial.printf("# noise floor avg %.1f max %.1f dBm -> thresh %.1f dBm\n", sum / 500, mx, threshDbm);
}

void loop() {
  static uint32_t lastAbove = 0, start = 0;
  static float peak = -200, feiHz = 0;
  static bool feiDone = false;
  float r = rssi();
  uint32_t now = micros();
  if (r >= threshDbm) {
    if (!armed) {
      nEdges = 0;
      nBits = 0;
      peak = -200;
      start = now;
      feiDone = false;
      feiHz = NAN;
      armed = true;
    }
    lastAbove = now;
    lastEdges = nEdges;
    lastBits = nBits;
    peak = max(peak, r);
    if (!feiDone && now - start > 2000) {  // mid-burst: carrier offset vs our LO (RegFeiMsb/Lsb, 2's complement x Fstep)
      feiHz = (int16_t)((rd(0x1D) << 8) | rd(0x1E)) * 61.035f;
      feiDone = true;
    }
    if (now - start > 100000) {  // nothing real is this long: noise floor rose above threshold
      armed = false;
      threshDbm += 3;
      memset((void *)bits, 0, sizeof(bits));
      Serial.printf("# stuck above threshold 100 ms, raising thresh to %.1f dBm\n", threshDbm);
    }
  } else if (armed && now - lastAbove > 3000) {  // 3 ms below threshold ends a burst
    armed = false;
    if (bitSyncBps) flushBits(peak, feiHz, lastAbove - start);
    else flushEdges(peak, feiHz, lastAbove - start);
  }
  handleSerial();
}
