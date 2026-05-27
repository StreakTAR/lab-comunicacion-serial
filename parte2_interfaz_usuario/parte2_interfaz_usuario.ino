/*
 * Laboratorio Comunicación Serial - Parte II - FRONTEND
 * con gestión de mensaje de bienvenida desde keypad y LCD.
 *
 * Menú principal:
 *   A : SERVO         (servo con ángulo por keypad)
 *   B : TEMP          (lectura periódica de temperatura)
 *   D : WELCOME       (submenú: ver / editar / borrar mensaje)
 *   C : refrescar menú
 *
 * Submenú WELCOME:
 *   1 : Ver el mensaje actual
 *   2 : Editar mensaje (solo dígitos 0-9)
 *   3 : Borrar mensaje
 *   C : Volver
 *
 * El mensaje se guarda en EEPROM y se muestra en el LCD al arrancar.
 *
 * También sigue respondiendo al protocolo del backend:
 *   - "WSET:<mensaje>\n" -> guarda y responde "OK"
 *   - "WCLR\n"           -> borra y responde "OK"
 *
 * Hardware:
 *   Keypad 4x4:  filas R1-R4 -> pines 9,8,7,6
 *                col  C1-C4  -> pines 5,4,3,2
 *   LCD 16x2 I2C: SDA=A4, SCL=A5
 *   SoftwareSerial: pin 10 (RX) <- pin 11 (TX) backend
 *                   pin 11 (TX) -> pin 10 (RX) backend
 *   GND común con backend.
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// ---------- Keypad 4x4 ----------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial link(10, 11);
const char EOT = 0x04;

// ---------- EEPROM ----------
const uint8_t  EEPROM_MAGIC      = 0xAA;
const uint16_t EEPROM_ADDR_MAGIC = 0;
const uint16_t EEPROM_ADDR_LEN   = 1;
const uint16_t EEPROM_ADDR_DATA  = 3;
const uint16_t WELCOME_MAX_LEN   = 32;   // 2 filas de 16 chars en el LCD

bool loadWelcomeMessage(char *buf, uint16_t maxLen) {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC) return false;
  uint16_t len = EEPROM.read(EEPROM_ADDR_LEN)
               | (EEPROM.read(EEPROM_ADDR_LEN + 1) << 8);
  if (len == 0 || len >= maxLen || len > WELCOME_MAX_LEN) return false;
  for (uint16_t i = 0; i < len; i++) {
    buf[i] = (char) EEPROM.read(EEPROM_ADDR_DATA + i);
  }
  buf[len] = '\0';
  return true;
}

void saveWelcomeMessage(const char *msg) {
  uint16_t len = strlen(msg);
  if (len > WELCOME_MAX_LEN) len = WELCOME_MAX_LEN;
  EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.update(EEPROM_ADDR_LEN,     (uint8_t)(len & 0xFF));
  EEPROM.update(EEPROM_ADDR_LEN + 1, (uint8_t)((len >> 8) & 0xFF));
  for (uint16_t i = 0; i < len; i++) {
    EEPROM.update(EEPROM_ADDR_DATA + i, (uint8_t) msg[i]);
  }
}

void clearWelcomeMessage() {
  EEPROM.update(EEPROM_ADDR_MAGIC, 0xFF);
}

// ---------- LCD helpers ----------
void lcdMsg(const char *l1, const char *l2 = "") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

void lcdShowWelcome(const char *msg) {
  lcd.clear();
  uint16_t len = strlen(msg);
  lcd.setCursor(0,0);
  for (uint16_t i = 0; i < 16 && i < len; i++) lcd.write(msg[i]);
  if (len > 16) {
    lcd.setCursor(0,1);
    for (uint16_t i = 16; i < 32 && i < len; i++) lcd.write(msg[i]);
  }
}

char waitKey() {
  while (true) {
    char k = keypad.getKey();
    if (k != NO_KEY) return k;
  }
}

void drainUntilEOT(unsigned long timeoutMs = 1500) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (link.available()) {
      char c = link.read();
      if (c == EOT) return;
    }
  }
}

bool readBackendLine(char *buf, uint16_t maxLen, unsigned long timeoutMs = 3000) {
  uint16_t i = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (link.available()) {
      char c = link.read();
      if (c == EOT) { buf[i] = '\0'; return false; }
      if (c == '\r') continue;
      if (c == '\n') { buf[i] = '\0'; return true; }
      if (i < maxLen - 1) buf[i++] = c;
      t0 = millis();
    }
  }
  buf[i] = '\0';
  return true;
}

bool handleProtocol(const char *line) {
  if (strncmp(line, "WSET:", 5) == 0) {
    saveWelcomeMessage(line + 5);
    link.print("OK\n");
    return true;
  }
  if (strcmp(line, "WCLR") == 0) {
    clearWelcomeMessage();
    link.print("OK\n");
    return true;
  }
  return false;
}

uint8_t lcdRow = 0;
void printToLcdRolling(const char *s) {
  if (lcdRow == 0) lcd.clear();
  lcd.setCursor(0, lcdRow);
  for (uint8_t i = 0; i < 16 && s[i]; i++) lcd.write(s[i]);
  lcdRow = (lcdRow + 1) % 2;
}

bool askNumber(const char *titulo, char *out, uint8_t maxDigits) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(titulo);
  lcd.setCursor(0,1);
  uint8_t n = 0;
  out[0] = '\0';
  while (true) {
    char k = waitKey();
    if (k >= '0' && k <= '9' && n < maxDigits) {
      out[n++] = k;
      out[n] = '\0';
      lcd.setCursor(0,1); lcd.print("                ");
      lcd.setCursor(0,1); lcd.print(out);
    } else if (k == '*') {
      if (n > 0) { n--; out[n] = '\0'; }
      lcd.setCursor(0,1); lcd.print("                ");
      lcd.setCursor(0,1); lcd.print(out);
    } else if (k == '#') {
      if (n > 0) return true;
    } else if (k == 'C' || k == 'D') {
      return false;
    }
  }
}

// ---------- Flujos del menú principal ----------

void flowServo() {
  char ang[8];
  if (!askNumber("SERVO ang 0-180", ang, 3)) return;

  link.print("servo\n");
  delay(150);
  while (link.available()) link.read();
  link.print(ang); link.print('\n');

  lcdRow = 0;
  char line[40];
  while (true) {
    bool isLine = readBackendLine(line, sizeof(line));
    if (isLine && handleProtocol(line)) continue;
    if (line[0]) printToLcdRolling(line);
    if (!isLine) break;
  }
  delay(1200);
}

void flowTemp() {
  char ms[8];
  if (!askNumber("TEMP ms (D=stop)", ms, 5)) return;

  link.print("temp "); link.print(ms); link.print('\n');

  lcdMsg("TEMP en vivo", "D=detener");
  delay(700);
  lcd.clear();

  char line[40];
  while (true) {
    char k = keypad.getKey();
    if (k == 'D' || k == 'C') {
      link.print("q\n");
      drainUntilEOT(2000);
      lcdMsg("TEMP detenida", "");
      delay(1000);
      return;
    }
    if (link.available()) {
      bool isLine = readBackendLine(line, sizeof(line), 200);
      if (!isLine && line[0] == '\0') return;
      if (line[0]) {
        char *coma = strchr(line, ',');
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("t=");
        if (coma) {
          *coma = '\0';
          lcd.print(line); lcd.print(" ms");
          lcd.setCursor(0,1); lcd.print("T="); lcd.print(coma+1); lcd.print(" C");
        } else {
          lcd.print(line);
        }
      }
      if (!isLine) return;
    }
  }
}

// ---------- Submenú WELCOME ----------

// Muestra el mensaje actual; espera tecla para volver.
void welcomeVer() {
  char buf[WELCOME_MAX_LEN + 1];
  if (loadWelcomeMessage(buf, sizeof(buf))) {
    lcdShowWelcome(buf);
  } else {
    lcdMsg("Sin mensaje", "guardado");
  }
  // esperar tecla para volver
  delay(300);  // anti-rebote para no consumir la 'D' que nos trajo aquí
  while (keypad.getKey() == NO_KEY) { /* esperar */ }
}

// Repinta la fila 2 del LCD con el contenido del buffer en edición.
// Si tiene más de 16 chars muestra los últimos 16.
void repaintEditLine(const char *buf, uint16_t n) {
  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);
  if (n <= 16) lcd.print(buf);
  else         lcd.print(buf + (n - 16));
}

// Permite escribir un mensaje nuevo de dígitos.
// '*' borra, '#' guarda, 'C' cancela.
void welcomeEditar() {
  char buf[WELCOME_MAX_LEN + 1];
  uint16_t n = 0;
  buf[0] = '\0';

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Edite: (#=ok)");

  while (true) {
    char k = waitKey();
    if (k >= '0' && k <= '9' && n < WELCOME_MAX_LEN) {
      buf[n++] = k;
      buf[n] = '\0';
      repaintEditLine(buf, n);
    } else if (k == '*') {
      if (n > 0) { n--; buf[n] = '\0'; repaintEditLine(buf, n); }
    } else if (k == '#') {
      if (n == 0) {
        lcdMsg("Vacio: no", "se guardo");
        delay(1500);
        return;
      }
      saveWelcomeMessage(buf);
      lcdMsg("Guardado!", "Reinicie p/ver");
      delay(1800);
      return;
    } else if (k == 'C') {
      lcdMsg("Cancelado", "");
      delay(900);
      return;
    }
  }
}

void welcomeBorrar() {
  lcdMsg("Borrar mensaje?", "#=si  C=no");
  while (true) {
    char k = waitKey();
    if (k == '#') {
      clearWelcomeMessage();
      lcdMsg("Borrado", "");
      delay(1200);
      return;
    } else if (k == 'C') {
      lcdMsg("Cancelado", "");
      delay(900);
      return;
    }
  }
}

void flowWelcome() {
  while (true) {
    lcdMsg("1:Ver  2:Edit", "3:Borr   C=sal");
    char k = waitKey();
    if (k == '1') {
      welcomeVer();
    } else if (k == '2') {
      welcomeEditar();
    } else if (k == '3') {
      welcomeBorrar();
    } else if (k == 'C') {
      return;
    }
  }
}

// ---------- Menú principal ----------

void showMenu() {
  lcdMsg("A:SERVO  B:TEMP", "D:welc   C=menu");
}

// ---------- Background: WSET/WCLR del backend ----------
void checkBackgroundProtocol() {
  static char protoBuf[40];
  static uint16_t pi = 0;
  static bool inProto = false;
  static unsigned long lastByteTime = 0;

  while (link.available()) {
    char c = link.read();
    lastByteTime = millis();
    if (c == '\r') continue;
    if (c == '\n') {
      protoBuf[pi] = '\0';
      if (handleProtocol(protoBuf)) {
        lcdMsg("Mensaje guardado", "OK (desde A)");
        delay(1500);
        showMenu();
      }
      pi = 0;
      inProto = false;
      return;
    }
    if (pi < sizeof(protoBuf) - 1) {
      protoBuf[pi++] = c;
      inProto = true;
    }
  }
  if (inProto && millis() - lastByteTime > 1000) {
    pi = 0;
    inProto = false;
  }
}

void setup() {
  Serial.begin(9600);
  link.begin(2400);
  lcd.init();
  lcd.backlight();

  // --- Bienvenida persistente ---
  char welcomeBuf[WELCOME_MAX_LEN + 1];
  if (loadWelcomeMessage(welcomeBuf, sizeof(welcomeBuf))) {
    lcdShowWelcome(welcomeBuf);
    delay(3000);
  } else {
    lcdMsg("Lab Serial", "Sin bienvenida");
    delay(1500);
  }

  lcdMsg("Conectando...", "Backend");
  drainUntilEOT(2500);
  showMenu();
}

void loop() {
  checkBackgroundProtocol();

  char k = keypad.getKey();
  if (k == NO_KEY) return;

  if (k == 'A') {
    flowServo();
    showMenu();
  } else if (k == 'B') {
    flowTemp();
    showMenu();
  } else if (k == 'D') {
    flowWelcome();
    showMenu();
  } else if (k == 'C') {
    showMenu();
  }
}
