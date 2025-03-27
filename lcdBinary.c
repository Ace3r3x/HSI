#include "lcdBinary.h"

/* 
 * Low-level hardware control functions for Raspberry Pi
 * Implemented using inline assembly
 */

/* Send a value (LOW or HIGH) to a GPIO pin */
void digitalWrite(uint32_t *gpio, int pin, int value)
{
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

/* Set the mode of a GPIO pin to INPUT or OUTPUT */
void pinMode(uint32_t *gpio, int pin, int mode)
{
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

/* Control an LED connected to a GPIO pin */
void writeLED(uint32_t *gpio, int led, int value)
{
    /* Set the pin as OUTPUT */
    pinMode(gpio, led, OUTPUT);
    
    /* Set the pin value */
    digitalWrite(gpio, led, value);
}

/* Read the state of a button connected to a GPIO pin */
int readButton(uint32_t *gpio, int button)
{
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

/* Wait for a button press with debouncing */
void waitForButton(uint32_t *gpio, int button)
{
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