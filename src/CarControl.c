#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include "wiiuse.h"

#define RIGHT_MOTOR_PIN_A 22
#define RIGHT_MOTOR_PIN_B 23
#define LEFT_MOTOR_PIN_A 18
#define LEFT_MOTOR_PIN_B 17
#define MAX_WIIMOTES 1
#define FORWARD 1
#define REVERSE -1
#define PWM_MAX 255
#define PWM_MIN 50
#define PITCH_DEADZONE 10.0f

int leftMotorSpeed = 0;
int rightMotorSpeed = 0;
int rumbleActive = 1;
struct wiimote_t **wiimotes;

/******************************************************************************
 *                             UTILITY FUNCTIONS                              *
 ******************************************************************************/

/**
 * @brief Clamps an integer value within a specified range.
 *
 * This function restricts the given integer input `v` to the range defined by
 * `lo` and `hi`. If `v` is less than `lo`, the function returns `lo`. If `v` is
 * greater than `hi`, it returns `hi`. Otherwise, it returns `v` unchanged.
 *
 * @param v The value to clamp.
 * @param lo The lower bound of the range.
 * @param hi The upper bound of the range.
 * @return The clamped value, ensuring it falls within [lo, hi].
 */
int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Clears the console terminal screen.
 *
 * This function checks if the standard output (stdout) is associated with a 
 * terminal (TTY). If it is, it sends ANSI escape codes to clear the screen 
 * ("\033[2J") and move the cursor to the top-left position ("\033[H").
 * It then flushes the output buffer to ensure the action is applied immediately.
 *
 * @note This function has no effect if the output is redirected to a file or pipe.
 */
void clear_terminal() {
    if (isatty(fileno(stdout))) {
        printf("\033[2J\033[H");
        fflush(stdout);
    }
}

/******************************************************************************
 *                               SETUP FUNCTIONS                              *
 ******************************************************************************/

/**
 * @brief Initializes the GPIO library and sets up motor control pins.
 * 
 * This function attempts to initialize the pigpio library. If initialization
 * fails, it prints an error message to stdout and returns 0 (false).
 * Upon successful initialization, it configures the GPIO pins for both the
 * left and right motors as output pins.
 * 
 * @return int Returns 1 on successful initialization, or 0 if the GPIO
 *             initialization fails.
 */
int setup() {
    if (gpioInitialise() < 0) {
        printf("Failed to initialize GPIO\n");
        return 0;
    }

    gpioSetMode(LEFT_MOTOR_PIN_A, PI_OUTPUT);
    gpioSetMode(LEFT_MOTOR_PIN_B, PI_OUTPUT);
    
    gpioSetMode(RIGHT_MOTOR_PIN_A, PI_OUTPUT);
    gpioSetMode(RIGHT_MOTOR_PIN_B, PI_OUTPUT);
    
    return 1;
}

/**
 * @brief Configure a Wii controller on startup.
 *
 * @details
 * Performs an initial tactile feedback (short rumble) to indicate the controller
 * has been detected and then enables motion sensing on the given controller.
 *
 * This function is blocking: it turns on rumble for approximately 500 milliseconds
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
    usleep(500000);
    wiiuse_rumble(controller, 0);
    wiiuse_motion_sensing(controller, 1);
}

int verify_accelerometer(struct wiimote_t *controller) {
    if (!controller) {
        return 0;
    }
    wiiuse_poll(wiimotes, MAX_WIIMOTES);
    return  controller->orient.pitch == 0.0f;
}


/******************************************************************************
 *                              RUMBLE FUNCTIONS                              *
 ******************************************************************************/

/**
 * @brief Toggles the status of the rumble feature.
 *
 * This function inverts the current boolean state of the 'rumbleActive'
 * global variable, effectively enabling or disabling the rumble (vibration)
 * feedback.
 */
void change_rumble_status() {
    rumbleActive = !rumbleActive;
}

/**
 * @brief Manages the rumble feature of the Wiimote controller.
 *
 * This function checks the `rumbleActive` state and enables the 
 * rumble functionality on the specified Wiimote controller if active.
 *
 * @param controller A pointer to the `wiimote_t` structure representing the controller.
 */
void handle_rumble(struct wiimote_t *controller) {
    if(rumbleActive) {
        wiiuse_rumble(controller, 1);
    }
}

/******************************************************************************
 *                               MOTOR FUNCTIONS                              *
 ******************************************************************************/

/**
 * @brief Calculates the left and right motor speeds based on Wiimote pitch orientation.
 *
 * This function determines steering by analyzing the pitch of the connected Wiimote.
 * It applies a deadzone to ignore small movements, normalizes the pitch value, and
 * uses a linear function to provide proportional steering.
 *
 * The resulting speed delta is applied inversely to the left and right motors
 * relative to a base speed to initiate a turn.
 *
 * @param controller Pointer to the wiimote_t structure containing current orientation data.
 * @return int* A pointer to an array containing two integers: [left_motor_speed, right_motor_speed].
 *              Note: This returns a pointer to a compound literal, which has automatic storage duration
 *              if unrelated to a static context. Use with caution outside the immediate scope.
 */
int* calculate_steering_speeds(struct wiimote_t *controller) {
    int baseSpeed = 180;

    float pitch = controller->orient.pitch;

    if (fabs(pitch) < PITCH_DEADZONE)
        pitch = 0;

    float steering = clamp_int((int)pitch, -100, 100) / 100.0;

    int maxDelta = baseSpeed - PWM_MIN;

    int delta = (int)(steering * maxDelta);

    int left  = baseSpeed + delta;
    int right = baseSpeed - delta;

    left  = clamp_int(left,  PWM_MIN, PWM_MAX);
    right = clamp_int(right, PWM_MIN, PWM_MAX);

    return (int[]){left, right};
}

/**
 * @brief Updates the speeds of the left and right motors by setting PWM values on the specified GPIO pins.
 *
 * This function controls a differential drive system by setting the PWM duty cycle for active motor pins
 * while ensuring the inactive motor pins are turned off (0 duty cycle). This is typically used to handle
 * direction changes where one set of pins drives forward and another set drives backward, or to simply
 * apply speed to the currently active direction.
 *
 * @param leftActiveMotorPin The GPIO pin number for the active left motor channel.
 * @param rightActiveMotorPin The GPIO pin number for the active right motor channel.
 * @param leftDeactiveMotorPin The GPIO pin number for the inactive left motor channel (will be set to 0).
 * @param rightDeactiveMotorPin The GPIO pin number for the inactive right motor channel (will be set to 0).
 * @param leftSpeed The PWM duty cycle value (speed) for the left motor.
 * @param rightSpeed The PWM duty cycle value (speed) for the right motor.
 */
void update_motor_speeds(int leftActiveMotorPin, int rightActiveMotorPin, int leftDeactiveMotorPin, int rightDeactiveMotorPin, int leftSpeed, int rightSpeed) {
    gpioPWM(leftActiveMotorPin, leftSpeed);
    gpioPWM(rightActiveMotorPin, rightSpeed);
    gpioPWM(leftDeactiveMotorPin, 0);
    gpioPWM(rightDeactiveMotorPin, 0);
}

/**
 * @brief Handles the acceleration and direction control of the car based on Wiimote input.
 *
 * This function calculates the appropriate steering speeds for the left and right motors
 * based on the controller's state. It then updates the motor speeds and directions
 * according to the requested movement direction.
 *
 * @param controller A pointer to the wiimote_t structure containing the controller's current state (accelerometer/buttons).
 * @param direction The target direction for the car movement.
 *                  Expected values are:
 *                  - FORWARD: Moves the car forward with calculated steering.
 *                  - REVERSE: Moves the car backward with calculated steering.
 *                  - Any other value results in stopping the motors.
 *
 * @note This function updates the global variables leftMotorSpeed and rightMotorSpeed.
 *       It assumes the existence of helper functions calculate_steering_speeds and update_motor_speeds,
 *       as well as motor pin definitions (LEFT_MOTOR_PIN_A, etc.).
 */
void handle_acceleration(struct wiimote_t *controller, int direction) {
    int *speeds = calculate_steering_speeds(controller);
    int left = speeds[0];
    int right = speeds[1];

    switch(direction) {
        case FORWARD:
            update_motor_speeds(LEFT_MOTOR_PIN_A, RIGHT_MOTOR_PIN_A, LEFT_MOTOR_PIN_B, RIGHT_MOTOR_PIN_B, left, right);
            leftMotorSpeed = left;
            rightMotorSpeed = right;
            break;

        case REVERSE:
            update_motor_speeds(LEFT_MOTOR_PIN_B, RIGHT_MOTOR_PIN_B, LEFT_MOTOR_PIN_A, RIGHT_MOTOR_PIN_A, left, right);
            leftMotorSpeed = -left;
            rightMotorSpeed = -right;
            break;

        default:
            update_motor_speeds(LEFT_MOTOR_PIN_A, RIGHT_MOTOR_PIN_A, LEFT_MOTOR_PIN_B, RIGHT_MOTOR_PIN_B, 0, 0);
            leftMotorSpeed = 0;
            rightMotorSpeed = 0;
            break;
    }
}

/******************************************************************************
 *                            CONNECTION FUNCTIONS                            *
 ******************************************************************************/

/**
 * @brief Attempt to find and connect to one or more Wiimotes.
 *
 * @details
 * Prompts the user to put the Wiimote in discoverable mode (press 1+2), calls wiiuse_find with a
 * 5-second timeout, and then calls wiiuse_connect. If no devices are found or no connections succeed,
 * the function returns 0. On success it returns 1.
 *
 * @return 1 if at least one Wiimote was found and successfully connected; 0 otherwise.
 */
int connect_wiimote() {
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

/**
 * @brief Attempt to discover and connect one Wiimote and initialize a controller.
 *
 * @details
 * This routine delegates device discovery and connection to connect_wiimote(). If
 * connect_wiimote() fails to find or open a Wiimote, an error message is printed to stdout
 * and no further initialization is performed. On successful connection, a success message is
 * printed and setup_controller() is invoked to perform controller-specific initialization for
 * the provided controller object.
 *
 * The function does not allocate or free wiimote objects itself; it relies on
 * connect_wiimote() to populate any wiimote array or related structures and on callers or
 * other subsystems to manage their lifetime.
 *
 * @param controller Pointer to a valid wiimote_t structure which will be passed to
 *                   setup_controller() for initialization after a successful connection.
 *                   The caller must ensure this pointer remains valid for the duration of
 *                   setup_controller() and any subsequent use.
 *
 * @note This function prints status and error messages to stdout for connection success/failure.
 * @note Side effects include invoking setup_controller() and any initialization it performs.
 */
void handle_connection_phase(struct wiimote_t *controller) {
    if(!connect_wiimote()) {
        printf("Failed to connect to Wiimote\n");
    } else {
        printf("Wiimote connected!\n");
        setup_controller(controller);
        printf("Verify connection\n");
        while(verify_accelerometer(controller) && WIIMOTE_IS_CONNECTED(controller)) {
            printf("Fixing connection...\n");
            printf("Pitch: %f\n", controller->orient.pitch);
            wiiuse_set_leds(controller, WIIMOTE_LED_2);
            wiiuse_motion_sensing(controller, 0);
            sleep(1);
            wiiuse_motion_sensing(controller, 1);
        }
    }
}

/******************************************************************************
 *                            MAIN CONTROL FUNCTIONS                          *
 ******************************************************************************/

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
        handle_acceleration(controller, REVERSE);
    } else if (IS_PRESSED(controller, WIIMOTE_BUTTON_TWO)) {
        //printf("Forward button pressed\n");
        handle_rumble(controller);
        handle_acceleration(controller, FORWARD);
    } else if (IS_JUST_PRESSED(controller, WIIMOTE_BUTTON_PLUS)) {
        change_rumble_status();
    } else {
        wiiuse_rumble(controller, 0); 
        handle_acceleration(controller, 0);
    } 
    
    return 1; // Return 1 to indicate that the program should continue running
}

int main() {
    if(!setup()) {
        printf("Failed to setup GPIO\n");
        return 1;
    }
   
    wiimotes = wiiuse_init(MAX_WIIMOTES);;
    struct wiimote_t *controller = wiimotes[0];

    while(1) {
        while(!WIIMOTE_IS_CONNECTED(controller)) {
            printf("No Wiimote connected. Attempting to connect...\n");
            handle_connection_phase(controller);
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
    
    wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
    return 0;
}