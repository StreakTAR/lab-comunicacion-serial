/*
 * Laboratorio Comunicación Serial - Parte II - BACKEND
 *
 * Mismo terminal de la Parte I, pero hablando por SoftwareSerial
 * con el Arduino frontend (keypad + LCD).
 *
 * - El USB (Serial hardware) queda libre para depurar.
 * - SoftwareSerial en pines 10/11 conversa con el frontend.
 * - Cuando un comando termina, enviamos el byte EOT (0x04)
 *   para que el frontend sepa que puede volver al menú.
 *
 * Hardware:
 *   Pin 9       -> Servo signal
 *   Pin A0      -> TMP36 Vout
 *   Pin 10 (RX) <- Pin 11 (TX) del Arduino frontend
 *   Pin 11 (TX) -> Pin 10 (RX) del Arduino frontend
 *   GND común entre ambos Arduinos.
 */

#include <Servo.h>
#include <SoftwareSerial.h>

// ---------- Configuración ----------
const uint8_t PIN_SERVO    = 9;
const uint8_t PIN_TMP36    = A0;
const uint8_t PIN_SS_RX    = 10;
const uint8_t PIN_SS_TX    = 11;
const uint16_t BUF_SIZE    = 64;
const char    PROMPT[]     = "arduino> ";
const char    EOT          = 0x04;     // marca fin de respuesta

SoftwareSerial link(PIN_SS_RX, PIN_SS_TX);
Servo  servo;
char   lineBuf[BUF_SIZE];

// ---------- Helpers ----------

// Escribe en ambos canales: link (para el frontend) y Serial (debug por USB).
void out(const char *s)              { link.print(s);   Serial.print(s); }
void outln(const char *s)            { link.println(s); Serial.println(s); }
void outF(const __FlashStringHelper *s) { link.print(s); Serial.print(s); }
void outlnF(const __FlashStringHelper *s){ link.println(s); Serial.println(s); }

// Lee una línea desde link, terminada en '\n'. Bloqueante.
// Lee una línea desde link, terminada en '\n'. Bloqueante.
// (Versión con debug: imprime cada caracter recibido por Serial)
void readLine(char *buf, uint16_t maxLen) {
  uint16_t i = 0;
  while (true) {
    if (link.available()) {
      char c = link.read();
      // DEBUG: muestra en Serial USB cada byte recibido
      Serial.print("[RX:");
      Serial.print((int)c);
      Serial.print("/");
      if (c >= 32 && c < 127) Serial.print(c); else Serial.print('?');
      Serial.println("]");
      
      if (c == '\r') continue;
      if (c == '\n') { buf[i] = '\0'; return; }
      if (i < maxLen - 1) buf[i++] = c;
    }
  }
}

long parseAddress(const char *s) {
  if (s == NULL || *s == '\0') return -1;
  char *end;
  long v = strtol(s, &end, 0);
  if (*end != '\0') return -1;
  return v;
}

// ---------- Comandos ----------

void cmdHelp() {
  outlnF(F("Comandos:"));
  outlnF(F("  help, servo, temp <ms>, dump <ini> <fin>"));
}

void cmdServo() {
  outF(F("Angulo (0-180): "));
  char buf[16];
  readLine(buf, sizeof(buf));
  int ang = atoi(buf);
  if (ang < 0 || ang > 180) { outlnF(F("[err] rango")); return; }
  servo.write(ang);
  out("[ok] servo=");
  char tmp[8]; itoa(ang, tmp, 10);
  outln(tmp);
}

void cmdTemp(const char *arg) {
  long intervalo = atol(arg);
  if (intervalo <= 0) { outlnF(F("[err] intervalo")); return; }
  outlnF(F("[info] temp iniciada"));

  unsigned long t0 = millis();
  unsigned long tLast = 0;

  while (true) {
    if (link.available()) {
      char c = link.read();
      if (c == 'q' || c == 'Q' || c == 3) {
        while (link.available()) link.read();
        outlnF(F("[info] temp detenida"));
        return;
      }
    }
    unsigned long now = millis();
    if (now - tLast >= (unsigned long)intervalo) {
      tLast = now;
      int raw = analogRead(PIN_TMP36);
      float volts = raw * (5.0f / 1023.0f);
      float tempC = (volts - 0.5f) * 100.0f;
      // formato compacto: "t,T" para que el LCD lo pueda parsear fácil
      char buf[24];
      // Arduino no tiene %f en printf por defecto: armamos a mano
      unsigned long t = now - t0;
      int tEnt = (int)tempC;
      int tDec = (int)((tempC - tEnt) * 100); if (tDec < 0) tDec = -tDec;
      snprintf(buf, sizeof(buf), "%lu,%d.%02d", t, tEnt, tDec);
      outln(buf);
    }
  }
}

void cmdDump(const char *iniStr, const char *finStr) {
  long ini = parseAddress(iniStr);
  long fin = parseAddress(finStr);
  if (ini < 0 || fin < 0 || fin < ini || ini > 0xFFFF || fin > 0xFFFF) {
    outlnF(F("[err] dump args"));
    return;
  }
  outlnF(F("[info] dump bin:"));
  link.flush(); Serial.flush();
  for (uint16_t addr = (uint16_t)ini; ; addr++) {
    uint8_t *p = (uint8_t *)addr;
    link.write(*p);
    Serial.write(*p);
    if (addr == (uint16_t)fin) break;
  }
  link.println(); Serial.println();
  outlnF(F("[info] dump fin"));
}

// ---------- Dispatcher ----------

void handleLine(char *line) {
  char *cmd = strtok(line, " \t");
  if (cmd == NULL) return;

  if      (strcmp(cmd, "help")  == 0) cmdHelp();
  else if (strcmp(cmd, "servo") == 0) cmdServo();
  else if (strcmp(cmd, "temp")  == 0) {
    char *a = strtok(NULL, " \t");
    if (a == NULL) outlnF(F("[err] uso: temp <ms>"));
    else cmdTemp(a);
  }
  else if (strcmp(cmd, "dump")  == 0) {
    char *a = strtok(NULL, " \t");
    char *b = strtok(NULL, " \t");
    if (a == NULL || b == NULL) outlnF(F("[err] uso: dump <i> <f>"));
    else cmdDump(a, b);
  }
  else {
    out("[err] ?: "); outln(cmd);
  }
}

void setup() {
  Serial.begin(9600);
  link.begin(2400);
  servo.attach(PIN_SERVO);
  servo.write(90);
  outlnF(F("== Backend listo =="));
}

void loop() {
  out(PROMPT);
  readLine(lineBuf, BUF_SIZE);
  handleLine(lineBuf);
  // marcador de fin-de-comando para el frontend
  link.write(EOT);
  Serial.write(EOT);
  Serial.println();
}
