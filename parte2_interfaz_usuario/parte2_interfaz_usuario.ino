/*
 * Laboratorio Comunicación Serial - Parte II - FRONTEND (Keypad 4x4)
 *
 * Interfaz de usuario:
 *   - Keypad matricial 4x4 (filas R1-R4 -> pines 9,8,7,6
 *                           col  C1-C4  -> pines 5,4,3,2)
 *   - LCD 16x2 I2C (SDA=A4, SCL=A5)
 *
 * Comunicación con backend:
 *   - SoftwareSerial pin 10 (RX) <- pin 11 (TX) del backend
 *                   pin 11 (TX) -> pin 10 (RX) del backend
 *   - GND común entre ambos Arduinos.
 *
 * Mapa del keypad:
 *   A : SERVO
 *   B : TEMP
 *   C : volver al menú / cancelar
 *   D : detener lectura en vivo (equivale a 'q')
 *   0-9 : dígitos
 *   *  : borrar último dígito
 *   #  : confirmar / Enter
 *
 * Librerías:
 *   - Keypad (Mark Stanley / Alexander Brevig)
 *   - LiquidCrystal_I2C  (Frank de Brabander)
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

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

// ---------- LCD ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- Link al backend ----------
SoftwareSerial link(10, 11);
const char EOT = 0x04;

// ---------- Utilidades de LCD ----------
void lcdMsg(const char *l1, const char *l2 = "") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
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

// Lee una línea desde el backend.
// Devuelve true si terminó por '\n', false si terminó por EOT o timeout.
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

uint8_t lcdRow = 0;
void printToLcdRolling(const char *s) {
  if (lcdRow == 0) lcd.clear();
  lcd.setCursor(0, lcdRow);
  for (uint8_t i = 0; i < 16 && s[i]; i++) lcd.write(s[i]);
  lcdRow = (lcdRow + 1) % 2;
}

// ---------- Entrada de número por keypad ----------
// '#' confirma, '*' borra dígito, 'C' o 'D' cancelan.
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

// ---------- Flujos ----------

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

// ---------- Menú ----------

void showMenu() {
  lcdMsg("A:SERVO  B:TEMP", "C=menu");
}

void setup() {
  Serial.begin(9600);
  link.begin(2400);
  lcd.init();
  lcd.backlight();

  lcdMsg("Conectando...", "Backend");
  drainUntilEOT(2500);
  showMenu();
}

void loop() {
  char k = keypad.getKey();
  if (k == NO_KEY) return;

  if (k == 'A') {
    flowServo();
    showMenu();
  } else if (k == 'B') {
    flowTemp();
    showMenu();
  } else if (k == 'C') {
    showMenu();
  }
}
