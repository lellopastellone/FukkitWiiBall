#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>
#include "wiiuse.h"

#define LEFT_MOTOR_PIN 17 //TODO: set the correct GPIO pin for the left motor
#define RIGHT_MOTOR_PIN 18 //TODO: set the correct GPIO pin for the right motor
#define PWM_FREQUENCY 20000 // 20 kHz for motor control
#define PWM_RANGE 255 // 8-bit resolution for PWM
#define CONNECT_PIN 1 //TODO: set the correct GPIO pin for the connect button
#define MAX_WIIMOTES 1

int leftMotorSpeed = 0;
int rightMotorSpeed = 0;

int setup() {
    if (gpioInitialise() < 0) {
        printf("Failed to initialize GPIO\n");
        return 0;
    }

    gpioSetMode(LEFT_MOTOR_PIN, PI_OUTPUT);
    gpioSetMode(RIGHT_MOTOR_PIN, PI_OUTPUT);
    gpioSetMode(CONNECT_PIN, PI_INPUT);

    gpioSetPWMfrequency(LEFT_MOTOR_PIN, PWM_FREQUENCY);
    gpioSetPWMfrequency(RIGHT_MOTOR_PIN, PWM_FREQUENCY);

    gpioSetPWMrange(LEFT_MOTOR_PIN, PWM_RANGE);
    gpioSetPWMrange(RIGHT_MOTOR_PIN, PWM_RANGE);
    
    return 1;
}

void setup_controller(struct wiimote_t *controller) {
    wiiuse_rumble(controller, 1);
    sleep(1);
    wiiuse_rumble(controller, 0);
    wiiuse_motion_sensing(controller, 1);
}

int connect_wiimote(struct wiimote_t **wiimotes) {
    int connected, found;
    printf("Press 1+2 on your Wiimote now...\n");
    found = wiiuse_find(wiimotes, MAX_WIIMOTES, 5);
    if (!found) {
        return 0;
    }

    connected = wiiuse_connect(wiimotes, MAX_WIIMOTES);
    if (!connected) {
        return 0;
    }

    return 1;
}

static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void clear_terminal() {
    if (isatty(fileno(stdout))) {
        printf("\033[2J\033[H");
        fflush(stdout);
    }
}

void handle_acceleration(struct wiimote_t *controller, int acceleration) {
    //TODO: handle torque vectoring
    if (acceleration > 0) {
        leftMotorSpeed = clamp_int(acceleration + ((int)(controller->orient.pitch)), 0, 100);
        rightMotorSpeed = clamp_int(acceleration - ((int)(controller->orient.pitch)), 0, 100);
    } else if (acceleration < 0) {
        leftMotorSpeed = clamp_int(acceleration - ((int)(controller->orient.pitch)), -100, 0);
        rightMotorSpeed = clamp_int(acceleration + ((int)(controller->orient.pitch)), -100, 0);
    } else {
        leftMotorSpeed = 0;
        rightMotorSpeed = 0;
    }
}

int handle_event(struct wiimote_t *controller) {
    if (IS_JUST_PRESSED(controller, WIIMOTE_BUTTON_HOME)) {
        printf("Home button pressed\n");
        return 0; // Return 0 to indicate that the program should exit
    } else if (IS_PRESSED(controller, WIIMOTE_BUTTON_ONE)) {
        //printf("Backward button pressed\n");
        wiiuse_rumble(controller, 1);
        handle_acceleration(controller, -50);
    } else if (IS_PRESSED(controller, WIIMOTE_BUTTON_TWO)) {
        //printf("Forward button pressed\n");
        wiiuse_rumble(controller, 1);
        handle_acceleration(controller, 50);
    } else {
        wiiuse_rumble(controller, 0); 
        handle_acceleration(controller, 0);
    } 
    
    return 1; // Return 1 to indicate that the program should continue running
}

void handle_connection_phase(struct wiimote_t *controller, struct wiimote_t **wiimotes) {
    if(!connect_wiimote(wiimotes)) {
        printf("Failed to connect to Wiimote\n");
    } else {
        printf("Wiimote connected!\n");
        setup_controller(controller);
    }
}

int main() {
    /*if(!setup()) {
        printf("Failed to setup GPIO\n");
        return 1;
    }*/

    int rightSpeed = 0;
    int leftSpeed = 0;
    struct wiimote_t **wiimotes = wiiuse_init(MAX_WIIMOTES);;
    struct wiimote_t *controller = wiimotes[0];



    while(1) {
        if (!WIIMOTE_IS_CONNECTED(controller)) {
            printf("No Wiimote connected. Attempting to connect...\n");
            handle_connection_phase(controller, wiimotes);
            wiiuse_set_leds(controller, WIIMOTE_LED_1);

        }

        if (wiiuse_poll(wiimotes, MAX_WIIMOTES)) {
            clear_terminal();
            printf("Left Motor Speed: %d, Right Motor Speed: %d\n", leftMotorSpeed, rightMotorSpeed);
            if(!handle_event(controller)) {
                break;
            }
        }
    };
    
    //TODO: initialize the loop. 
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
    return 0;
}