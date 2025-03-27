/* ***************************************************************************** */
/* Low-level hardware control functions for Raspberry Pi                         */
/* Implements GPIO control for LEDs, buttons and LCD devices                     */
/* Uses inline ARM assembly for direct hardware access                           */
/* ***************************************************************************** */

#include "lcdBinary.h"

// -----------------------------------------------------------------------------
// GPIO control functions

/* this version needs gpio as argument, because it is in a separate file */
void digitalWrite(uint32_t *gpio, int pin, int value) {
    int offset = pin / 32;
    int shift = pin % 32;
    
    if (value == LOW) {
        /* Use GPCLR register to clear the pin */
        asm volatile (
            "mov r3, #1 \n\t"
            "lsl r3, r3, %[shift] \n\t"
            "str r3, [%[gpio], #40] \n\t"
            :
            : [gpio] "r" (gpio + offset), [shift] "r" (shift)
            : "r3"
        );
    } else {
        /* Use GPSET register to set the pin */
        asm volatile (
            "mov r3, #1 \n\t"
            "lsl r3, r3, %[shift] \n\t"
            "str r3, [%[gpio], #28] \n\t"
            :
            : [gpio] "r" (gpio + offset), [shift] "r" (shift)
            : "r3"
        );
    }
}

// adapted from setPinMode
void pinMode(uint32_t *gpio, int pin, int mode) {
    int fSel = pin / 10;
    int shift = (pin % 10) * 3;
    
    asm volatile (
        /* Read current value of the GPFSEL register */
        "ldr r3, [%[gpio], %[fSel], lsl #2] \n\t"
        
        /* Clear the 3 bits for this pin */
        "mov r2, #7 \n\t"                      /* 7 = 0b111 */
        "lsl r2, r2, %[shift] \n\t"            /* Shift to pin position */
        "bic r3, r3, r2 \n\t"                  /* Clear the 3 bits */
        
        /* Set the mode bits */
        "mov r2, %[mode] \n\t"                 /* Get mode value */
        "lsl r2, r2, %[shift] \n\t"            /* Shift to pin position */
        "orr r3, r3, r2 \n\t"                  /* Set the bits */
        
        /* Write back to the GPFSEL register */
        "str r3, [%[gpio], %[fSel], lsl #2] \n\t"
        :
        : [gpio] "r" (gpio), [fSel] "r" (fSel), [shift] "r" (shift), [mode] "r" (mode)
        : "r2", "r3", "cc"
    );
}

void writeLED(uint32_t *gpio, int led, int value) {
    /* Set the pin as OUTPUT */
    pinMode(gpio, led, OUTPUT);
    
    /* Set the pin value */
    digitalWrite(gpio, led, value);
}

int readButton(uint32_t *gpio, int button) {
    int result;
    int offset = button / 32;
    int shift = button % 32;
    
    /* Set the pin as INPUT */
    pinMode(gpio, button, INPUT);
    
    /* Read the pin value from GPLEV register */
    asm volatile (
        "mov r3, #1 \n\t"
        "lsl r3, r3, %[shift] \n\t"
        "ldr r2, [%[gpio], #52] \n\t"          /* Read GPLEV0 register */
        "and r2, r2, r3 \n\t"                  /* Mask with button bit */
        "cmp r2, #0 \n\t"                      /* Compare with 0 */
        "moveq %[result], #0 \n\t"             /* If 0, button is not pressed */
        "movne %[result], #1 \n\t"             /* If not 0, button is pressed */
        : [result] "=r" (result)
        : [gpio] "r" (gpio + offset), [shift] "r" (shift)
        : "r2", "r3", "cc"
    );
    
    return result;
}

// -----------------------------------------------------------------------------
// Button handling helper functions

/* Detect a button press with debouncing */
int detectButtonPress(uint32_t *gpio, int button) {
    static int prevState = LOW;
    int currState = readButton(gpio, button);
    struct timespec sleeper, dummy;
    
    /* If button state changed from not pressed to pressed */
    if (currState == HIGH && prevState == LOW) {
        /* Debounce delay */
        sleeper.tv_sec = 0;
        sleeper.tv_nsec = 50 * 1000000; /* 50ms */
        nanosleep(&sleeper, &dummy);
        
        /* Check if button is still pressed */
        currState = readButton(gpio, button);
        if (currState == HIGH) {
            prevState = HIGH;
            return 1; /* Button press detected */
        }
    } else if (currState == LOW && prevState == HIGH) {
        /* Button was released */
        prevState = LOW;
    }
    
    return 0; /* No new button press */
}

/* Detect a button release with debouncing */
int detectButtonRelease(uint32_t *gpio, int button) {
    static int prevState = HIGH;
    int currState = readButton(gpio, button);
    struct timespec sleeper, dummy;
    
    /* If button state changed from pressed to not pressed */
    if (currState == LOW && prevState == HIGH) {
        /* Debounce delay */
        sleeper.tv_sec = 0;
        sleeper.tv_nsec = 50 * 1000000; /* 50ms */
        nanosleep(&sleeper, &dummy);
        
        /* Check if button is still released */
        currState = readButton(gpio, button);
        if (currState == LOW) {
            prevState = LOW;
            return 1; /* Button release detected */
        }
    } else if (currState == HIGH && prevState == LOW) {
        /* Button was pressed */
        prevState = HIGH;
    }
    
    return 0; /* No new button release */
}

/* Get input value using button presses */
int getButtonInput(uint32_t *gpio, int button, int maxValue, int timeoutSec, int confirmMethod) {
    int value = 1; /* Start with value 1 */
    int confirmed = 0;
    time_t startTime = time(NULL);
    time_t currentTime;
    time_t lastPressTime = 0;
    int pressCount = 0;
    int longPressDetected = 0;
    struct timespec sleeper, dummy;
    
    /* Reset button state */
    while (readButton(gpio, button) == HIGH) {
        sleeper.tv_sec = 0;
        sleeper.tv_nsec = 10 * 1000000;
        nanosleep(&sleeper, &dummy);
    }
    
    while (!confirmed) {
        currentTime = time(NULL);
        
        /* Check for timeout */
        if (timeoutSec > 0 && (currentTime - startTime) >= timeoutSec) {
            return value; /* Return current value on timeout */
        }
        
        /* Check for button press */
        if (detectButtonPress(gpio, button)) {
            /* Button was pressed, increment value */
            value = (value % maxValue) + 1;
            
            /* Reset timeout on button press */
            startTime = time(NULL);
            
            /* For double-press detection */
            if (confirmMethod == 2) {
                if (pressCount == 0 || (currentTime - lastPressTime) > 1) {
                    /* First press or too much time passed */
                    pressCount = 1;
                } else {
                    /* Second press within 1 second */
                    pressCount++;
                }
                lastPressTime = currentTime;
            }
            
            /* Wait for button release with long-press detection */
            time_t pressStartTime = time(NULL);
            while (readButton(gpio, button) == HIGH) {
                /* For long-press detection */
                if (confirmMethod == 1 && (time(NULL) - pressStartTime) >= 1) {
                    longPressDetected = 1;
                }
                
                sleeper.tv_sec = 0;
                sleeper.tv_nsec = 10 * 1000000;
                nanosleep(&sleeper, &dummy);
            }
            
            /* Handle confirmation methods */
            if (confirmMethod == 1 && longPressDetected) {
                confirmed = 1; /* Long press confirmation */
            } else if (confirmMethod == 2 && pressCount >= 2) {
                confirmed = 1; /* Double press confirmation */
            }
        }
        
        /* Small delay to prevent CPU hogging */
        sleeper.tv_sec = 0;
        sleeper.tv_nsec = 10 * 1000000;
        nanosleep(&sleeper, &dummy);
    }
    
    return value;
}

void waitForButton(uint32_t *gpio, int button) {
    /* Simple implementation using readButton */
    int prevState = 0;
    int currState;
    int debounceTime = 50; /* milliseconds */
    struct timespec sleeper, dummy;
    
    while (1) {
        currState = readButton(gpio, button);
        
        /* If button state changed from not pressed to pressed */
        if (currState == HIGH && prevState == LOW) {
            /* Debounce delay */
            sleeper.tv_sec = 0;
            sleeper.tv_nsec = debounceTime * 1000000;
            nanosleep(&sleeper, &dummy);
            
            /* Check if button is still pressed */
            currState = readButton(gpio, button);
            if (currState == HIGH) {
                /* Wait for button release */
                while (readButton(gpio, button) == HIGH) {
                    sleeper.tv_sec = 0;
                    sleeper.tv_nsec = 10 * 1000000;
                    nanosleep(&sleeper, &dummy);
                }
                break;
            }
        }
        
        prevState = currState;
        
        /* Small polling delay */
        sleeper.tv_sec = 0;
        sleeper.tv_nsec = 10 * 1000000;
        nanosleep(&sleeper, &dummy);
    }
}