/*
  ============================================================================
   KIT IoT ESP32-C3 Mini — SENAI São Carlos · firmware v1.5 (página no kit + WebSocket)
  ============================================================================
   Lê os sensores do kit, mostra as leituras no LCD, envia a telemetria e
   AGORA TAMBÉM RECEBE COMANDOS para mover o servo (MQTT e WebSocket).

   v1.8 · CONECTIVIDADE ("Kit offline" no site):
     - O kit diz POR QUE caiu: motivo do último reinício (ex.: queda de tensão = servos puxando
       mais corrente que a fonte), motivo da queda do Wi-Fi e erro do MQTT aparecem no LCD,
       no Monitor Serial e no site (mensagem "hello").
     - Telemetria traz o sinal do Wi-Fi (rssi, dBm): o site mostra ao lado de "Kit online".
     - Wi-Fi: nova tentativa a cada 10 s (antes 20 s) e, após 3 falhas, reinicia o rádio do zero.
     - Servos com velocidade máxima (SERVO_VEL_MAX): um salto grande dos 5 juntos (botão de pose)
       puxa o pico de corrente de todos ao mesmo tempo, derruba a tensão e REINICIA o ESP32.
     - "axes_rel" = posição pedida (o site sincroniza por ela) e "axes_atual" = posição em que o
       servo está agora (o site desenha a linha tracejada).
     - Sem esperar 3 s pelo Monitor Serial quando o USB não está ligado num PC (volta mais rápido).

   v1.7 · MAIS RÁPIDO E SINCRONIZADO COM O SITE:
     - Serial que não trava: com o kit no USB do PC e o Monitor Serial fechado, cada linha de log
       podia parar o laço por até 100 ms (USB-CDC). Agora o log é descartado em vez de esperar,
       e o servo não escreve mais uma linha a cada movimento.
     - Telemetria WebSocket 20×/s (antes 10×/s) e resposta a comando em até 25 ms (antes 30 ms).
     - Telemetria traz "axes_rel": o valor de cada junta na escala do site (−90…90). O site usa isso
       para acompanhar o kit (encoder, outro site aberto, E-STOP, kit que reiniciou).
     - Partida escalonada: os servos ligam um por vez (SERVO_PARTIDA_MS) para não somar o pico de
       corrente dos 5 ao mesmo tempo, que pode reiniciar o ESP32.

   v1.6 · OS 5 SERVOS DO BRAÇO:
     - Antes só a BASE tinha pino (os outros eixos estavam em −1 e o comando do site era ignorado).
     - Ombro GPIO3 · Cotovelo GPIO4 · Punho GPIO20 · Garra GPIO21 (veja "GPIOs" abaixo).
     - O "hello" (WebSocket, MQTT e /status) informa "pins":{"waist":10,...}: o site mostra o GPIO de cada eixo.

   v1.5 · CONTROLE LOCAL DIRETO (método mais rápido):
     - O kit SERVE A PÁGINA do ATLAS: abra http://kit-iot.local (ou http://<IP do LCD>) na mesma rede.
       A página fala com o kit por WebSocket (porta 81): latência de poucos ms, sem internet e sem broker.
     - A mesma página aberta como arquivo no computador também usa o WebSocket.
     - O MQTT continua ativo como reserva / acesso remoto (modo "Automático" da página).
     - WebSocket aceita E-STOP ({"estop":1} / {"estop":0}) e ping ({"ping":1} → {"type":"pong"}).
     - Telemetria passa a informar "estop":true/false (a página sincroniza o botão).
     - Heartbeat no WebSocket: cliente que sumiu (Wi-Fi do celular caiu) é desconectado em ~10 s.
     - A página fica em pagina.h (gerado a partir do HTML por gerar_pagina.py).

   v1.4 · CONEXÃO MQTT OTIMIZADA (o kit caía do broker):
     - Wi-Fi sem economia de energia (WiFi.setSleep(false)): era a causa nº 1 de queda/atraso.
     - Telemetria MQTT por EVENTO: só publica quando algo muda (no máx. 4×/s) + 1 pulso por segundo.
       Antes eram 5–10 mensagens/s por TLS, o que estoura a fila do broker público.
     - Comandos do site são "coalescidos": se chegam vários, aplica só o último (sem fila acumulada).
     - Keep-alive 30 s, timeout de socket 10 s (TLS em rede lenta), buffer liberado a cada reconexão.
     - Reconexão com espera crescente (2 s → 30 s) e motivo da queda no Monitor Serial.
     - Se o Wi-Fi cai, o socket TLS é fechado na hora (antes ficava "zumbi" até o keep-alive).
     - 3 publicações seguidas falhando = reconecta (detecta link morto rápido).
     - E-STOP retido: ao reconectar, o kit recebe o estado atual do E-STOP.
     - Serial não imprime mais cada mensagem recebida (USB-CDC bloqueava a tarefa do MQTT).

   O QUE MUDOU EM RELAÇÃO À v1.0 (era por isso que o servo não mexia pelo site):
     1. O kit assina o tópico de comando  <MQTT_TOPIC>/cmd  e o <MQTT_TOPIC>/estop.
     2. Callback do MQTT + comando por WebSocket, ambos com validação (0–180°).
     3. Broker selecionável em BROKER_PUBLICO: 1 = shiftr.io (o site alcança), 0 = HiveMQ do PC.
     4. Conexão MQTT aceita broker sem usuário/senha (HiveMQ padrão).
     5. v1.2: entende o payload do site ATLAS {"waist","shoulder","elbow","pitch","grip"}
        (graus relativos à pose zero) e o tópico <MQTT_TOPIC>/estop (E-STOP).
        (até a v1.5 só o eixo waist, BASE no GPIO10, tinha servo; a v1.6 liga os 5)

   ---------------------------------------------------------------------------
   Comandos aceitos (MQTT em <MQTT_TOPIC>/cmd · WebSocket em texto)
   ---------------------------------------------------------------------------
     {"waist":-20,"shoulder":10,"elbow":0,"pitch":5,"grip":30}   ← formato do site ATLAS
        graus RELATIVOS à pose zero: servo = CMD_CENTRO + CMD_SINAL × valor (limitado a 0–180).
        Pode vir só com os eixos que mudaram. Eixo sem pino é apenas registrado no Serial.
     90                       → ângulo absoluto da base (0–180)
     {"angle":45}             → idem, em JSON
     <MQTT_TOPIC>/estop       "1" congela os servos e ignora comandos · "0" libera
     (o E-STOP é só software: não corta a energia dos servos; use parada física no hardware)
   ---------------------------------------------------------------------------
   GPIOs — conforme gravado na tampa de acrílico
   ---------------------------------------------------------------------------
     GPIO0   SCL (I2C)        MPU-6050 + LCD 16x2 (barramento compartilhado)
     GPIO1   SDA (I2C)        MPU-6050 + LCD 16x2
     GPIO2   não usar         (strapping)
     GPIO3   PWM servo OMBRO  (era o Echo do HC-SR04: DESCONECTE o sensor antes de ligar o servo)
     GPIO4   PWM servo COTOVELO (era o Trigger do HC-SR04)
     GPIO5   CLK (KY-040)
     GPIO6   DT  (KY-040)
     GPIO7   Botão SW (KY-040) — pull-up interno
     GPIO8   não usar         (strapping / LED azul da placa)
     GPIO9   não usar         (strapping / botão BOOT)
     GPIO10  PWM servo BASE (SG90) — alimente o servo pelo 5 V, nunca pelo 3,3 V
     GPIO20  PWM servo PUNHO  (pino RX da placa)
     GPIO21  PWM servo GARRA  (pino TX da placa)
     Servos: o fio de sinal vai no GPIO; 5 V e GND vêm da fonte externa, com o GND ligado ao GND do ESP32.

   ---------------------------------------------------------------------------
   Comportamento no kit
   ---------------------------------------------------------------------------
     - Girar o encoder muda a contagem de passos e move o servo (±5° por passo, 0–180°).
     - Clicar no encoder recentra o servo em 90°.
     - Comando MQTT/WebSocket move o servo direto para o ângulo pedido.
       (para o servo obedecer SÓ à rede, ponha SERVO_SEGUE_ENCODER 0)
     - O LCD alterna a cada 3 s: juntas (Base, Omb, Cot, Pun) · Garra/encoder · acelerômetro · rede.
       Ao girar, clicar ou receber comando, a primeira tela aparece na hora.

   ---------------------------------------------------------------------------
   Comunicação (as duas vias ao mesmo tempo, com o mesmo JSON de telemetria)
   ---------------------------------------------------------------------------
     MQTT       <MQTT_TOPIC>/up      telemetria JSON (ao mudar, até 4×/s; senão 1×/s) e "hello" (a cada 30 s)
                <MQTT_TOPIC>/online  "1" retido ao conectar; "0" é o last will (queda do kit)
                <MQTT_TOPIC>/cmd     comandos de servo (entrada)
     WebSocket  ws://<IP do kit>:81 ou ws://kit-iot.local:81 — telemetria 20×/s e comandos.
                Exige a página aberta por http:// (a servida pelo kit) ou como arquivo local.
     HTTP       http://kit-iot.local → página do ATLAS · /status → JSON de identificação

     Telemetria (campos ausentes = sensor indisponível; v1.3 não envia distância nem temperatura):
       {"type":"telemetry","seq":12,"uptime_ms":34567,
        "encoder":{"count":12,"dir":1,"angle_deg":216,"button":false,"clicks":3},
        "distance_cm":23.4,
        "accel_g":{"x":0.012,"y":-0.020,"z":0.991},
        "gyro_dps":{"x":0.4,"y":-0.3,"z":0.1},
        "servo":{"cmd_deg":95},
        "sensors":{"encoder":true,"mpu6050":true,"hcsr04":true,"lcd":true,"servo":true}}

   ---------------------------------------------------------------------------
   Arduino IDE
   ---------------------------------------------------------------------------
     Placa: "ESP32C3 Dev Module" · USB CDC On Boot: "Enabled" (Monitor Serial a 115200)
     A pasta do sketch deve se chamar "kit_iot" (igual ao nome deste .ino).
     Bibliotecas (Gerenciador de Bibliotecas):
       - "WebSockets"   de Markus Sattler
       - "PubSubClient" de Nick O'Leary
     O MPU-6050 e o LCD usam drivers próprios neste arquivo (sem bibliotecas extras).
     Wi-Fi: copie secrets.example.h para secrets.h (mesma pasta) e preencha.
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <stdarg.h>
#include <esp_system.h>

// ============================================================ CONFIGURAÇÃO ===
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#warning "secrets.h não encontrado: copie secrets.example.h para secrets.h e preencha o Wi-Fi"
#define WIFI_SSID "WIFI_IOT_CFP601"
#define WIFI_PASSWORD "iot@senai601"
#endif

#define FW_VERSAO "kit-iot 1.8"
const char* KIT_NOME = "kit-iot";            // mDNS: kit-iot.local

// Liga/desliga cada parte (útil para testar um periférico por vez).
#define ENABLE_ENCODER 1
#define ENABLE_HCSR04 0   // v1.3: distância removida (sem leitura, sem LCD, sem JSON)
#define ENABLE_MPU6050 1
#define ENABLE_LCD 1
#define ENABLE_SERVO 1
#define ENABLE_MQTT 1
#define ENABLE_WEBSOCKET 1

// ---- MQTT ----
// BROKER_PUBLICO 1 = broker público shiftr.io: o site ATLAS (na internet) e o kit alcançam.
//                    Qualquer pessoa que souber o tópico pode ler e COMANDAR: use um tópico único.
// BROKER_PUBLICO 0 = HiveMQ instalado no PC (10.110.6.8:1883): o site no manus.space NÃO alcança.
#define BROKER_PUBLICO 1
#if BROKER_PUBLICO
#define MQTT_HOST_PADRAO "public.cloud.shiftr.io"
#define MQTT_TLS 1                     // porta 8883 com TLS (o site usa wss://public.cloud.shiftr.io:443)
#define MQTT_USER_PADRAO "public"
#define MQTT_PASS_PADRAO "public"
#else
#define MQTT_HOST_PADRAO "10.110.6.8"
#define MQTT_TLS 0                     // porta 1883 sem criptografia
#define MQTT_USER_PADRAO ""
#define MQTT_PASS_PADRAO ""
#endif
#ifndef MQTT_HOST_CFG
#define MQTT_HOST_CFG MQTT_HOST_PADRAO
#endif
#ifndef MQTT_USER
#define MQTT_USER MQTT_USER_PADRAO
#define MQTT_PASS MQTT_PASS_PADRAO
#endif
const char* MQTT_HOST = MQTT_HOST_CFG;
// TROQUE o final por um nome só seu (letras e números) e use EXATAMENTE o mesmo no campo
// "Prefixo do tópico" do site. Em broker público, "kit-iot-01" é de todo mundo.
const char* MQTT_TOPIC = "senai-sao-carlos/nexusatlas-x7k2";
// Tópico de comando extra, se o site publicar em outro lugar (veja o "Log MQTT" do site). "" = nenhum.
const char* MQTT_CMD_EXTRA = "";
#if MQTT_TLS
const uint16_t MQTT_PORT = 8883;
#else
const uint16_t MQTT_PORT = 1883;
#endif
const uint32_t MQTT_RETRY_MIN_MS = 1500;    // 1ª nova tentativa de conexão com o broker
const uint32_t MQTT_RETRY_MAX_MS = 20000;   // espera máxima entre tentativas (cresce 2×)
const uint16_t MQTT_KEEPALIVE_S = 30;       // ping ao broker; 15 s era curto demais para TLS em Wi-Fi fraco
const uint16_t MQTT_SOCKET_TIMEOUT_S = 10;
const uint8_t MQTT_FALHAS_PUBLICAR = 3;     // publicações seguidas falhando → força reconexão

// ---- WebSocket local e página ----
const uint16_t WS_PORT = 81;
const uint16_t HTTP_PORT = 80;              // http://kit-iot.local → página do ATLAS
#define ENABLE_PAGINA 1                     // 0 = não serve a página (só WebSocket/MQTT)
#if ENABLE_PAGINA
#include "pagina.h"                         // PAGINA_HTML[] (gerado por gerar_pagina.py)
#endif

// ---- Taxas de envio ----
const uint32_t WS_PERIODO_MS = 25;         // 40×/s: ciclo de controle de 25 ms
const uint32_t MQTT_PERIODO_MS = 1000;     // pulso: 1×/s mesmo sem mudança (o site usa para "Kit online")
const uint32_t WS_URGENTE_MS = 25;         // comando: envia no próximo ciclo de 25 ms
const uint32_t MQTT_URGENTE_MS = 250;      // com mudança (servo/encoder): no máx. 4×/s pela internet
const uint32_t HELLO_PERIODO_MS = 30000;

// ---- Pinos (tampa do kit) ----
const int PIN_SCL = 0;
const int PIN_SDA = 1;
const int PIN_ECHO = 3;
const int PIN_TRIG = 4;
const int PIN_ENC_CLK = 5;
const int PIN_ENC_DT = 6;
const int PIN_ENC_SW = 7;
const int PIN_SERVO = 10;            // eixo 0 · BASE (waist)
// Demais eixos do braço: GPIO de cada servo. −1 = não ligado (o eixo é ignorado).
// Não use GPIO 2, 8 e 9 (strapping). Servos MG996R: fonte externa de 5 V, GND comum com o ESP32.
// v1.6: GPIO3/4 eram do HC-SR04 (desligado desde a v1.3) e GPIO20/21 são RX/TX da UART0
// (livres com "USB CDC On Boot: Enabled", o Serial sai pela USB).
const int PIN_SERVO_OMBRO = 3;       // eixo 1 · shoulder (antigo Echo do HC-SR04)
const int PIN_SERVO_COTOVELO = 4;    // eixo 2 · elbow    (antigo Trigger do HC-SR04)
const int PIN_SERVO_PUNHO = 20;      // eixo 3 · pitch    (RX)
const int PIN_SERVO_GARRA = 21;      // eixo 4 · grip     (TX: a ROM escreve aqui no boot, a garra pode tremer ao ligar)
#if ENABLE_HCSR04
static_assert(PIN_SERVO_OMBRO != PIN_ECHO && PIN_SERVO_OMBRO != PIN_TRIG &&
              PIN_SERVO_COTOVELO != PIN_ECHO && PIN_SERVO_COTOVELO != PIN_TRIG,
              "HC-SR04 e servos no mesmo GPIO: mude os pinos do ombro/cotovelo ou ponha ENABLE_HCSR04 0");
#endif

// ---- Encoder KY-040 ----
const int ENC_TRANSICOES_POR_PASSO = 4;   // KY-040 típico: 4 (use 2 se cada clique contar dois passos)
const int ENC_PASSOS_POR_VOLTA = 20;      // ficha do KY-040: 20 passos por volta
#define ENC_INVERTER 0                    // 1 = inverte o sentido (horário passa a ser −1)
const uint32_t BOTAO_DEBOUNCE_MS = 25;

// ---- Servo SG90 ----
#define SERVO_SEGUE_ENCODER 0             // movimento somente pela rede; encoder não comanda o braço
const int SERVO_PASSO_GRAUS = 5;
const int SERVO_MIN = 0;
const int SERVO_MAX = 180;
const int SERVO_CENTRO = 90;
const int SERVO_PULSO_MIN_US = 500;       // SG90: 0,5 ms = 0°
const int SERVO_PULSO_MAX_US = 2400;      //       2,4 ms = 180°
const int SERVO_BITS = 14;                // resolução do PWM (LEDC) a 50 Hz
const uint32_t SERVO_PARTIDA_MS = 250;    // intervalo entre ligar um servo e o próximo (pico de corrente)
// Velocidade máxima de cada servo em graus/s (0 = sem limite: vai direto ao ângulo pedido).
// Arrastar o slider fica igual; o que muda é o salto grande (botão de pose), que sem limite
// faz os 5 servos puxarem a corrente de partida juntos, a tensão cai e o ESP32 reinicia.
const int SERVO_VEL_MAX = 600;
const uint32_t SERVO_PASSO_MS = 20;       // atualização do movimento = período do PWM (50 Hz)

// ---- HC-SR04 ----
const uint32_t US_PERIODO_MS = 60;        // mínimo recomendado entre disparos
const uint32_t US_TIMEOUT_US = 30000;     // eco mais longo que isso = nada à frente
const float US_MIN_CM = 2.0f;
const float US_MAX_CM = 400.0f;

// ---- MPU-6050 ----
const uint32_t MPU_PERIODO_MS = 20;       // 50 leituras por segundo
#define GYRO_CALIBRAR 1                   // mede o desvio do giroscópio ao ligar (deixe o kit parado ~1 s)

// ---- LCD 16x2 ----
const uint32_t LCD_TELA_MS = 3000;        // tempo de cada tela
const uint32_t LCD_ATIVIDADE_MS = 4000;   // após giro/clique/comando, mantém a tela do encoder
const uint32_t LCD_INICIO_MS = 6000;      // ao ligar: versão + motivo do último reinício

// ---- Wi-Fi ----
// Muitas placas ESP32-C3 Super Mini só conectam com a potência de transmissão reduzida
// (limitação da antena da placa). 0 = potência padrão.
#define WIFI_POTENCIA_REDUZIDA 0
// Se o RSSI no Monitor Serial estiver pior que −75 dBm, teste WIFI_POWER_11dBm ou WIFI_POWER_13dBm.
#define WIFI_TX_POWER WIFI_POWER_19_5dBm

#define DEBUG 1
const uint32_t CMD_LOG_MS = 500;         // intervalo mínimo entre logs de comando no Serial (0 = mostra todos)
const uint32_t STATUS_LOG_MS = 5000;      // resumo periódico no Serial (0 = desliga)
#if DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

// ============================================================ ESTADO ===
// Fotografia dos dados, montada no laço principal e lida pela tarefa do MQTT.
struct Snap {
  bool encOk; long count; int dir; int angle; bool button; uint32_t clicks;
  bool usOk; float distance;                       // < 0 = sem eco
  bool mpuOk; float ax, ay, az, gx, gy, gz, temp;
  bool lcdOk;
  bool servoOk; int servoDeg;
  int rssi;                                        // sinal do Wi-Fi (dBm); 0 = sem Wi-Fi
  uint32_t uptime;
};
Snap shared = {};
portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;

// Monta JSON num buffer fixo, sem estourar o tamanho (fica aqui em cima por causa dos
// protótipos que o Arduino gera automaticamente).
struct Json {
  char* buf; size_t cap; size_t len;
  Json(char* b, size_t c) : buf(b), cap(c), len(0) { buf[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
    if (n > 0) len = (len + n < cap - 1) ? len + n : cap - 1;
  }
};

volatile bool wsUrgente = false, mqttUrgente = false;   // giro/clique/comando: enviar já
volatile bool mqttOk = false;
volatile bool estopParaMqtt = false;      // E-STOP mudou pelo WebSocket: publicar retido no MQTT
// Diagnóstico da conexão (LCD, Serial e "hello" do site)
volatile uint16_t wifiFalhas = 0;         // desconexões/tentativas frustradas do Wi-Fi desde que ligou
volatile uint8_t wifiMotivo = 0;          // motivo da última (código do ESP-IDF: 201 = rede não achada, 15/202 = senha…)
volatile int mqttEstado = 0;              // último mqtt.state() com erro (−2 = rede/TLS, −4 = timeout…)
volatile uint16_t mqttQuedas = 0;
volatile uint32_t ultimaAtividade = 0;
bool wifiUp = false, redeIniciada = false;

Snap readShared() {
  Snap s;
  portENTER_CRITICAL(&snapMux);
  s = shared;
  portEXIT_CRITICAL(&snapMux);
  return s;
}

void atividade(uint32_t now) {
  ultimaAtividade = now;
  wsUrgente = true;
  mqttUrgente = true;
}

// ============================================================ ENCODER KY-040 ===
// Decodificação em quadratura: um passo só é contado quando o eixo volta à posição de
// repouso, o que elimina a trepidação dos contatos.
volatile long encPassos = 0;
volatile int8_t encAcum = 0;
volatile uint8_t encPrev = 3;
portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR isrEncoder() {
  uint8_t cur = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  portENTER_CRITICAL_ISR(&encMux);
  encAcum += QDEC[(encPrev << 2) | cur];
  encPrev = cur;
  bool repouso = cur == 3 || (ENC_TRANSICOES_POR_PASSO == 2 && cur == 0);
  if (repouso) {
    if (encAcum >= 2) encPassos = encPassos + 1;
    else if (encAcum <= -2) encPassos = encPassos - 1;
    encAcum = 0;
  }
  portEXIT_CRITICAL_ISR(&encMux);
}

bool encOk = false;
long encCount = 0;
int encDir = 0;
bool btnPressed = false;
uint32_t btnClicks = 0;

void servoMover(int delta);
void servoPara(int graus);

void setupEncoder() {
#if ENABLE_ENCODER
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);   // o módulo já tem pull-ups em CLK/DT
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);    // o SW do KY-040 não tem pull-up no módulo
  encPrev = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT), isrEncoder, CHANGE);
  encOk = true;
#endif
}

void readEncoder(uint32_t now) {
  if (!encOk) return;
  portENTER_CRITICAL(&encMux);
  long passos = encPassos;
  portEXIT_CRITICAL(&encMux);
  if (ENC_INVERTER) passos = -passos;
  if (passos != encCount) {
    long d = passos - encCount;
    encCount = passos;
    encDir = d > 0 ? 1 : -1;
    servoMover((int)d * SERVO_PASSO_GRAUS);
    atividade(now);
    LOG("[KY-040] contagem %ld (%s)\n", encCount, encDir > 0 ? "horário" : "anti-horário");
  }
}

void readBotao(uint32_t now) {
  if (!encOk) return;
  static bool ultimoBruto = HIGH;
  static uint32_t borda = 0;
  bool bruto = digitalRead(PIN_ENC_SW);
  if (bruto != ultimoBruto) {
    ultimoBruto = bruto;
    borda = now;
  }
  if (now - borda < BOTAO_DEBOUNCE_MS) return;
  bool pressionado = bruto == LOW;
  if (pressionado == btnPressed) return;
  btnPressed = pressionado;
  if (pressionado) {
    btnClicks++;
    if (SERVO_SEGUE_ENCODER) servoPara(SERVO_CENTRO);
    LOG("[KY-040] clique %lu\n", (unsigned long)btnClicks);
  }
  atividade(now);
}

int encAngulo() {
  long p = ((encCount % ENC_PASSOS_POR_VOLTA) + ENC_PASSOS_POR_VOLTA) % ENC_PASSOS_POR_VOLTA;
  return (int)(p * 360 / ENC_PASSOS_POR_VOLTA);
}

// ============================================================ SERVOS ===
// Eixo 0 = BASE (waist), o SG90 do kit no GPIO10. Eixos 1–4 (ombro, cotovelo, punho, garra)
// usam os pinos de "Pinos" (−1 = não ligado: o eixo é só registrado no Serial).
const int NUM_EIXOS = 5;
const char* NOME_EIXO[NUM_EIXOS] = {"waist", "shoulder", "elbow", "pitch", "grip"};   // chaves do JSON (site): não mudar
const char* NOME_PT[NUM_EIXOS] = {"Base", "Ombro", "Cotovelo", "Punho", "Garra"};      // nomes mostrados no Serial
const char* NOME_LCD[NUM_EIXOS] = {"Base", "Omb", "Cot", "Pun", "Garra"};             // nomes curtos para o LCD 16x2
volatile int cmdRel[NUM_EIXOS] = {0, 0, 0, 0, 0};   // último comando (graus relativos) de cada junta, mesmo sem servo ligado
const int PINO_EIXO[NUM_EIXOS] = {PIN_SERVO, PIN_SERVO_OMBRO, PIN_SERVO_COTOVELO, PIN_SERVO_PUNHO, PIN_SERVO_GARRA};
// Comando do site = graus RELATIVOS à pose zero do modelo: servo = CENTRO + SINAL × valor.
// Ajuste o centro e o sinal de cada eixo conforme a montagem mecânica (sinal −1 inverte o giro).
const int CMD_CENTRO[NUM_EIXOS] = {90, 90, 90, 90, 90};
const int CMD_SINAL[NUM_EIXOS] = {1, 1, 1, 1, 1};

bool servoOk = false;                       // base configurada
bool eixoAtivo[NUM_EIXOS] = {false, false, false, false, false};
volatile int servoGrausEixo[NUM_EIXOS] = {SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO};   // ângulo que o PWM aplica agora
volatile int servoAlvoEixo[NUM_EIXOS] = {SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO, SERVO_CENTRO};    // ângulo pedido (atualizaServos chega nele)
#define servoGraus servoGrausEixo[0]        // ângulo da base (LCD, telemetria e encoder)
volatile bool estopAtivo = false;           // true = servos congelados, comandos ignorados

// Valor PEDIDO da junta em graus relativos à pose zero (o mesmo número do site).
int valorRel(int i) {
  if (eixoAtivo[i]) return (servoAlvoEixo[i] - CMD_CENTRO[i]) * CMD_SINAL[i];
  return cmdRel[i];
}

// Onde o servo está AGORA (difere do pedido enquanto ele anda limitado por SERVO_VEL_MAX).
int valorAtual(int i) {
  if (eixoAtivo[i]) return (servoGrausEixo[i] - CMD_CENTRO[i]) * CMD_SINAL[i];
  return cmdRel[i];
}

// Uma linha no Monitor Serial com as 5 juntas.
void logJuntas(const char* rotulo) {
  LOG("[%s]", rotulo);
  for (int i = 0; i < NUM_EIXOS; i++)
    LOG("%s %s: %+d°%s", i ? " |" : "", NOME_PT[i], valorRel(i), eixoAtivo[i] ? "" : " (sem servo)");
  LOG("\n");
}

void servoEscreverEixo(int eixo, int graus) {
  long us = map(graus, 0, 180, SERVO_PULSO_MIN_US, SERVO_PULSO_MAX_US);
  uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << SERVO_BITS) - 1) / 20000UL);   // período de 20 ms
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PINO_EIXO[eixo], duty);
#else
  ledcWrite(eixo, duty);                    // canal LEDC = número do eixo
#endif
}

void setupServo() {
#if ENABLE_SERVO
  bool primeiro = true;
  for (int i = 0; i < NUM_EIXOS; i++) {
    if (PINO_EIXO[i] < 0) continue;
    if (!primeiro) delay(SERVO_PARTIDA_MS);   // um servo por vez: os 5 juntos derrubam a fonte
    primeiro = false;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    eixoAtivo[i] = ledcAttach(PINO_EIXO[i], 50, SERVO_BITS);
#else
    ledcSetup(i, 50, SERVO_BITS);
    ledcAttachPin(PINO_EIXO[i], i);
    eixoAtivo[i] = true;
#endif
    if (eixoAtivo[i]) servoEscreverEixo(i, servoGrausEixo[i]);   // ao ligar, vai para o centro
    LOG("[SERVO] %s (GPIO%d): %s\n", NOME_PT[i], PINO_EIXO[i], eixoAtivo[i] ? "PWM ativo (90°)" : "falha ao configurar o PWM");
  }
  servoOk = eixoAtivo[0];
  if (!servoOk) LOG("[SERVO] base (GPIO%d) indisponível\n", PIN_SERVO);
#endif
}

// Só registra o ângulo pedido; quem move o servo é atualizaServos(), no laço principal.
void servoParaEixo(int eixo, int graus) {
  if (eixo < 0 || eixo >= NUM_EIXOS || !eixoAtivo[eixo]) return;
  if (estopAtivo) return;
  servoAlvoEixo[eixo] = constrain(graus, SERVO_MIN, SERVO_MAX);
  // sem log aqui: o site manda até 50 comandos/s; o resumo sai em aplicaComando (limitado por CMD_LOG_MS)
}

void servoPara(int graus) { servoParaEixo(0, graus); }

void servoMover(int delta) {
  if (SERVO_SEGUE_ENCODER) servoPara(servoAlvoEixo[0] + delta);
}

// E-STOP: o servo para onde está (também no meio de um movimento).
void congelaServos() {
  for (int i = 0; i < NUM_EIXOS; i++) servoAlvoEixo[i] = servoGrausEixo[i];
}

// Leva cada servo ao ângulo pedido a no máximo SERVO_VEL_MAX graus/s.
void atualizaServos(uint32_t now) {
  static uint32_t ultimo = 0;
  if (now - ultimo < SERVO_PASSO_MS) return;
  uint32_t dt = ultimo ? now - ultimo : SERVO_PASSO_MS;
  if (dt > 100) dt = 100;                   // laço atrasou: não compensa com um salto
  ultimo = now;
  int passo = SERVO_VEL_MAX > 0 ? (int)(SERVO_VEL_MAX * dt / 1000) : SERVO_MAX;
  if (passo < 1) passo = 1;
  for (int i = 0; i < NUM_EIXOS; i++) {
    if (!eixoAtivo[i]) continue;
    int atual = servoGrausEixo[i], alvo = servoAlvoEixo[i];
    if (atual == alvo) continue;
    atual += constrain(alvo - atual, -passo, passo);
    servoGrausEixo[i] = atual;
    servoEscreverEixo(i, atual);
  }
}

// ---- Comando recebido pela rede (MQTT ou WebSocket) ----
// Procura "chave": número num JSON simples. Aceita sinal e decimais.
bool jsonNumero(const char* msg, const char* chave, float* out) {
  char alvo[24];
  snprintf(alvo, sizeof(alvo), "\"%s\"", chave);
  const char* p = strstr(msg, alvo);
  if (!p) return false;
  p = strchr(p + strlen(alvo), ':');
  if (!p) return false;
  p++;
  while (*p == ' ') p++;
  char* fim = nullptr;
  float v = strtof(p, &fim);
  if (fim == p) return false;
  *out = v;
  return true;
}

// Devolve true se o comando foi entendido. Formatos: ver o cabeçalho deste arquivo.
bool aplicaComando(const char* origem, const char* msg) {
  if (!msg) return false;
  while (*msg == ' ' || *msg == '\t' || *msg == '\r' || *msg == '\n') msg++;

  static uint32_t ultimoLog = 0;            // o site envia até 20×/s: limita o texto no Serial
  uint32_t agora = millis();
  bool logar = (CMD_LOG_MS == 0) || (agora - ultimoLog >= CMD_LOG_MS);
  if (logar) ultimoLog = agora;

  if (estopAtivo) {
    if (logar) LOG("[CMD/%s] ignorado: E-STOP ativo\n", origem);
    return false;
  }

  // 1) payload do site ATLAS: {"waist":..,"shoulder":..,"elbow":..,"pitch":..,"grip":..}
  bool entendeu = false;
  for (int i = 0; i < NUM_EIXOS; i++) {
    float v;
    if (!jsonNumero(msg, NOME_EIXO[i], &v)) continue;
    entendeu = true;
    cmdRel[i] = (int)lroundf(v);
    int alvo = CMD_CENTRO[i] + CMD_SINAL[i] * (int)lroundf(v);
    int lim = constrain(alvo, SERVO_MIN, SERVO_MAX);
    if (!eixoAtivo[i]) {
      if (logar) LOG("[CMD/%s] %s=%.1f° (sem servo ligado nesta junta)\n", origem, NOME_PT[i], v);
      continue;
    }
    servoParaEixo(i, lim);
    if (logar) LOG("[CMD/%s] %s=%.1f° → servo %d°%s\n", origem, NOME_PT[i], v, lim, lim != alvo ? " (limitado)" : "");
  }
  if (entendeu) {
    atividade(agora);
    if (logar) logJuntas("JUNTAS");
    return true;
  }

  // 2) ângulo absoluto da base: "90" ou {"angle":45} (0–180)
  const char* num = nullptr;
  const char* p = strstr(msg, "\"angle\"");
  if (p) {
    p = strchr(p + 7, ':');
    if (p) num = p + 1;
  } else if ((*msg >= '0' && *msg <= '9') || *msg == '-') {
    num = msg;
  }
  if (!num) {
    LOG("[CMD/%s] formato não reconhecido: %s\n", origem, msg);
    return false;
  }
  while (*num == ' ') num++;
  char* fim = nullptr;
  long ang = strtol(num, &fim, 10);
  if (fim == num) {
    LOG("[CMD/%s] ângulo ausente: %s\n", origem, msg);
    return false;
  }
  if (ang < SERVO_MIN || ang > SERVO_MAX) {
    LOG("[CMD/%s] ângulo fora de 0-180 rejeitado: %ld\n", origem, ang);
    return false;
  }
  if (!eixoAtivo[0]) {
    LOG("[CMD/%s] servo da base indisponível\n", origem);
    return false;
  }
  servoParaEixo(0, (int)ang);
  atividade(agora);
  if (logar) LOG("[CMD/%s] base → %ld°\n", origem, ang);
  return true;
}

// ============================================================ HC-SR04 ===
// Medição por interrupção: o laço principal nunca fica esperando o eco.
volatile uint32_t echoInicio = 0, echoLargura = 0;
volatile bool echoPronto = false, echoVisto = false;

void IRAM_ATTR isrEcho() {
  if (digitalRead(PIN_ECHO)) {
    echoInicio = micros();
    echoVisto = true;
  } else if (echoInicio) {
    echoLargura = micros() - echoInicio;
    echoPronto = true;
  }
}

bool usOk = false;
float distanciaCm = -1;

void setupUltrassom() {
#if ENABLE_HCSR04
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ECHO), isrEcho, CHANGE);
#endif
}

float mediana3(const float* v, uint8_t n) {
  float a[3];
  for (uint8_t i = 0; i < n; i++) a[i] = v[i];
  for (uint8_t i = 1; i < n; i++)
    for (uint8_t j = i; j > 0 && a[j] < a[j - 1]; j--) { float t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
  return a[n / 2];
}

void readUltrassom(uint32_t now) {
#if ENABLE_HCSR04
  static uint32_t ultimo = 0;
  static float hist[3];
  static uint8_t histN = 0, histI = 0, falhas = 0;
  static uint16_t silencio = 0;
  if (ultimo && now - ultimo < US_PERIODO_MS) return;

  if (ultimo) {
    // 1) resultado do disparo anterior
    bool visto = echoVisto, pronto = echoPronto;
    uint32_t largura = echoLargura;
    if (visto) {
      silencio = 0;
      if (!usOk) { usOk = true; LOG("[HC-SR04] respondendo\n"); }
    } else if (++silencio >= 1000 / US_PERIODO_MS && usOk) {   // 1 s sem nenhum pulso de eco
      usOk = false;
      distanciaCm = -1;
      histN = 0;
      LOG("[HC-SR04] sem resposta (confira a ligação e o divisor do Echo)\n");
    }
    float cm = -1;
    if (pronto && largura < US_TIMEOUT_US) {
      float d = largura / 58.0f;                 // tempo de ida e volta (µs) ÷ 58 = cm
      if (d >= US_MIN_CM && d <= US_MAX_CM) cm = d;
    }
    if (cm > 0) {                                // mediana das 3 últimas leituras válidas
      hist[histI] = cm;
      histI = (histI + 1) % 3;
      if (histN < 3) histN++;
      falhas = 0;
      distanciaCm = mediana3(hist, histN);
    } else if (++falhas >= 3) {                  // 3 leituras sem eco válido seguidas
      distanciaCm = -1;
      histN = 0;
    }
  }
  // 2) novo disparo: pulso de 10 µs no Trigger
  echoPronto = false;
  echoVisto = false;
  echoInicio = 0;
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  ultimo = now;
#endif
}

// ============================================================ I2C ===
bool i2cResponde(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ============================================================ MPU-6050 ===
uint8_t mpuAddr = 0;
bool mpuOk = false;
float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, tempC = 0;
float gOffX = 0, gOffY = 0, gOffZ = 0;
uint8_t mpuFalhas = 0;
uint32_t mpuTentativa = 0;

bool mpuEscreve(uint8_t reg, uint8_t valor) {
  Wire.beginTransmission(mpuAddr);
  Wire.write(reg);
  Wire.write(valor);
  return Wire.endTransmission() == 0;
}

bool mpuLeBruto(int16_t r[7]) {           // aceleração X/Y/Z, temperatura, giroscópio X/Y/Z
  Wire.beginTransmission(mpuAddr);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)mpuAddr, 14) != 14) return false;
  for (int i = 0; i < 7; i++) r[i] = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

bool mpuInit() {
  mpuAddr = i2cResponde(0x68) ? 0x68 : i2cResponde(0x69) ? 0x69 : 0;
  if (!mpuAddr) return false;
  bool ok = mpuEscreve(0x6B, 0x01)          // acorda; relógio pelo giroscópio X
         && mpuEscreve(0x1A, 0x03)          // filtro passa-baixas ~44 Hz
         && mpuEscreve(0x1B, 0x00)          // giroscópio ±250 °/s → 131 LSB por °/s
         && mpuEscreve(0x1C, 0x00);         // acelerômetro ±2 g → 16384 LSB por g
  if (ok) LOG("[MPU-6050] encontrado em 0x%02X\n", mpuAddr);
  return ok;
}

void calibraGiroscopio() {
#if GYRO_CALIBRAR
  long sx = 0, sy = 0, sz = 0;
  int n = 0;
  int16_t r[7];
  for (int i = 0; i < 100; i++) {
    if (mpuLeBruto(r)) { sx += r[4]; sy += r[5]; sz += r[6]; n++; }
    delay(10);
  }
  if (n) { gOffX = sx / (float)n / 131.0f; gOffY = sy / (float)n / 131.0f; gOffZ = sz / (float)n / 131.0f; }
  LOG("[MPU-6050] desvio do giroscópio: %.2f %.2f %.2f °/s\n", gOffX, gOffY, gOffZ);
#endif
}

void setupMPU() {
#if ENABLE_MPU6050
  mpuOk = mpuInit();
  if (mpuOk) calibraGiroscopio();
  else LOG("[MPU-6050] não encontrado (0x68/0x69)\n");
#endif
}

void readMPU(uint32_t now) {
#if ENABLE_MPU6050
  static uint32_t ultimo = 0;
  if (!mpuOk) {
    if (now - mpuTentativa >= 3000) {      // sensor reconectado?
      mpuTentativa = now;
      if ((mpuOk = mpuInit())) mpuFalhas = 0;
    }
    return;
  }
  if (now - ultimo < MPU_PERIODO_MS) return;
  ultimo = now;
  int16_t r[7];
  if (!mpuLeBruto(r)) {
    if (++mpuFalhas >= 5) {
      mpuOk = false;
      mpuTentativa = now;
      LOG("[MPU-6050] parou de responder\n");
    }
    return;
  }
  mpuFalhas = 0;
  ax = r[0] / 16384.0f;
  ay = r[1] / 16384.0f;
  az = r[2] / 16384.0f;
  tempC = r[3] / 340.0f + 36.53f;           // fórmula da folha de dados (temperatura do chip)
  gx = r[4] / 131.0f - gOffX;
  gy = r[5] / 131.0f - gOffY;
  gz = r[6] / 131.0f - gOffZ;
#endif
}

// ============================================================ LCD 16x2 (PCF8574) ===
// Driver HD44780 em 4 bits pelo expansor I2C. Endereço detectado por varredura.
const uint8_t LCD_RS = 0x01, LCD_EN = 0x04, LCD_LUZ = 0x08;
uint8_t lcdAddr = 0;
bool lcdOk = false;
char lcdTela[2][17];
uint32_t lcdTentativa = 0;

uint8_t lcdBuf[120];
size_t lcdN = 0;

void lcdFila(uint8_t valor, uint8_t rs) {   // 1 byte = 2 meios-bytes, cada um com pulso no EN
  if (lcdN + 6 > sizeof(lcdBuf)) return;
  uint8_t alto = (valor & 0xF0) | rs | LCD_LUZ, baixo = ((valor << 4) & 0xF0) | rs | LCD_LUZ;
  lcdBuf[lcdN++] = alto; lcdBuf[lcdN++] = alto | LCD_EN; lcdBuf[lcdN++] = alto;
  lcdBuf[lcdN++] = baixo; lcdBuf[lcdN++] = baixo | LCD_EN; lcdBuf[lcdN++] = baixo;
}

bool lcdEnvia() {                           // uma transação I2C para a fila inteira
  Wire.beginTransmission(lcdAddr);
  Wire.write(lcdBuf, lcdN);
  lcdN = 0;
  return Wire.endTransmission() == 0;
}

bool lcdComando(uint8_t c) {
  lcdFila(c, 0);
  bool ok = lcdEnvia();
  if (c <= 3) delay(2);                     // limpar / início levam ~1,5 ms
  return ok;
}

void lcdMeioByte(uint8_t n) {               // usado só na inicialização
  uint8_t v = (n << 4) | LCD_LUZ;
  uint8_t b[3] = {v, (uint8_t)(v | LCD_EN), v};
  Wire.beginTransmission(lcdAddr);
  Wire.write(b, 3);
  Wire.endTransmission();
}

bool lcdInit() {
  const uint8_t candidatos[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E};
  lcdAddr = 0;
  for (uint8_t a : candidatos) if (i2cResponde(a)) { lcdAddr = a; break; }
  if (!lcdAddr) return false;
  delay(50);
  lcdMeioByte(0x03); delay(5);
  lcdMeioByte(0x03); delayMicroseconds(150);
  lcdMeioByte(0x03);
  lcdMeioByte(0x02);                        // modo 4 bits
  bool ok = lcdComando(0x28)                // 2 linhas, matriz 5x8
         && lcdComando(0x0C)                // display ligado, sem cursor
         && lcdComando(0x06)                // escreve da esquerda para a direita
         && lcdComando(0x01);               // limpa
  memset(lcdTela, ' ', sizeof(lcdTela));
  lcdTela[0][16] = lcdTela[1][16] = 0;
  if (ok) LOG("[LCD] encontrado em 0x%02X\n", lcdAddr);
  return ok;
}

// Escreve uma linha reescrevendo só os caracteres que mudaram (sem piscar).
void lcdLinha(uint8_t linha, const char* texto) {
  if (!lcdOk) return;
  char alvo[17];
  snprintf(alvo, sizeof(alvo), "%-16.16s", texto);
  for (uint8_t i = 0; i < 16;) {
    if (alvo[i] == lcdTela[linha][i]) { i++; continue; }
    lcdFila(0x80 | (linha ? 0x40 : 0x00) | i, 0);   // posiciona o cursor
    while (i < 16 && alvo[i] != lcdTela[linha][i]) { lcdFila((uint8_t)alvo[i], LCD_RS); lcdTela[linha][i] = alvo[i]; i++; }
    if (!lcdEnvia()) {
      lcdOk = false;
      lcdTentativa = millis();
      LOG("[LCD] parou de responder\n");
      return;
    }
  }
}

void setupLCD() {
#if ENABLE_LCD
  lcdOk = lcdInit();
  if (lcdOk) { lcdLinha(0, FW_VERSAO); lcdLinha(1, resetLcd()); }   // ex.: "Ini:QUEDA TENSAO"
  else LOG("[LCD] não encontrado (0x20-0x27 / 0x38-0x3F)\n");
#endif
}

void updateLCD(uint32_t now) {
#if ENABLE_LCD
  static uint32_t ultimo = 0;
  if (!lcdOk) {
    if (now - lcdTentativa >= 5000) {
      lcdTentativa = now;
      lcdOk = lcdInit();
    }
    return;
  }
  if (now - ultimo < 250) return;
  ultimo = now;
  char l1[24], l2[24];
  if (now < LCD_INICIO_MS) return;                   // mantém versão + motivo do reinício (setupLCD)
  bool mexendo = ultimaAtividade && now - ultimaAtividade < LCD_ATIVIDADE_MS;
  if (!wifiUp && !mexendo) {                         // sem Wi-Fi: o motivo fica na tela
    lcdLinha(0, wifiFalhas ? "WiFi: SEM REDE" : "WiFi: conectando");
    if (wifiFalhas) snprintf(l2, sizeof(l2), "%s x%u", wifiMotivoLcd(), (unsigned)wifiFalhas);
    else snprintf(l2, sizeof(l2), "%s", WIFI_SSID);
    lcdLinha(1, l2);
    return;
  }
  uint8_t tela = (now / LCD_TELA_MS) % 4;
  if (mexendo) tela = 0;
  if (tela == 0) {                                   // juntas: Base, Ombro, Cotovelo, Punho
    snprintf(l1, sizeof(l1), "%s:%+d %s:%+d", NOME_LCD[0], valorRel(0), NOME_LCD[1], valorRel(1));
    snprintf(l2, sizeof(l2), "%s:%+d %s:%+d", NOME_LCD[2], valorRel(2), NOME_LCD[3], valorRel(3));
  } else if (tela == 1) {                            // garra e encoder
    snprintf(l1, sizeof(l1), "Garra: %+d\xDF", valorRel(4));   // 0xDF = ° no LCD
    snprintf(l2, sizeof(l2), "Enc:%+ld", encCount);
  } else if (tela == 2) {
    if (mpuOk) {
      snprintf(l1, sizeof(l1), "Ax%+.2f Ay%+.2f", ax, ay);
      snprintf(l2, sizeof(l2), "Az%+.2f", az);
    } else {
      snprintf(l1, sizeof(l1), "MPU-6050");
      snprintf(l2, sizeof(l2), "nao encontrado");
    }
  } else {                                           // rede: IP + sinal · MQTT (ou código do erro) + clientes WS
    snprintf(l1, sizeof(l1), "%s %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    char mq[8];
    if (mqttOk) snprintf(mq, sizeof(mq), "ok");
    else if (mqttEstado) snprintf(mq, sizeof(mq), "%d", mqttEstado);   // −2 = rede/TLS, −4 = timeout
    else snprintf(mq, sizeof(mq), "--");
    snprintf(l2, sizeof(l2), "MQTT:%s WS:%u", mq, (unsigned)(redeIniciada && ENABLE_WEBSOCKET ? wsClientes() : 0));
  }
  lcdLinha(0, l1);
  lcdLinha(1, l2);
#endif
}

// ============================================================ JSON ===
const char* tf(bool b) { return b ? "true" : "false"; }

void addSensores(Json& j, const Snap& s) {
  j.add(",\"sensors\":{\"encoder\":%s,\"mpu6050\":%s,\"hcsr04\":%s,\"lcd\":%s,\"servo\":%s}",
        tf(s.encOk), tf(s.mpuOk), tf(s.usOk), tf(s.lcdOk), tf(s.servoOk));
}

size_t jsonTelemetria(const Snap& s, uint32_t seq, char* out, size_t cap) {
  Json j(out, cap);
  j.add("{\"type\":\"telemetry\",\"seq\":%lu,\"uptime_ms\":%lu", (unsigned long)seq, (unsigned long)s.uptime);
  if (s.encOk)
    j.add(",\"encoder\":{\"count\":%ld,\"dir\":%d,\"angle_deg\":%d,\"button\":%s,\"clicks\":%lu}",
          s.count, s.dir, s.angle, tf(s.button), (unsigned long)s.clicks);
  if (s.usOk) {
    if (s.distance < 0) j.add(",\"distance_cm\":null");
    else j.add(",\"distance_cm\":%.1f", s.distance);
  }
  if (s.mpuOk)
    j.add(",\"accel_g\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},\"gyro_dps\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}",
          s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
  if (s.servoOk) j.add(",\"servo\":{\"cmd_deg\":%d}", s.servoDeg);
  j.add(",\"axes_deg\":{");                 // posição absoluta (0-180°) de cada eixo; null = sem servo
  for (int i = 0; i < NUM_EIXOS; i++) {
    if (eixoAtivo[i]) j.add("%s\"%s\":%d", i ? "," : "", NOME_EIXO[i], (int)servoGrausEixo[i]);
    else j.add("%s\"%s\":null", i ? "," : "", NOME_EIXO[i]);
  }
  j.add("}");
  j.add(",\"axes_rel\":{");                 // posição PEDIDA de cada junta na escala do site (−90…90): o site sincroniza por ela
  for (int i = 0; i < NUM_EIXOS; i++) j.add("%s\"%s\":%d", i ? "," : "", NOME_EIXO[i], valorRel(i));
  j.add("}");
  j.add(",\"axes_atual\":{");               // onde cada servo está agora (anda até o pedido a SERVO_VEL_MAX)
  for (int i = 0; i < NUM_EIXOS; i++) j.add("%s\"%s\":%d", i ? "," : "", NOME_EIXO[i], valorAtual(i));
  j.add("}");
  if (s.rssi) j.add(",\"rssi\":%d", s.rssi);
  j.add(",\"estop\":%s", tf(estopAtivo));
  addSensores(j, s);
  j.add("}");
  return j.len;
}

size_t jsonHello(const Snap& s, char* out, size_t cap) {
  Json j(out, cap);
  j.add("{\"type\":\"hello\",\"device\":\"ESP32-C3 Super Mini\",\"fw\":\"%s\",\"ip\":\"%s\"",
        FW_VERSAO, WiFi.localIP().toString().c_str());
  j.add(",\"pins\":{");                     // GPIO de cada eixo (o site mostra ao lado do nome); null = sem servo
  for (int i = 0; i < NUM_EIXOS; i++) {
    if (PINO_EIXO[i] >= 0) j.add("%s\"%s\":%d", i ? "," : "", NOME_EIXO[i], PINO_EIXO[i]);
    else j.add("%s\"%s\":null", i ? "," : "", NOME_EIXO[i]);
  }
  j.add("}");
  // diagnóstico da conexão: o site mostra o motivo do último reinício e das quedas
  j.add(",\"reset\":\"%s\",\"uptime_s\":%lu,\"rssi\":%d,\"wifi_falhas\":%u,\"wifi_motivo\":%u,\"mqtt_quedas\":%u",
        resetCodigo(), (unsigned long)(s.uptime / 1000), s.rssi, (unsigned)wifiFalhas, (unsigned)wifiMotivo, (unsigned)mqttQuedas);
  addSensores(j, s);
  j.add("}");
  return j.len;
}

// Motivo do último reinício: "brownout" = a tensão caiu (servos puxando mais corrente que a fonte).
const char* resetCodigo() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_PWR_GLITCH: return "brownout";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_SW: return "software";
    case ESP_RST_EXT: return "botao";
    case ESP_RST_USB: case ESP_RST_JTAG: return "usb";
    default: return "outro";
  }
}

// O mesmo, curto, para o LCD (16 colunas, sem acento).
const char* resetLcd() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Ini: ligado";
    case ESP_RST_BROWNOUT: case ESP_RST_PWR_GLITCH: return "Ini:QUEDA TENSAO";
    case ESP_RST_PANIC: return "Ini: ERRO (panic)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "Ini: WATCHDOG";
    case ESP_RST_USB: case ESP_RST_JTAG: return "Ini: USB/gravado";
    default: return "Ini: reiniciado";
  }
}

void atualizaSnap(uint32_t now) {
  Snap s;
  s.encOk = encOk; s.count = encCount; s.dir = encDir; s.angle = encAngulo(); s.button = btnPressed; s.clicks = btnClicks;
  s.usOk = usOk; s.distance = distanciaCm;
  s.mpuOk = mpuOk; s.ax = ax; s.ay = ay; s.az = az; s.gx = gx; s.gy = gy; s.gz = gz; s.temp = tempC;
  s.lcdOk = lcdOk;
  s.servoOk = servoOk; s.servoDeg = servoGraus;
  static uint32_t ultimoRssi = 0;
  static int rssi = 0;
  if (now - ultimoRssi >= 1000) { ultimoRssi = now; rssi = wifiUp ? WiFi.RSSI() : 0; }
  s.rssi = rssi;
  s.uptime = now;
  portENTER_CRITICAL(&snapMux);
  shared = s;
  portEXIT_CRITICAL(&snapMux);
}

// ============================================================ PÁGINA (HTTP) ===
WebServer http(HTTP_PORT);

void setupPagina() {
#if ENABLE_PAGINA
  http.on("/", HTTP_GET, []() {
    http.sendHeader("Cache-Control", "no-cache");
    http.send_P(200, "text/html; charset=utf-8", PAGINA_HTML, strlen_P(PAGINA_HTML));
  });
  http.on("/status", HTTP_GET, []() {         // teste rápido: http://kit-iot.local/status
    char json[512];
    jsonHello(readShared(), json, sizeof(json));
    http.send(200, "application/json", json);
  });
  http.onNotFound([]() { http.sendHeader("Location", "/"); http.send(302, "text/plain", ""); });
  http.begin();
  MDNS.addService("http", "tcp", HTTP_PORT);
  LOG("[HTTP] página do ATLAS em http://%s.local e http://%s\n", KIT_NOME, WiFi.localIP().toString().c_str());
#endif
}

void atendePagina() {
#if ENABLE_PAGINA
  if (redeIniciada) http.handleClient();
#endif
}

// ============================================================ WEBSOCKET ===
WebSocketsServer ws(WS_PORT);

uint8_t wsClientes() { return ws.connectedClients(); }

void onWsEvento(uint8_t num, WStype_t tipo, uint8_t* payload, size_t len) {
  if (tipo == WStype_CONNECTED) {
    char json[512];
    jsonHello(readShared(), json, sizeof(json));
    ws.sendTXT(num, json);
    LOG("[WS] cliente #%u conectado (%s)\n", num, ws.remoteIP(num).toString().c_str());
  } else if (tipo == WStype_DISCONNECTED) {
    LOG("[WS] cliente #%u desconectado\n", num);
  } else if (tipo == WStype_TEXT) {
    char msg[128];
    size_t n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
    memcpy(msg, payload, n);
    msg[n] = 0;
    float v;
    if (jsonNumero(msg, "ping", &v)) {          // medição de latência pela página
      ws.sendTXT(num, "{\"type\":\"pong\"}");
      return;
    }
    if (jsonNumero(msg, "estop", &v)) {         // E-STOP pela via local: {"estop":1} congela · {"estop":0} libera
      bool novo = v != 0;
      if (novo != estopAtivo) {
        estopAtivo = novo;
        if (estopAtivo) congelaServos();
        LOG("[WS] E-STOP %s\n", estopAtivo ? "ATIVO: servos congelados" : "liberado");
#if ENABLE_MQTT
        estopParaMqtt = true;                   // replica o estado retido no broker (tarefa do MQTT)
#endif
      }
      atividade(millis());
      return;
    }
    aplicaComando("WS", msg);
  }
}

void enviaWs(uint32_t now) {
#if ENABLE_WEBSOCKET
  static uint32_t ultimo = 0, seq = 0;
  if (!redeIniciada) return;
  ws.loop();
  if (!ws.connectedClients()) return;
  bool urgente = wsUrgente && now - ultimo >= WS_URGENTE_MS;
  if (now - ultimo < WS_PERIODO_MS && !urgente) return;
  wsUrgente = false;
  ultimo = now;
  char json[1024];
  jsonTelemetria(readShared(), ++seq, json, sizeof(json));
  ws.broadcastTXT(json);
#endif
}

// ============================================================ MQTT (tarefa própria) ===
// A conexão com o broker (DNS + TLS) pode levar segundos. Numa tarefa separada, ela não
// atrasa a leitura dos sensores, o LCD nem o WebSocket.
#if MQTT_TLS
WiFiClientSecure mqttRede;
#else
WiFiClient mqttRede;
#endif
PubSubClient mqtt(mqttRede);
char topicoUp[96], topicoOnline[96], topicoCmd[96], topicoEstop[96];

// Comando recebido fica aqui até o fim da leitura: se vierem vários seguidos, só o último vale.
char cmdPendente[128];
volatile bool temCmdPendente = false;

// Chamado pelo PubSubClient (dentro de mqtt.loop(), na tarefa do MQTT) a cada mensagem recebida.
// Fica curto de propósito: nada de Serial aqui (o USB-CDC pode travar a tarefa).
void onMqttMsg(char* topic, byte* payload, unsigned int len) {
  char msg[128];
  size_t n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = 0;
  if (strcmp(topic, topicoEstop) == 0) {          // E-STOP: "1" congela, "0" libera (aplica na hora)
    bool novo = (msg[0] == '1');
    if (novo != estopAtivo) {
      estopAtivo = novo;
      if (estopAtivo) congelaServos();
      LOG("[MQTT] E-STOP %s\n", estopAtivo ? "ATIVO: servos congelados" : "liberado");
      atividade(millis());
    }
    return;
  }
  memcpy(cmdPendente, msg, n + 1);                // guarda só o mais recente
  temCmdPendente = true;
}

const char* mqttMotivo(int st) {
  switch (st) {
    case -4: return "sem resposta do broker (timeout)";
    case -3: return "conexão perdida";
    case -2: return "falha de rede/TLS (DNS, porta 8883 bloqueada ou pouca memória)";
    case -1: return "desconectado";
    case 1:  return "protocolo recusado";
    case 2:  return "client ID recusado";
    case 3:  return "broker indisponível";
    case 4:  return "usuário/senha inválidos";
    case 5:  return "não autorizado";
    default: return "?";
  }
}

void mqttFechaSocket() {
  mqtt.disconnect();
  mqttRede.stop();                                // libera o socket/TLS (evita vazamento de memória)
}

bool mqttConecta() {
  char id[40];
  snprintf(id, sizeof(id), "kit-iot-%06lx-%04lx", (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF), (unsigned long)(esp_random() & 0xFFFF));
  mqttFechaSocket();
  LOG("[MQTT] conectando a %s:%u · RSSI %d dBm · memória livre %u B…\n",
      MQTT_HOST, MQTT_PORT, WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
  uint32_t t0 = millis();
  // last will: se o kit cair, o broker publica "0" (retido) em /online
  bool conectou = (strlen(MQTT_USER) > 0)
      ? mqtt.connect(id, MQTT_USER, MQTT_PASS, topicoOnline, 1, true, "0")
      : mqtt.connect(id, topicoOnline, 1, true, "0");          // broker sem login (HiveMQ padrão)
  if (!conectou) {
    mqttEstado = mqtt.state();
    LOG("[MQTT] falhou: %s (código %d)\n", mqttMotivo(mqtt.state()), mqtt.state());
    mqttRede.stop();
    return false;
  }
  mqttEstado = 0;
  mqtt.publish(topicoOnline, "1", true);
  // assina os tópicos de comando a cada (re)conexão
  bool s1 = mqtt.subscribe(topicoCmd);
  bool s2 = mqtt.subscribe(topicoEstop, 1);       // E-STOP retido pelo site: chega logo após assinar
  bool s3 = true;
  if (strlen(MQTT_CMD_EXTRA) > 0) s3 = mqtt.subscribe(MQTT_CMD_EXTRA);
  LOG("[MQTT] conectado em %lu ms · publica em %s · ouve %s (%s) e %s (%s)%s\n",
      (unsigned long)(millis() - t0), topicoUp, topicoCmd, s1 ? "ok" : "falhou", topicoEstop, s2 ? "ok" : "falhou",
      s3 ? "" : " · tópico extra falhou");
  return true;
}

// "Assinatura" do que o site mostra: muda quando um servo, o encoder ou o E-STOP muda.
uint32_t assinaturaEstado(const Snap& s) {
  uint32_t h = 2166136261u;
  auto mix = [&](int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; };
  for (int i = 0; i < NUM_EIXOS; i++) mix(servoGrausEixo[i]);
  mix(s.count); mix(s.clicks); mix(s.button); mix(estopAtivo);
  return h;
}

void tarefaMqtt(void*) {
  uint32_t proximaTentativa = 0, espera = MQTT_RETRY_MIN_MS;
  uint32_t ultimoEnvio = 0, ultimoHello = 0, seq = 0, conectadoEm = 0, ultimaAssinatura = 0;
  uint8_t falhasPublicar = 0;
  bool helloPendente = true, conectadoAntes = false, wifiAntes = false;
  char json[1024];
  for (;;) {
    uint32_t now = millis();
    bool wifi = WiFi.status() == WL_CONNECTED;
    if (!wifi) {
      if (wifiAntes) {                            // Wi-Fi caiu: fecha o TLS já, sem esperar o keep-alive
        wifiAntes = false;
        mqttFechaSocket();
        if (conectadoAntes) { conectadoAntes = false; mqttQuedas++; LOG("[MQTT] Wi-Fi caiu: conexão MQTT fechada\n"); }
      }
      mqttOk = false;
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (!wifiAntes) {                             // Wi-Fi voltou: tenta o broker imediatamente
      wifiAntes = true;
      proximaTentativa = now;
      espera = MQTT_RETRY_MIN_MS;
    }
    if (!mqtt.connected()) {
      mqttOk = false;
      if (conectadoAntes) {
        conectadoAntes = false;
        mqttQuedas++;
        mqttEstado = mqtt.state();
        LOG("[MQTT] conexão perdida após %lu s: %s · quedas: %u\n",
            (unsigned long)((now - conectadoEm) / 1000), mqttMotivo(mqtt.state()), (unsigned)mqttQuedas);
        proximaTentativa = now + 500;             // a 1ª nova tentativa é quase imediata
        espera = MQTT_RETRY_MIN_MS;
      }
      if ((int32_t)(now - proximaTentativa) >= 0) {
        if (mqttConecta()) {
          helloPendente = true; conectadoAntes = true; conectadoEm = millis();
          falhasPublicar = 0; espera = MQTT_RETRY_MIN_MS; ultimaAssinatura = 0;
        } else {
          proximaTentativa = millis() + espera;
          LOG("[MQTT] nova tentativa em %lu s\n", (unsigned long)(espera / 1000));
          espera = espera * 2 > MQTT_RETRY_MAX_MS ? MQTT_RETRY_MAX_MS : espera * 2;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    mqttOk = true;

    // Lê TUDO o que chegou (o PubSubClient trata um pacote por loop()); depois aplica só o último comando.
    for (int i = 0; i < 8; i++) {
      if (!mqtt.loop()) break;
      if (!mqttRede.available()) break;
    }
    if (temCmdPendente) {
      char c[128];
      memcpy(c, cmdPendente, sizeof(c));
      temCmdPendente = false;
      aplicaComando("MQTT", c);
    }
    if (!mqtt.connected()) continue;

    bool ok = true;
    if (estopParaMqtt) {                          // mantém o E-STOP retido igual ao do kit
      estopParaMqtt = false;
      mqtt.publish(topicoEstop, estopAtivo ? "1" : "0", true);
    }
    if (helloPendente || now - ultimoHello >= HELLO_PERIODO_MS) {
      jsonHello(readShared(), json, sizeof(json));
      ok = mqtt.publish(topicoUp, json);
      ultimoHello = now;
      helloPendente = false;
    }
    Snap snap = readShared();
    uint32_t assinatura = assinaturaEstado(snap);
    bool mudou = assinatura != ultimaAssinatura || mqttUrgente;
    if (now - ultimoEnvio >= MQTT_PERIODO_MS || (mudou && now - ultimoEnvio >= MQTT_URGENTE_MS)) {
      mqttUrgente = false;
      ultimoEnvio = now;
      ultimaAssinatura = assinatura;
      size_t n = jsonTelemetria(snap, ++seq, json, sizeof(json));
      ok = mqtt.publish(topicoUp, (const uint8_t*)json, n, false) && ok;
    }
    if (!ok) {
      if (++falhasPublicar >= MQTT_FALHAS_PUBLICAR) {
        LOG("[MQTT] %u publicações seguidas falharam: reconectando\n", (unsigned)falhasPublicar);
        falhasPublicar = 0;
        mqttFechaSocket();
      }
    } else {
      falhasPublicar = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setupMQTT() {
#if ENABLE_MQTT
  snprintf(topicoUp, sizeof(topicoUp), "%s/up", MQTT_TOPIC);
  snprintf(topicoOnline, sizeof(topicoOnline), "%s/online", MQTT_TOPIC);
  snprintf(topicoCmd, sizeof(topicoCmd), "%s/cmd", MQTT_TOPIC);
  snprintf(topicoEstop, sizeof(topicoEstop), "%s/estop", MQTT_TOPIC);
#if MQTT_TLS
  mqttRede.setInsecure();                   // criptografa, mas não valida o certificado do broker
  mqttRede.setHandshakeTimeout(15);
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMsg);              // <<< sem isto o kit nunca recebia comandos
  mqtt.setBufferSize(1280);                 // telemetria ≈ 700 bytes
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  xTaskCreate(tarefaMqtt, "mqtt", 12288, nullptr, 2, nullptr);   // prioridade acima do loop()
#endif
}

// ============================================================ WI-FI ===
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(KIT_NOME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);                   // não regrava a flash a cada conexão
  // Motivo de cada queda/tentativa frustrada (roda na tarefa do Wi-Fi: só guarda, quem mostra é o laço)
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiMotivo = info.wifi_sta_disconnected.reason;
    wifiFalhas++;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);                     // SEM economia de energia: o rádio não "cochila" e o MQTT não cai
#if WIFI_POTENCIA_REDUZIDA
  WiFi.setTxPower(WIFI_TX_POWER);
#endif
  LOG("[WIFI] conectando a \"%s\"…\n", WIFI_SSID);
}

// Texto curto do motivo da queda do Wi-Fi para o LCD (códigos do ESP-IDF).
const char* wifiMotivoLcd() {
  static char buf[17];
  switch (wifiMotivo) {
    case 201: return "nao acha rede";                         // NO_AP_FOUND: nome errado ou longe demais
    case 15: case 202: case 204: return "senha/sinal?";      // handshake/autenticação falhou
    case 200: case 2: case 4: return "sinal fraco";          // beacon timeout / auth/assoc expirou
    case 8: return "desconectou";
    default: snprintf(buf, sizeof(buf), "motivo %u", (unsigned)wifiMotivo); return buf;
  }
}

void handleWiFi(uint32_t now) {
  static uint32_t caiuEm = 0;
  static uint32_t proximaTentativa = 0;
  static uint8_t tentativas = 0;
  static uint16_t falhasLogadas = 0;
  if (wifiFalhas != falhasLogadas) {        // o evento do Wi-Fi só guardou; o log sai daqui
    falhasLogadas = wifiFalhas;
    LOG("[WIFI] sem conexão: motivo %u (%s) · %u falha(s) desde que ligou\n", (unsigned)wifiMotivo,
        WiFi.disconnectReasonName((wifi_err_reason_t)wifiMotivo), (unsigned)falhasLogadas);
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiUp) {
    wifiUp = true;
    tentativas = 0;
    WiFi.setSleep(false);                   // reforço: algumas versões do core religam o modem sleep
    LOG("[WIFI] conectado · IP %s · RSSI %d dBm · ws://%s:%u\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), WS_PORT);
    if (WiFi.RSSI() < -75) LOG("[WIFI] sinal fraco: aproxime do roteador ou aumente WIFI_TX_POWER\n");
    if (!redeIniciada) {
      redeIniciada = true;
      if (MDNS.begin(KIT_NOME)) LOG("[WIFI] também em ws://%s.local:%u\n", KIT_NOME, WS_PORT);
#if ENABLE_WEBSOCKET
      ws.begin();
      ws.onEvent(onWsEvento);
      ws.enableHeartbeat(5000, 3000, 2);   // ping a cada 5 s; 2 falhas = desconecta cliente sumido
      MDNS.addService("ws", "tcp", WS_PORT);
#endif
      setupPagina();
    }
  } else if (!up && wifiUp) {
    wifiUp = false;
    caiuEm = now;
    proximaTentativa = now;
    tentativas = 0;
    LOG("[WIFI] conexão perdida; reconectando…\n");
  }
  if (!up && (int32_t)(now - proximaTentativa) >= 0) {
    // Não depende apenas do AutoReconnect: alguns roteadores deixam o rádio preso.
    // Recomeça rápido e reinicia somente a interface Wi-Fi, não o ESP32 nem o robô.
    proximaTentativa = now + (tentativas < 3 ? 3000 : 10000);
    tentativas++;
    if (tentativas >= 4) {
      tentativas = 0;
      LOG("[WIFI] sem rede há %lu s: reiniciando o rádio Wi-Fi\n",
          (unsigned long)((now - caiuEm) / 1000));
      WiFi.disconnect(false, false);
      delay(20);
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(KIT_NOME);
      WiFi.setSleep(false);
#if WIFI_POTENCIA_REDUZIDA
      WiFi.setTxPower(WIFI_TX_POWER);
#endif
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
      LOG("[WIFI] tentativa de reconexão %u\n", (unsigned)tentativas);
      WiFi.reconnect();
    }
  }
}

// ============================================================ RESUMO NO SERIAL ===
void logStatus(uint32_t now) {
#if DEBUG
  static uint32_t ultimo = 0;
  if (STATUS_LOG_MS == 0 || now - ultimo < STATUS_LOG_MS) return;
  ultimo = now;
  LOG("[STATUS] %lus · Wi-Fi %s", (unsigned long)(now / 1000), wifiUp ? "ok" : "OFF");
  if (wifiUp) LOG(" (%s, %d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  LOG(" · MQTT %s · E-STOP %s · memória livre %u B\n", mqttOk ? "ok" : "OFF", estopAtivo ? "ATIVO" : "livre", (unsigned)ESP.getFreeHeap());
  logJuntas("JUNTAS");
#endif
}

// ============================================================ SETUP / LOOP ===
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
#if ARDUINO_USB_CDC_ON_BOOT
  // Espera o Monitor Serial abrir só se o USB estiver num PC; na fonte/carregador segue direto.
  while (Serial.isPlugged() && !Serial && millis() - t0 < 3000) delay(10);
  // USB no PC com o Monitor Serial fechado: sem isto cada LOG espera até 100 ms e trava o laço
  // (WebSocket, encoder, servos). Com 0, o texto que não cabe é descartado na hora.
  Serial.setTxTimeoutMs(0);
#else
  while (!Serial && millis() - t0 < 3000) delay(10);
#endif
  delay(300);
  LOG("\n=== %s · %s ===\n", FW_VERSAO, KIT_NOME);
  LOG("Último reinício: %s%s\n", resetCodigo(),
      esp_reset_reason() == ESP_RST_BROWNOUT ? "  <<< QUEDA DE TENSÃO: alimente os servos por fonte externa 5 V (GND comum)" : "");
  LOG("MQTT %s:%u · tópico %s/up (telemetria) e %s/cmd (comando) · WebSocket porta %u\n",
      MQTT_HOST, MQTT_PORT, MQTT_TOPIC, MQTT_TOPIC, WS_PORT);
  Wire.begin(PIN_SDA, PIN_SCL);             // SDA = GPIO1, SCL = GPIO0
  Wire.setClock(100000);                    // o PCF8574 do LCD trabalha até 100 kHz
  setupLCD();
  setupMPU();
  setupEncoder();
  setupUltrassom();
  setupServo();
  setupWiFi();
  setupMQTT();
}

void loop() {
  uint32_t now = millis();
  handleWiFi(now);
  readEncoder(now);
  readBotao(now);
  atualizaServos(now);
  readUltrassom(now);
  readMPU(now);
  atualizaSnap(now);
  enviaWs(now);
  atendePagina();
  updateLCD(now);
  logStatus(now);
  delay(1);                                 // cede tempo à tarefa do MQTT e ao Wi-Fi
}
