# Laboratorio de Comunicación Serial - Arduino Uno

Implementación de terminal interactivo sobre comunicación serial 
con Arduino Uno, incluyendo control de actuador, lectura periódica 
de sensor y volcado binario de RAM.

## Estructura

- `parte1_terminal/` — Terminal interactivo con servo, TMP36 y dump RAM
- `parte2_terminal_arduino/` — Backend de la Parte II (recibe comandos por SoftwareSerial)
- `parte2_interfaz_usuario/` — Frontend con keypad 4x4 y LCD I2C
- `.github/workflows/` — CI: compilación automática en cada push

## Simulación en Tinkercad

[Enlace al diseño de Tinkercad] ← agregar aquí

## Hardware

- Arduino Uno (x2)
- Servomotor SG90
- Sensor de temperatura TMP36
- Keypad matricial 4x4
- LCD 16x2 con módulo I2C (PCF8574, dirección 0x27)

## Compilación

Los sketches se compilan automáticamente en GitHub Actions con cada push. 
Ver pestaña [Actions](../../actions).
