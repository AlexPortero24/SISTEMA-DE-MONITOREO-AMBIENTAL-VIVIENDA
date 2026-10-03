/**
 * ============================================================================
 * PROYECTO: Sistema Inteligente de Monitoreo Ambiental para Vivienda
 * ASIGNATURA: Sistemas Digitales - Proyecto Final UEA
 * PLATAFORMA: Arduino UNO / Simulación en Wokwi
 * COMUNICACIÓN: I2C (PCF8574 - LCD 16x2) y UART (Serial 9600 bps)
 * CONTROL: Máquina de Estados Finitos (FSM) tipo Moore no bloqueante
 * ============================================================================
 */

#include "Wire.h"
#include "LiquidCrystal_I2C.h"
#include "DHT.h"
// ----------------------------------------------------------------------------
// 1. ASIGNACIÓN HARDWARE DE PINES
// ----------------------------------------------------------------------------
#define PIN_DHT        2    // Bus digital de datos (One-Wire) para sensor DHT22
#define TIPO_DHT       DHT22
#define PIN_LDR        A0   // Canal analógico del divisor resistivo de iluminación
#define PIN_LED_VERDE  6    // Salida lógica para indicador de confort
#define PIN_LED_ROJO   7    // Salida lógica para indicador de alerta crítica
#define PIN_BUZZER     8    // Salida digital para señal acústica

// ----------------------------------------------------------------------------
// 2. PARÁMETROS CRÍTICOS Y RANGOS DE CONFORT RESIDENCIAL
// ----------------------------------------------------------------------------
const float TEMP_CONFORT_MIN = 18.0; // Límite inferior de temperatura normal (°C)
const float TEMP_CONFORT_MAX = 25.0; // Límite superior de temperatura normal (°C)
const float TEMP_CRITICA_MAX = 29.0; // Umbral térmico de calor crítico (°C)
const float TEMP_CRITICA_MIN = 15.0; // Umbral térmico de frío extremo (°C)

const float HUM_CONFORT_MIN  = 40.0; // Límite inferior de humedad normal (%)
const float HUM_CONFORT_MAX  = 60.0; // Límite superior de humedad normal (%)
const float HUM_CRITICA_MAX  = 70.0; // Riesgo crítico de proliferación de moho (%)
const float HUM_CRITICA_MIN  = 30.0; // Riesgo crítico de sequedad mucosal (%)

const int   LUZ_CONFORT_MIN  = 400;  // Nivel ADC mínimo para iluminación adecuada
const int   LUZ_PENUMBRA_MIN = 150;  // Umbral inferior antes de oscuridad crítica

// ----------------------------------------------------------------------------
// 3. DEFINICIÓN FORMAL DEL MODELO DE ESTADOS (FSM TIPO MOORE)
// ----------------------------------------------------------------------------
enum EstadoFSM {
  ESTADO_INICIAL,      // Rutina de inicialización y chequeo de hardware
  ESTADO_CONFORT,      // Parámetros dentro de rangos normales de habitabilidad
  ESTADO_ADVERTENCIA,  // Desviaciones moderadas que requieren ventilación/luz
  ESTADO_ALARMA        // Parámetros críticos que amenazan la salud o infraestructura
};

EstadoFSM estadoActual = ESTADO_INICIAL;

// ----------------------------------------------------------------------------
// 4. TEMPORIZACIÓN MEDIANTE CONTADORES DE SOFTWARE (millis)
// ----------------------------------------------------------------------------
unsigned long tiempoPrevioMuestreo = 0;
const unsigned long INTERVALO_MUESTREO = 2000; // Periodo de muestreo: 2 segundos

unsigned long tiempoPrevioOscilador = 0;
const unsigned long INTERVALO_OSCILADOR = 500; // Conmutación para alerta: 1 Hz (500 ms)
bool faseOscilador = false;

// ----------------------------------------------------------------------------
// 5. OBJETOS DE PERIFÉRICOS
// ----------------------------------------------------------------------------
DHT sensorDHT(PIN_DHT, TIPO_DHT);
LiquidCrystal_I2C pantallaLCD(0x27, 16, 2); // Dirección I2C fija en 0x27

// ----------------------------------------------------------------------------
// 6. PROTOTIPOS DE FUNCIONES MODULARES
// ----------------------------------------------------------------------------
void adquirirDatosSensores(float &temperatura, float &humedad, int &luminosidad);
EstadoFSM evaluarCondicionesAmbientales(float t, float h, int l);
void gestionarActuadoresMoore(EstadoFSM estado);
void refrescarPantallaLCD(float t, float h, int l, EstadoFSM estado);
void emitirTelemetriaUART(float t, float h, int l, EstadoFSM estado);

// ----------------------------------------------------------------------------
// 7. INICIALIZACIÓN DEL SISTEMA (SETUP)
// ----------------------------------------------------------------------------
void setup() {
  // Configuración de puertos de salida digital
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Inicialización de actuadores en nivel inactivo
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Configuración de la interfaz serial de comunicaciones
  Serial.begin(9600);
  Serial.println(F("=================================================="));
  Serial.println(F("  SISTEMA INTELIGENTE DE MONITOREO AMBIENTAL      "));
  Serial.println(F("           CONTROL DE VIVIENDA - UEA              "));
  Serial.println(F("=================================================="));

  // Inicialización del bus I2C y display alfanumérico
  pantallaLCD.init();
  pantallaLCD.backlight();
  pantallaLCD.clear();
  pantallaLCD.setCursor(0, 0);
  pantallaLCD.print(F("MONITOR VIVIENDA"));
  pantallaLCD.setCursor(0, 1);
  pantallaLCD.print(F("INICIANDO HARDWARE"));

  // Arranque del sensor digital ambiental
  sensorDHT.begin();
  
  // Retardo controlado inicial para estabilización de voltajes de sensado
  delay(1500);
  
  pantallaLCD.clear();
  estadoActual = ESTADO_CONFORT; // Transición automática al lazo operacional
}

// ----------------------------------------------------------------------------
// 8. LAZO PRINCIPAL DE EJECUCIÓN CONCURRENTE (LOOP)
// ----------------------------------------------------------------------------
void loop() {
  unsigned long tiempoActual = millis();

  // Generación de base de tiempo asíncrona para señales de alerta (1 Hz)
  if (tiempoActual - tiempoPrevioOscilador >= INTERVALO_OSCILADOR) {
    tiempoPrevioOscilador = tiempoActual;
    faseOscilador = !faseOscilador;
  }

  // Tarea de adquisición de datos y evaluación de la máquina de estados
  if (tiempoActual - tiempoPrevioMuestreo >= INTERVALO_MUESTREO) {
    tiempoPrevioMuestreo = tiempoActual;

    float temperaturaAmbiente = 0.0;
    float humedadRelativa = 0.0;
    int nivelLuminosidad = 0;

    // Adquisición física desde los transductores
    adquirirDatosSensores(temperaturaAmbiente, humedadRelativa, nivelLuminosidad);

    // Evaluación lógica de transiciones de estado
    estadoActual = evaluarCondicionesAmbientales(temperaturaAmbiente, humedadRelativa, nivelLuminosidad);

    // Actualización de interfaces de usuario y diagnóstico
    refrescarPantallaLCD(temperaturaAmbiente, humedadRelativa, nivelLuminosidad, estadoActual);
    emitirTelemetriaUART(temperaturaAmbiente, humedadRelativa, nivelLuminosidad, estadoActual);
  }

  // Ejecución continua de salidas físicas conforme al estado actual de Moore
  gestionarActuadoresMoore(estadoActual);
}

// ----------------------------------------------------------------------------
// 9. IMPLEMENTACIÓN DE FUNCIONES DE SOPORTE
// ----------------------------------------------------------------------------

/**
 * Lee las magnitudes físicas de los transductores y valida errores de bus.
 */
void adquirirDatosSensores(float &temperatura, float &humedad, int &luminosidad) {
  temperatura = sensorDHT.readTemperature();
  humedad = sensorDHT.readHumidity();
  luminosidad = analogRead(PIN_LDR);

  // Manejo preventivo de fallos físicos o pérdida de trama de bits
  if (isnan(temperatura) || isnan(humedad)) {
    Serial.println(F("[ERROR]: Pérdida de comunicación en bus digital DHT22."));
    temperatura = -99.0;
    humedad = -99.0;
  }
}

/**
 * Evalúa las condiciones ambientales respecto a los umbrales de habitabilidad.
 */
EstadoFSM evaluarCondicionesAmbientales(float t, float h, int l) {
  // Ante falla de adquisición de sensores, se fuerza estado de advertencia
  if (t == -99.0 || h == -99.0) {
    return ESTADO_ADVERTENCIA;
  }

  // Condición de Alarma Crítica (Riesgo grave para personas o vivienda)
  if (t > TEMP_CRITICA_MAX || t < TEMP_CRITICA_MIN ||
      h > HUM_CRITICA_MAX || h < HUM_CRITICA_MIN) {
    return ESTADO_ALARMA;
  }

  // Condición de Advertencia (Fuera de los rangos óptimos de confort)
  if ((t > TEMP_CONFORT_MAX && t <= TEMP_CRITICA_MAX) ||
      (t >= TEMP_CRITICA_MIN && t < TEMP_CONFORT_MIN) ||
      (h > HUM_CONFORT_MAX && h <= HUM_CRITICA_MAX) ||
      (h >= HUM_CRITICA_MIN && h < HUM_CONFORT_MIN) ||
      (l < LUZ_CONFORT_MIN)) {
    return ESTADO_ADVERTENCIA;
  }

  // Si todas las variables cumplen simultáneamente los criterios de diseño
  return ESTADO_CONFORT;
}

/**
 * Conmuta los actuadores físicos aplicando el principio de la máquina de Moore.
 */
void gestionarActuadoresMoore(EstadoFSM estado) {
  switch (estado) {
    case ESTADO_CONFORT:
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case ESTADO_ADVERTENCIA:
      // Indicador verde destellante sin contaminación acústica
      digitalWrite(PIN_LED_VERDE, faseOscilador ? HIGH : LOW);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case ESTADO_ALARMA:
      // Indicador rojo activo continuo y alarma acústica intermitente
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_ROJO, HIGH);
      digitalWrite(PIN_BUZZER, faseOscilador ? HIGH : LOW);
      break;

    default:
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;
  }
}

/**
 * Despliega las variables numéricas y el estado en la pantalla LCD 16x2.
 */
void refrescarPantallaLCD(float t, float h, int l, EstadoFSM estado) {
  // Fila 0: Magnitudes de Temperatura y Humedad
  pantallaLCD.setCursor(0, 0);
  if (t == -99.0) {
    pantallaLCD.print(F("Err Sensor DHT "));
  } else {
    pantallaLCD.print(F("T:"));
    pantallaLCD.print((int)t);
    pantallaLCD.print(F("C "));
    pantallaLCD.print(F("H:"));
    pantallaLCD.print((int)h);
    pantallaLCD.print(F("%   "));
  }

  // Fila 1: Nivel de Luz y Estado FSM
  pantallaLCD.setCursor(0, 1);
  pantallaLCD.print(F("L:"));
  pantallaLCD.print(l);
  
  if (l < 100) pantallaLCD.print(F(" "));
  pantallaLCD.print(F(" "));

  switch (estado) {
    case ESTADO_CONFORT:
      pantallaLCD.print(F("E:Confort"));
      break;
    case ESTADO_ADVERTENCIA:
      pantallaLCD.print(F("E:Advert."));
      break;
    case ESTADO_ALARMA:
      pantallaLCD.print(F("E:ALARMA!"));
      break;
    default:
      pantallaLCD.print(F("E:Inic.  "));
      break;
  }
}

/**
 * Transmite la trama de telemetría por el puerto serial para supervisión remota.
 */
void emitirTelemetriaUART(float t, float h, int l, EstadoFSM estado) {
  Serial.print(F("T: "));
  Serial.print(t, 1);
  Serial.print(F(" C | H: "));
  Serial.print(h, 1);
  Serial.print(F(" % | Luz: "));
  Serial.print(l);
  Serial.print(F(" | Estado: "));

  switch (estado) {
    case ESTADO_CONFORT:
      Serial.println(F("CONFORT (Condiciones Ideales)"));
      break;
    case ESTADO_ADVERTENCIA:
      Serial.println(F("ADVERTENCIA (Atencion en Vivienda)"));
      break;
    case ESTADO_ALARMA:
      Serial.println(F("ALARMA CRITICA (Riesgo en Vivienda)"));
      break;
    default:
      Serial.println(F("INICIAL"));
      break;
  }
}