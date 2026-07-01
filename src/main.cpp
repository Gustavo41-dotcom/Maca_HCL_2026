/**
 * @file main.cpp
 * @brief Firmware de Controle de Movimento e SeguranÃ§a - Maca HC
 * @author Equipe Maca HC
 * @institution IFSP - Campus Guarulhos
 * @details MÃ¡quina de estados nÃ£o-bloqueante com rampa PWM, interrupÃ§Ãµes de hardware para 
 * botoeiras e limites de curso, filtro de mÃ©dia mÃ³vel para detecÃ§Ã£o de sobrecarga mecÃ¢nica 
 * (seguranÃ§a do paciente) e temporizador de seguranÃ§a (Timeout).
 */

#include <Arduino.h>
#include "BalancaMaca.h"

// ==============================================================================
// 1. MAPEAMENTO DE HARDWARE (PINOUT)
// ==============================================================================

// --- Ponte H (Motor) ---
#define RPWM_LE 25         // RotaÃ§Ã£o Frente (Sobe)
#define LPWM_LE 26         // RotaÃ§Ã£o RÃ© (Desce)
#define EN_LE   27         // HabilitaÃ§Ã£o do Driver Esquerdo
#define RPWM_LD 14
#define LPWM_LD 12
#define EN_LD   13

// --- Interface Humano-MÃ¡quina (IHM) ---
#define BOTAO_LIGAR 4      // BotÃ£o Azul: Inicia/Para ciclo de movimento
#define BOTAO_EMERGENCIA 5 // Botoeira Cogumelo Vermelha: Corte absoluto de energia
#define LED_FALHA 15       // Alerta Amarelo: IndicaÃ§Ã£o visual de bloqueio/falha

// --- Sensores de Limite e SeguranÃ§a ---
#define FIM_CURSO_CIMA 18  // Micro-switch de limite superior (NF - Normalmente Fechado)
#define FIM_CURSO_BAIXO 19 // Micro-switch de limite inferior (NF - Normalmente Fechado)

// ==============================================================================
// 2. CONFIGURAÃ‡Ã•ES DO SISTEMA
// ==============================================================================

// --- ConfiguraÃ§Ãµes do PWM (Motores) ---
#define CANAL_RPWM_LE 0
#define CANAL_LPWM_LE 1
#define CANAL_RPWM_LD 2
#define CANAL_LPWM_LD 3
const int freq = 5000;
const int resolution = 8; 

// --- ConfiguraÃ§Ãµes de Sensores e Travas ---
BalancaMaca sensorCorrenteLE(36); 
const float LIMITE_CORRENTE_PERIGOSA = 3000.0; 

// NOVO: Temporizador de SeguranÃ§a (Timeout)
const unsigned long TEMPO_MAXIMO_OPERACAO = 12000; // 12 segundos em milissegundos
unsigned long tempoInicioOperacao = 0; 

// --- VariÃ¡veis da MÃ¡quina de Estados ---
enum EstadoMaca { DESLIGADO, ACELERANDO, MOVIMENTO_CONSTANTE, DESACELERANDO, EMERGENCY_STOP };
EstadoMaca estadoAtual = DESLIGADO; 

unsigned long tempoUltimoPasso = 0;
const int INTERVALO_RAMPA = 15; 
int velocidadeAtual = 0;
int velocidadeAlvo = 0;
int sentidoAtual = 1; 

// Flags de diagnÃ³stico de falha para o Alarme Visual
bool bloqueioPorSobrecarga = false; 
bool bloqueioPorTimeout = false;

// ==============================================================================
// 3. ROTINAS DE INTERRUPÃ‡ÃƒO DE HARDWARE (ISR)
// ==============================================================================

volatile bool flagBotaoLigar = false;
volatile bool flagBotaoEmergencia = false;

void IRAM_ATTR tratarBotaoLigar() { flagBotaoLigar = true; }
void IRAM_ATTR tratarBotaoEmergencia() { flagBotaoEmergencia = true; }

// ==============================================================================
// 4. SETUP INICIAL
// ==============================================================================

void setup() {
  Serial.begin(115200);
  sensorCorrenteLE.inicializar();

  ledcSetup(CANAL_RPWM_LE, freq, resolution);
  ledcSetup(CANAL_LPWM_LE, freq, resolution);
  ledcSetup(CANAL_RPWM_LD, freq, resolution);
  ledcSetup(CANAL_LPWM_LD, freq, resolution);
  
  ledcAttachPin(RPWM_LE, CANAL_RPWM_LE);
  ledcAttachPin(LPWM_LE, CANAL_LPWM_LE);
  ledcAttachPin(RPWM_LD, CANAL_RPWM_LD);
  ledcAttachPin(LPWM_LD, CANAL_LPWM_LD);
  
  pinMode(EN_LE, OUTPUT);
  pinMode(EN_LD, OUTPUT);
  digitalWrite(EN_LE, HIGH);
  digitalWrite(EN_LD, HIGH);

  pinMode(LED_FALHA, OUTPUT);
  digitalWrite(LED_FALHA, LOW);

  pinMode(BOTAO_LIGAR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOTAO_LIGAR), tratarBotaoLigar, FALLING);
  pinMode(BOTAO_EMERGENCIA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOTAO_EMERGENCIA), tratarBotaoEmergencia, FALLING);
  
  pinMode(FIM_CURSO_CIMA, INPUT_PULLUP);
  pinMode(FIM_CURSO_BAIXO, INPUT_PULLUP);
}

// ==============================================================================
// 5. FUNÃ‡Ã•ES AUXILIARES
// ==============================================================================

void moverMaca(int velocidade) {
  if (velocidade > 0) {
    ledcWrite(CANAL_RPWM_LE, velocidade);
    ledcWrite(CANAL_LPWM_LE, 0);
    ledcWrite(CANAL_RPWM_LD, velocidade);
    ledcWrite(CANAL_LPWM_LD, 0);
  } else if (velocidade < 0) {
    ledcWrite(CANAL_RPWM_LE, 0);
    ledcWrite(CANAL_LPWM_LE, abs(velocidade));
    ledcWrite(CANAL_RPWM_LD, 0);
    ledcWrite(CANAL_LPWM_LD, abs(velocidade));
  } else {
    ledcWrite(CANAL_RPWM_LE, 0);
    ledcWrite(CANAL_LPWM_LE, 0);
    ledcWrite(CANAL_RPWM_LD, 0);
    ledcWrite(CANAL_LPWM_LD, 0);
  }
}

unsigned long tempoUltimoCliqueLigar = 0;
unsigned long tempoUltimoCliqueEmergencia = 0;

// ==============================================================================
// 6. LOOP PRINCIPAL (MÃQUINA DE ESTADOS)
// ==============================================================================

void loop() {
  unsigned long tempoAtual = millis();
  sensorCorrenteLE.atualizarFiltro();

  // --- TRAVA 1: Fim de Curso MecÃ¢nico ---
  if (estadoAtual == ACELERANDO || estadoAtual == MOVIMENTO_CONSTANTE || estadoAtual == DESACELERANDO) {
    if (velocidadeAtual > 0 && digitalRead(FIM_CURSO_CIMA) == LOW) {
      Serial.println("LIMITADOR: Fim de Curso SUPERIOR atingido!");
      moverMaca(0);
      velocidadeAtual = velocidadeAlvo = 0;
      estadoAtual = DESLIGADO; 
    } 
    else if (velocidadeAtual < 0 && digitalRead(FIM_CURSO_BAIXO) == LOW) {
      Serial.println("LIMITADOR: Fim de Curso INFERIOR atingido!");
      moverMaca(0);
      velocidadeAtual = velocidadeAlvo = 0;
      estadoAtual = DESLIGADO; 
    }
  }

  // --- TRAVA 2: Sobrecarga de Corrente (ObstruÃ§Ã£o FÃ­sica) ---
  if (estadoAtual != DESLIGADO && estadoAtual != EMERGENCY_STOP) {
    float correnteLida = sensorCorrenteLE.obterMediaADC();
    if (correnteLida > LIMITE_CORRENTE_PERIGOSA) {
      Serial.println("ALERTA CRITICO: Obstrucao mecanica detectada!");
      estadoAtual = EMERGENCY_STOP;
      bloqueioPorSobrecarga = true; 
    }
  }

  // --- TRAVA 3: Temporizador de SeguranÃ§a (Timeout) ---
  if (estadoAtual != DESLIGADO && estadoAtual != EMERGENCY_STOP) {
    if (tempoAtual - tempoInicioOperacao > TEMPO_MAXIMO_OPERACAO) {
      Serial.println("ERRO FATAL: Falha de Timeout. Ciclo excedeu o tempo maximo seguro!");
      estadoAtual = EMERGENCY_STOP;
      bloqueioPorTimeout = true;
    }
  }

  // --- CONTROLE 1: BotÃ£o de EmergÃªncia Absoluta ---
  if (flagBotaoEmergencia) {
    flagBotaoEmergencia = false;
    if (tempoAtual - tempoUltimoCliqueEmergencia > 300) { 
      tempoUltimoCliqueEmergencia = tempoAtual;
      Serial.println("PARADA DE EMERGENCIA ACIONADA MANUALMENTE!");
      estadoAtual = EMERGENCY_STOP;
      bloqueioPorSobrecarga = false; 
      bloqueioPorTimeout = false;
    }
  }

  // --- CONTROLE 2: BotÃ£o de OperaÃ§Ã£o Normal (Azul) ---
  if (flagBotaoLigar) {
    flagBotaoLigar = false;
    if (tempoAtual - tempoUltimoCliqueLigar > 300) { 
      tempoUltimoCliqueLigar = tempoAtual;
      
      if (estadoAtual == DESLIGADO || estadoAtual == EMERGENCY_STOP) {
        
        if (sentidoAtual == 1 && digitalRead(FIM_CURSO_CIMA) == LOW) sentidoAtual = -1; 
        else if (sentidoAtual == -1 && digitalRead(FIM_CURSO_BAIXO) == LOW) sentidoAtual = 1; 

        // Limpa todas as flags de erro ao rearmar
        bloqueioPorSobrecarga = false;
        bloqueioPorTimeout = false;
        digitalWrite(LED_FALHA, LOW); 
        
        estadoAtual = ACELERANDO;
        velocidadeAlvo = 200 * sentidoAtual; 
        sentidoAtual = sentidoAtual * -1; 
        
        // Registra o tempo em que o movimento comeÃ§ou para a Trava 3
        tempoInicioOperacao = tempoAtual; 

      } else {
        velocidadeAlvo = 0;
        estadoAtual = DESACELERANDO;
      }
    }
  }

  // --- NÃšCLEO: ExecuÃ§Ã£o dos Estados ---
  switch (estadoAtual) {
    case DESLIGADO:
      break;
      
    case ACELERANDO:
      if (tempoAtual - tempoUltimoPasso >= INTERVALO_RAMPA) {
        tempoUltimoPasso = tempoAtual; 
        if (velocidadeAtual < velocidadeAlvo) velocidadeAtual++;
        if (velocidadeAtual > velocidadeAlvo) velocidadeAtual--;
        moverMaca(velocidadeAtual);
        if (velocidadeAtual == velocidadeAlvo) estadoAtual = MOVIMENTO_CONSTANTE;
      }
      break;
      
    case MOVIMENTO_CONSTANTE:
      if (tempoAtual - tempoUltimoPasso >= 2000) {
        velocidadeAlvo = 0; 
        estadoAtual = DESACELERANDO;
      }
      break;
      
    case DESACELERANDO:
      if (tempoAtual - tempoUltimoPasso >= INTERVALO_RAMPA) {
        tempoUltimoPasso = tempoAtual;
        if (velocidadeAtual > 0) velocidadeAtual--;
        if (velocidadeAtual < 0) velocidadeAtual++;
        moverMaca(velocidadeAtual);
        if (velocidadeAtual == 0) estadoAtual = DESLIGADO;
      }
      break;
      
    case EMERGENCY_STOP:
      moverMaca(0); 
      velocidadeAtual = 0;
      velocidadeAlvo = 0;
      
      // IHM Visual DinÃ¢mico para diferentes falhas
      if (bloqueioPorSobrecarga) {
        // ObstruÃ§Ã£o MecÃ¢nica: Pisca RÃ¡pido
        if (tempoAtual % 600 < 300) digitalWrite(LED_FALHA, HIGH);
        else digitalWrite(LED_FALHA, LOW);
      } 
      else if (bloqueioPorTimeout) {
        // Falha no Motor/Trilho (Timeout): Fica Aceso Direto
        digitalWrite(LED_FALHA, HIGH);
      } 
      else {
        // EmergÃªncia Manual (Desarme): Apagado
        digitalWrite(LED_FALHA, LOW);
      }
      break;
  }
}