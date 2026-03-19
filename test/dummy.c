#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>

// Definizione dei PIN (Numerazione BCM)
#define MOTORE_SX_A 22
#define MOTORE_SX_B 23
#define MOTORE_DX_A 18
#define MOTORE_DX_B 17

// Funzione per fermare tutto
void stop_car() {
    gpioPWM(MOTORE_SX_A, 0); gpioPWM(MOTORE_SX_B, 0);
    gpioPWM(MOTORE_DX_A, 0); gpioPWM(MOTORE_DX_B, 0);
    printf("Stop\n");
}

// Funzioni di movimento con controllo velocità (0-255)
void avanti(int v) {
    gpioPWM(MOTORE_SX_A, v); gpioWrite(MOTORE_SX_B, 0);
    gpioPWM(MOTORE_DX_A, v); gpioWrite(MOTORE_DX_B, 0);
    printf("Avanti a velocità %d\n", v);
}

void indietro(int v) {
    gpioWrite(MOTORE_SX_A, 0); gpioPWM(MOTORE_SX_B, v);
    gpioWrite(MOTORE_DX_A, 0); gpioPWM(MOTORE_DX_B, v);
    printf("Indietro a velocità %d\n", v);
}

void destra(int v) {
    gpioPWM(MOTORE_SX_A, v); gpioWrite(MOTORE_SX_B, 0);
    gpioWrite(MOTORE_DX_A, 0); gpioPWM(MOTORE_DX_B, v);
    printf("Gira a Destra (perno centrale)\n");
}

void sinistra(int v) {
    gpioWrite(MOTORE_SX_A, 0); gpioPWM(MOTORE_SX_B, v);
    gpioPWM(MOTORE_DX_A, v); gpioWrite(MOTORE_DX_B, 0);
    printf("Gira a Sinistra (perno centrale)\n");
}

int main() {
    if (gpioInitialise() < 0) return 1;

    // Configurazione iniziale
    gpioSetMode(MOTORE_SX_A, PI_OUTPUT);
    gpioSetMode(MOTORE_SX_B, PI_OUTPUT);
    gpioSetMode(MOTORE_DX_A, PI_OUTPUT);
    gpioSetMode(MOTORE_DX_B, PI_OUTPUT);

    // Impostiamo una velocità media (es. 160 su 255)
    int v_crociera = 160;

    printf("Inizio test motori con PWM...\n");

    avanti(v_crociera);   sleep(1);
    destra(v_crociera);   sleep(1);
    indietro(v_crociera); sleep(1);
    sinistra(v_crociera); sleep(1);
    
    // Test accelerazione
    printf("Test accelerazione graduale...\n");
    for(int i=0; i<=255; i+=50) {
        avanti(i);
        usleep(200000); // 0.2 secondi
    }

    stop_car();
    gpioTerminate();
    return 0;
}