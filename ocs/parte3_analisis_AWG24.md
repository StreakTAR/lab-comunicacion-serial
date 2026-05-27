# Parte III — Análisis de factibilidad: comunicación a 20 m con cable AWG 24

## Planteamiento

Se propone implementar físicamente el montaje de la Parte II con los dos
Arduinos ubicados en edificios separados por **20 metros** de distancia,
empleando como medio de transmisión cable AWG 24 sin ningún hardware
adicional. La pregunta es si esto es factible o qué modificaciones se
requieren para que el sistema funcione de manera confiable.

La respuesta corta es: **no es factible usar AWG 24 únicamente con la
señalización TTL que produce el UART/SoftwareSerial del Arduino Uno**. Se
requieren transceivers (típicamente RS-485 con MAX485) para que la
comunicación sea robusta a esa distancia. A continuación, el análisis
detallado.

---

## 1. ¿Cuál es el problema real? (Lo que NO es el problema)

Es tentador pensar que el problema es la resistencia del cable. Veámoslo:

El cable AWG 24 tiene una resistencia de aproximadamente **84.2 Ω/km**, es
decir, **0.0842 Ω/m**. Para 20 metros de ida y vuelta (40 m de cobre en
total) la resistencia es:

```
R_total = 0.0842 Ω/m × 40 m ≈ 3.4 Ω
```

Esto es **despreciable**. Para un Arduino que entrega aproximadamente 20 mA
por pin, la caída de tensión sería de apenas **68 mV** — irrelevante para
los umbrales TTL (donde el "1" lógico es > 2.0 V y el "0" lógico es < 0.8 V).

**Conclusión parcial:** la resistencia DC del AWG 24 NO es el problema. El
problema está en otra parte.

---

## 2. El problema real: integridad de la señal en TTL

La comunicación UART entre dos Arduinos usa señalización **TTL single-ended**
(referida a una masa común, con niveles 0 V / 5 V). Esta señalización tiene
tres limitaciones graves a 20 metros:

### 2.1 Capacitancia parásita del cable

Cada cable presenta una capacitancia distribuida del orden de
**40–60 pF/m**. Para 20 metros eso es **≈ 1 nF** por conductor. Junto con la
impedancia de salida del driver del Arduino (~25 Ω) y la del pin receptor,
se forma un filtro RC que **redondea los flancos de la señal**.

Para 9600 baud (el bit time es ~104 µs) puede que el sistema lo tolere,
pero los flancos de subida y bajada ya no son cuadrados, y el receptor
puede muestrear mal el bit. A 2400 baud (~417 µs por bit) hay más margen,
pero el problema fundamental persiste.

### 2.2 Susceptibilidad a ruido electromagnético (EMI)

Una señal single-ended es muy vulnerable: cualquier ruido inducido en el
cable (de motores cercanos, cableado eléctrico, equipos industriales,
incluso WiFi o celulares) se suma directamente a la señal y altera los
niveles. **Entre edificios** esto es prácticamente garantizado: el cable
actúa como una antena que recoge interferencia.

### 2.3 Diferencias de potencial de tierra entre edificios

Este es el problema más subestimado y el más peligroso. **Dos edificios
físicamente separados rara vez tienen la misma referencia de tierra.** Las
masas pueden diferir por varios voltios (o incluso decenas de voltios)
debido a:

- Distintos paneles eléctricos con tomas a tierra independientes.
- Corrientes circulantes por el terreno.
- Inducción por tormentas eléctricas (¡muy peligroso!).

Si conectamos el GND de los dos Arduinos con un simple cable AWG 24,
estamos creando un **bucle de tierra** por el que pueden circular corrientes
parásitas. Esto puede:

- Corromper la comunicación.
- Quemar los pines GPIO de los Arduinos.
- Provocar descargas peligrosas durante tormentas.

---

## 3. Límites prácticos de UART TTL

La literatura técnica confirma que UART en niveles TTL es confiable solo
para distancias muy cortas:

| Estándar | Señalización | Distancia máxima | Velocidad típica |
|----------|--------------|-----------------|------------------|
| **TTL (pelado)** | Single-ended 0/5 V | < 1–2 m (recomendado), 15 m en ambiente limpio | 9600 baud máximo |
| **RS-232** (con MAX232) | Single-ended ±12 V | 15 m | hasta 115200 baud |
| **RS-422** (con drivers) | Diferencial balanceado | 1200 m | hasta 10 Mbps |
| **RS-485** (con MAX485) | Diferencial balanceado | **1200 m** | hasta 10 Mbps |

Para 20 metros, **TTL directo está claramente fuera de su rango operativo
recomendado**, especialmente en un ambiente real entre edificios.

---

## 4. Solución recomendada: RS-485 con transceivers MAX485

La solución estándar de la industria para este escenario es usar **RS-485**
mediante el integrado **MAX485** (o módulos equivalentes ya armados). Esta
solución resuelve los tres problemas identificados:

### 4.1 ¿Cómo funciona?

RS-485 usa **señalización diferencial**: en lugar de un solo cable referido
a GND, usa dos cables (A y B) que llevan la señal y su inverso. El receptor
mide la **diferencia de tensión** entre A y B, no el valor absoluto contra
tierra.

Esto da inmunidad al ruido porque cualquier interferencia que se acople al
cable lo hace por igual en ambos conductores (modo común) y se cancela en
la resta del receptor.

### 4.2 Esquema del montaje propuesto

```
┌────────────────────┐                            ┌────────────────────┐
│   Arduino A        │                            │   Arduino B        │
│ (Edificio 1)       │                            │ (Edificio 2)       │
│                    │   ┌─────────┐              │   ┌─────────┐      │
│  TX  ──────────────┼──►│   DI    │              │   │   RO    │◄──── │ ── RX
│  RX  ◄─────────────┼───│   RO    │              │   │   DI    │◄──── │ ── TX
│  Pin 2 (DE/RE) ────┼──►│DE/RE────┤              │   ├DE/RE◄───┼──── Pin 2
│                    │   │   A     ├──── 20 m ────┤  A│         │      │
│                    │   │   B     ├──── par     ─┤  B│         │      │
│  GND ─────┬────────┘   │ GND     │   trenzado   │   │  GND    │      │
│           │            │   │     │              │   │   │     │      │
│           │            └───┼─────┘              └───┼───┘     │      │
│           │                │ 120 Ω en cada extremo │         │      │
│           │                │ (resistencia de       │         │      │
│           │                │  terminación)         │         │      │
│           └────────────────┴─────── (NO conectar GND directo)        │
│                                                                      │
│                            Cada Arduino con su fuente local          │
└──────────────────────────────────────────────────────────────────────┘
```

### 4.3 Componentes necesarios

- **2 × módulos MAX485** (uno por Arduino), aproximadamente USD 1 cada uno
- **2 × resistencias de terminación de 120 Ω** (una en cada extremo del bus)
- **Cable de par trenzado** (UTP CAT5/CAT6 es ideal — usar un par para A/B)
- Idealmente, un tercer conductor para una referencia de masa de señal
  (Signal Common, SC), aunque NO debe ser conexión de tierra de chasis

### 4.4 Modificaciones al código

El cambio en software es mínimo: solo hay que controlar el pin DE/RE del
MAX485 para conmutar entre transmisión y recepción (RS-485 es half-duplex).

```cpp
const uint8_t PIN_DE_RE = 2;

void setup() {
  pinMode(PIN_DE_RE, OUTPUT);
  digitalWrite(PIN_DE_RE, LOW);   // empezamos en modo recepción
  link.begin(9600);                // ahora SÍ es seguro 9600 baud
}

void transmitir(const char *s) {
  digitalWrite(PIN_DE_RE, HIGH);   // habilitar transmisor
  link.print(s);
  link.flush();                    // esperar a que termine de enviar
  digitalWrite(PIN_DE_RE, LOW);    // volver a recepción
}
```

---

## 5. Recomendaciones adicionales para implementación física

Más allá del transceiver, hay buenas prácticas que se deben seguir:

1. **Cable de par trenzado.** El trenzado del par A/B es lo que cancela el
   ruido inducido en modo común. Sin trenzado, RS-485 pierde gran parte de
   su inmunidad.

2. **Resistencias de terminación.** Colocar 120 Ω entre A y B en **cada
   extremo** del cable. Sin estas resistencias, las señales rebotan en los
   extremos del cable (reflexiones) y se corrompen.

3. **Resistencias de polarización (bias).** En estado de reposo (idle), el
   bus puede quedar flotando. Se recomiendan resistencias pull-up en A y
   pull-down en B (típicamente 560 Ω) para definir un estado de reposo.

4. **NO conectar los GND directos entre edificios.** Esta es la regla más
   importante. Si los dos sistemas no comparten una alimentación común, NO
   se debe tirar un cable GND-GND largo entre los edificios porque crearía
   un bucle de tierra. RS-485 tolera diferencias de potencial de modo común
   de hasta ±7 V sin problema, gracias a su señalización diferencial.

5. **Aislamiento galvánico (opcional pero recomendado).** Para máxima
   seguridad ante diferencias de potencial entre edificios o tormentas, se
   pueden usar **transceivers RS-485 aislados** (como ADM2587E) o agregar
   optoacopladores en las señales TTL antes del MAX485. Esto evita que un
   problema eléctrico en un edificio dañe al equipo del otro.

6. **Protección contra sobrevoltaje.** Diodos TVS (transient voltage
   suppressors) en las líneas A y B para proteger contra picos inducidos
   por descargas atmosféricas cercanas.

7. **Velocidad razonable.** Aunque RS-485 soporta hasta 10 Mbps, para este
   caso 9600 o 19200 baud son más que suficientes y dan mayor robustez.

---

## 6. Alternativas

Si por alguna razón no se quiere usar RS-485, existen otras opciones:

- **RS-232 con MAX232**: viable hasta 15 m, pero a 20 m está ya en el límite
  o ligeramente por fuera del estándar. Susceptible a ruido y requiere
  referencia de masa común (lo cual es problemático entre edificios).

- **Fibra óptica con conversores TTL/fibra**: solución más cara pero
  ofrece aislamiento galvánico total e inmunidad absoluta a EMI. Ideal en
  ambientes industriales severos o si hay riesgo eléctrico.

- **Comunicación inalámbrica** (módulos LoRa, nRF24L01, ESP-NOW): elimina
  el cable por completo. Para 20 metros entre edificios es perfectamente
  viable con módulos económicos. Cambia el problema de "instalar cable" por
  "lidiar con interferencia inalámbrica y latencia variable".

---

## 7. Conclusión

**No es factible** implementar la Parte II directamente con cable AWG 24 a
20 metros entre edificios usando señalización TTL nativa del Arduino. Los
problemas principales son:

1. Degradación de la señal por capacitancia parásita del cable.
2. Alta susceptibilidad al ruido electromagnético.
3. Imposibilidad práctica de tener una referencia de masa común segura
   entre edificios distintos.

La **modificación mínima requerida** para que el sistema funcione es agregar
**un par de transceivers RS-485 (MAX485)** y usar **cable de par trenzado**
con **resistencias de terminación de 120 Ω**. Con este cambio, la
comunicación es robusta hasta 1200 m según el estándar TIA/EIA-485, por lo
que 20 m queda holgadamente dentro del rango operativo.

El cambio en software es mínimo: solo se debe controlar el pin DE/RE del
transceiver para conmutar entre transmisión y recepción (RS-485 es
half-duplex), lo cual se hace antes y después de cada `print` con un par
de `digitalWrite`.

---

## Referencias

- AWG wire data: HyperPhysics, Georgia State University
- Texas Instruments / DigiKey: "UARTs Provide Reliable Serial Communication"
- TIA/EIA-485-A standard (RS-485)
- Maxim Integrated MAX485 datasheet
