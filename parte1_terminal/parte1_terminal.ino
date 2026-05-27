/*
 * Laboratorio Comunicación Serial - Parte I
 * Terminal interactiva sobre UART con 3 comandos:
 *   - servo         : controla un servomotor (interactivo)
 *   - temp <ms>     : lee TMP36 periódicamente hasta recibir 'q'
 *   - dump <ini> <fin> : volcado binario crudo de la RAM
 *
 * Hardware (Tinkercad / Arduino Uno):
 *   Pin 9  -> Servo (signal)
 *   Pin A0 -> TMP36 (Vout)
 *   5V/GND -> alimentación de ambos
 */

#include <Servo.h>

// ---------- Configuración ----------
const uint8_t  PIN_SERVO = 9;
const uint8_t  PIN_TMP36 = A0;
const uint16_t BUF_SIZE  = 64;     // tamaño del buffer de la línea
const char     PROMPT[]  = "arduino> ";

Servo servo;
char  lineBuf[BUF_SIZE];           // buffer para la línea que escribe el usuario
uint8_t lineLen = 0;

// ---------- Utilidades de lectura ----------

// Lee una línea completa terminada en '\n' o '\r'. Bloqueante.
// Hace eco de cada caracter para que el usuario vea lo que escribe.
void readLine(char *buf, uint16_t maxLen) {
  uint16_t i = 0;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;          // ignoramos CR de "CR+LF"
      if (c == '\n') {                  // fin de línea
        buf[i] = '\0';
        Serial.println();               // bajamos cursor en el terminal
        return;
      }
      if (c == 8 || c == 127) {         // backspace
        if (i > 0) { i--; Serial.print("\b \b"); }
        continue;
      }
      if (i < maxLen - 1) {
        buf[i++] = c;
        Serial.print(c);                // eco
      }
    }
  }
}

// Convierte un string hexadecimal ("0x1FF", "1ff", "511") a entero.
// Devuelve -1 si la conversión falla.
long parseAddress(const char *s) {
  if (s == NULL || *s == '\0') return -1;
  // strtol con base 0 detecta automáticamente 0x / 0 / decimal
  char *end;
  long v = strtol(s, &end, 0);
  if (*end != '\0') return -1;
  return v;
}

// ---------- Comandos ----------

void cmdHelp() {
  Serial.println(F("Comandos disponibles:"));
  Serial.println(F("  help                       - muestra esta ayuda"));
  Serial.println(F("  servo                      - mueve el servo (pide angulo)"));
  Serial.println(F("  temp <intervalo_ms>        - lee TMP36 periodicamente"));
  Serial.println(F("                               presione 'q' + Enter para salir"));
  Serial.println(F("  dump <ini_hex> <fin_hex>   - volcado binario de RAM"));
  Serial.println(F("                               ej: dump 0x100 0x1FF"));
}

// --- Comando SERVO: interactivo ---
void cmdServo() {
  Serial.print(F("Ingrese angulo (0-180): "));
  char angBuf[16];
  readLine(angBuf, sizeof(angBuf));
  int ang = atoi(angBuf);
  if (ang < 0 || ang > 180) {
    Serial.println(F("[error] angulo fuera de rango"));
    return;
  }
  servo.write(ang);
  Serial.print(F("[ok] servo en "));
  Serial.print(ang);
  Serial.println(F(" grados"));
}

// --- Comando TEMP: lectura periódica con timestamp ---
void cmdTemp(const char *argStr) {
  long intervalo = atol(argStr);
  if (intervalo <= 0) {
    Serial.println(F("[error] uso: temp <intervalo_ms>  (intervalo > 0)"));
    return;
  }
  Serial.print(F("[info] lectura cada "));
  Serial.print(intervalo);
  Serial.println(F(" ms. Presione 'q' + Enter para detener."));
  Serial.println(F("t_ms,temp_C"));

  unsigned long t0 = millis();
  unsigned long tLast = 0;

  while (true) {
    // ¿el usuario pidió detener?
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'q' || c == 'Q' || c == 3) { // 3 = Ctrl+C real (por si acaso)
        // consumimos el resto de la línea si la hay
        while (Serial.available()) Serial.read();
        Serial.println(F("[info] lectura detenida"));
        return;
      }
    }

    unsigned long now = millis();
    if (now - tLast >= (unsigned long)intervalo) {
      tLast = now;
      // TMP36: Vout = 10 mV/°C con offset de 500 mV a 0 °C
      int raw = analogRead(PIN_TMP36);
      float volts = raw * (5.0f / 1023.0f);
      float tempC = (volts - 0.5f) * 100.0f;
      Serial.print(now - t0);
      Serial.print(',');
      Serial.println(tempC, 2);
    }
  }
}

// --- Comando DUMP: volcado binario de RAM ---
//
// El Arduino Uno (ATmega328P) tiene 2 KB de SRAM mapeados en
// 0x0100 .. 0x08FF del espacio de datos. Antes de 0x0100 están
// los registros y la I/O. Permitimos cualquier rango que el
// usuario indique, pero advertimos si pisa zonas no-RAM.
//
// Salida: bytes crudos vía Serial.write, sin separadores ni texto.
// Para capturarlo limpio fuera de Tinkercad se puede usar:
//   stty -F /dev/ttyUSB0 9600 raw && cat /dev/ttyUSB0 > dump.bin
// y luego  xxd dump.bin  para inspeccionarlo.
void cmdDump(const char *iniStr, const char *finStr) {
  long ini = parseAddress(iniStr);
  long fin = parseAddress(finStr);
  if (ini < 0 || fin < 0 || fin < ini) {
    Serial.println(F("[error] uso: dump <ini_hex> <fin_hex>  (fin >= ini)"));
    return;
  }
  if (ini > 0xFFFF || fin > 0xFFFF) {
    Serial.println(F("[error] direcciones fuera del espacio de 16 bits"));
    return;
  }

  uint16_t total = (uint16_t)(fin - ini + 1);
  // Cabecera legible (texto) para que el usuario sepa qué viene
  Serial.print(F("[info] volcando "));
  Serial.print(total);
  Serial.print(F(" bytes desde 0x"));
  Serial.print((uint16_t)ini, HEX);
  Serial.print(F(" hasta 0x"));
  Serial.println((uint16_t)fin, HEX);
  Serial.println(F("[info] inicio de datos binarios:"));
  Serial.flush();

  // El volcado en sí: bytes crudos
  for (uint16_t addr = (uint16_t)ini; ; addr++) {
    uint8_t *p = (uint8_t *)addr;
    Serial.write(*p);
    if (addr == (uint16_t)fin) break;   // evita overflow si fin == 0xFFFF
  }
  Serial.flush();
  Serial.println();
  Serial.println(F("[info] fin del volcado"));
}

// ---------- Dispatcher ----------

void handleLine(char *line) {
  // tokenizamos por espacios
  char *cmd = strtok(line, " \t");
  if (cmd == NULL) return;             // línea vacía

  if      (strcmp(cmd, "help")  == 0) cmdHelp();
  else if (strcmp(cmd, "servo") == 0) cmdServo();
  else if (strcmp(cmd, "temp")  == 0) {
    char *arg1 = strtok(NULL, " \t");
    if (arg1 == NULL) {
      Serial.println(F("[error] uso: temp <intervalo_ms>"));
    } else cmdTemp(arg1);
  }
  else if (strcmp(cmd, "dump")  == 0) {
    char *arg1 = strtok(NULL, " \t");
    char *arg2 = strtok(NULL, " \t");
    if (arg1 == NULL || arg2 == NULL) {
      Serial.println(F("[error] uso: dump <ini_hex> <fin_hex>"));
    } else cmdDump(arg1, arg2);
  }
  else {
    Serial.print(F("[error] comando desconocido: "));
    Serial.println(cmd);
    Serial.println(F("        escriba 'help' para ver los disponibles"));
  }
}

// ---------- setup / loop ----------

void setup() {
  Serial.begin(9600);
  servo.attach(PIN_SERVO);
  servo.write(90);                     // posición inicial

  Serial.println();
  Serial.println(F("=================================="));
  Serial.println(F(" Terminal Arduino - Lab Serial   "));
  Serial.println(F(" escriba 'help' para empezar     "));
  Serial.println(F("=================================="));
}

void loop() {
  Serial.print(PROMPT);
  readLine(lineBuf, BUF_SIZE);
  handleLine(lineBuf);
}
