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
int rumbleActive = 1;

int clamp_int(int v, int lo, int hi) {
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

// TODO: Docs
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

/**
 * @brief Configure a Wii controller on startup.
 *
 * @details
 * Performs an initial tactile feedback (short rumble) to indicate the controller
 * has been detected and then enables motion sensing on the given controller.
 *
 * This function is blocking: it turns on rumble for approximately one second
 * and then disables it before enabling motion sensing. It relies on the
 * underlying wiiuse library for actual hardware interaction.
 *
 * @param controller Pointer to an initialized wiimote_t representing the controller.
 *                   Must not be NULL.
 *
 * @note The function does not perform NULL checks; callers must ensure a valid
 *       controller pointer is passed. Any error handling for wiiuse functions
 *       (wiiuse_rumble, wiiuse_motion_sensing) is delegated to those functions.
 */
void setup_controller(struct wiimote_t *controller) {
    wiiuse_rumble(controller, 1);
    sleep(1);
    wiiuse_rumble(controller, 0);
    wiiuse_motion_sensing(controller, 1);
}

/**
 * @brief Attempt to find and connect to one or more Wiimotes.
 *
 * @details
 * Prompts the user to put the Wiimote in discoverable mode (press 1+2), calls wiiuse_find with a
 * 5-second timeout, and then calls wiiuse_connect. If no devices are found or no connections succeed,
 * the function returns 0. On success it returns 1.
 * 
 * @param wiimotes Pointer to an array of wiimote_t* that will be populated by wiiuse_find/wiiuse_connect.
 *                  The caller must provide an array capable of holding MAX_WIIMOTES pointers.
 *
 * @return 1 if at least one Wiimote was found and successfully connected; 0 otherwise.
 */
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

void handle_rumble(struct wiimote_t *controller) {
    if(rumbleActive) {
        wiiuse_rumble(controller, 1);
    }
}

void change_rumble_status() {
    rumbleActive = !rumbleActive;
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

/**
 * @brief Process a single input event from a Wiimote and update car controls.
 *
 * @details
 * Inspects the provided wiimote controller state using the IS_JUST_PRESSED and IS_PRESSED
 * macros to determine which button (if any) is active. Behavior:
 *  - WIIMOTE_BUTTON_HOME (just-pressed): logs a message to stdout and signals the caller to exit.
 *  - WIIMOTE_BUTTON_ONE (pressed): enables rumble and requests a negative acceleration step
 *    (calls handle_acceleration with -50) to move the car backward.
 *  - WIIMOTE_BUTTON_TWO (pressed): enables rumble and requests a positive acceleration step
 *    (calls handle_acceleration with 50) to move the car forward.
 *  - No relevant buttons pressed: disables rumble and requests zero acceleration (calls
 *    handle_acceleration with 0).
 *
 * The function uses wiiuse_rumble to toggle controller rumble and may print status messages
 * to stdout. It is intended to be called repeatedly in an event loop.
 *
 * @param controller Pointer to a wiimote_t structure representing the current controller state.
 *
 * @return 0 if the HOME button was just pressed (request to exit); 1 otherwise (continue running).
 */
int handle_event(struct wiimote_t *controller) {
    if (IS_JUST_PRESSED(controller, WIIMOTE_BUTTON_HOME)) {
        printf("Home button pressed\n");
        return 0; // Return 0 to indicate that the program should exit
    } else if (IS_PRESSED(controller, WIIMOTE_BUTTON_ONE)) {
        //printf("Backward button pressed\n");
        handle_rumble(controller);
        handle_acceleration(controller, -50);
    } else if (IS_PRESSED(controller, WIIMOTE_BUTTON_TWO)) {
        //printf("Forward button pressed\n");
        handle_rumble(controller);
        handle_acceleration(controller, 50);
    } else if (IS_JUST_PRESSED(controller, WIIMOTE_BUTTON_PLUS)) {
        change_rumble_status();
    } else {
        wiiuse_rumble(controller, 0); 
        handle_acceleration(controller, 0);
    } 
    
    return 1; // Return 1 to indicate that the program should continue running
}

/**
 * @brief Handle the Wiimote connection phase and initialize the controller on success.
 *
 * @details
 * Calls connect_wiimote() to attempt to find and connect to one or more Wiimotes. If the
 * connection attempt fails, an error message is printed to stdout. If a connection is
 * established, a success message is printed and setup_controller() is invoked to perform
 * controller-specific initialization for the provided controller object.
 *
 * This function delegates device discovery/connection and population of the wiimotes array
 * to connect_wiimote(); it does not itself manage allocation or cleanup of Wiimote objects.
 *
 * @param controller Pointer to a wiimote_t structure that will be passed to setup_controller()
 *                   for initialization after a successful connection. Must be valid when
 *                   setup_controller() is called.
 *
 * @param wiimotes   Pointer to an array (or pointer-to-pointer) of wiimote_t pointers that is
 *                   forwarded to connect_wiimote(). The connect_wiimote() implementation is
 *                   expected to populate this array on success; the caller is responsible for
 *                   providing sufficient storage and for any subsequent cleanup.
 */
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
            printf("Left Motor Speed: %d, Right Motor Speed: %d Rumble: %d\n", leftMotorSpeed, rightMotorSpeed, rumbleActive);
            if(!handle_event(controller)) {
                break;
            }
        }
    };
    
    //TODO: initialize the loop. 
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
    return 0;
}