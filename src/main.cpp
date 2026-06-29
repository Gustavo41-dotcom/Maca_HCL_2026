#include <Arduino.h>

// Pinos Motor LE (Esquerdo)
#define RPWM_LE 25
#define LPWM_LE 26
#define EN_LE   27

// Pinos Motor LD (Direito)
#define RPWM_LD 14
#define LPWM_LD 12
#define EN_LD   13

// Configurações do PWM
const int freq = 5000;
const int resolution = 8; // Resolução de 8 bits (0 a 255)

void setup() {
  Serial.begin(115200);
  
  // Configura PWM pela nova API (ESP32 Core 3.0+)
  ledcAttach(RPWM_LE, freq, resolution);
  ledcAttach(LPWM_LE, freq, resolution);
  ledcAttach(RPWM_LD, freq, resolution);
  ledcAttach(LPWM_LD, freq, resolution);
  
  // Configura pinos de habilitação dos drivers (Enable)
  pinMode(EN_LE, OUTPUT);
  pinMode(EN_LD, OUTPUT);
  
  // Liga os drivers IBT-2
  digitalWrite(EN_LE, HIGH);
  digitalWrite(EN_LD, HIGH);
  
  Serial.println("Sistema da Maca: Motores Prontos.");
}

// Função unificada para mover os dois motores simultaneamente
// Aceita valores de velocidade entre -255 (descendo) e 255 (subindo)
void moverMaca(int velocidade) {
  if (velocidade > 0) {
    ledcWrite(RPWM_LE, velocidade);
    ledcWrite(LPWM_LE, 0);
    ledcWrite(RPWM_LD, velocidade);
    ledcWrite(LPWM_LD, 0);
  } else if (velocidade < 0) {
    ledcWrite(RPWM_LE, 0);
    ledcWrite(LPWM_LE, abs(velocidade));
    ledcWrite(RPWM_LD, 0);
    ledcWrite(LPWM_LD, abs(velocidade));
  } else {
    ledcWrite(RPWM_LE, 0);
    ledcWrite(LPWM_LE, 0);
    ledcWrite(RPWM_LD, 0);
    ledcWrite(LPWM_LD, 0);
  }
}

void loop() {
  Serial.println("Acelerando (Sentido 1)...");
  for(int i = 0; i <= 200; i++) {
    moverMaca(i);
    delay(15);
  }
  
  delay(2000); // Mantém a velocidade constante
  
  Serial.println("Desacelerando...");
  for(int i = 200; i >= 0; i--) {
    moverMaca(i);
    delay(15);
  }
  
  delay(1000); // Pausa
  
  Serial.println("Acelerando (Sentido 2)...");
  for(int i = 0; i >= -200; i--) {
    moverMaca(i);
    delay(15);
  }
  
  delay(2000); // Mantém a velocidade constante
  
  Serial.println("Desacelerando...");
  for(int i = -200; i <= 0; i++) {
    moverMaca(i);
    delay(15);
  }
  
  delay(1000); // Pausa antes de repetir o ciclo
}