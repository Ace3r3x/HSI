/**
 * lcdBinary.h - Hardware control functions for Raspberry Pi
 * Used for GPIO, LEDs, buttons, and LCD display in MasterMind game
 */

 #ifndef LCD_BINARY_H
 #define LCD_BINARY_H
 
 #include <stdio.h>    /* Standard I/O */
 #include <stdlib.h>   /* Standard library */
 #include <stdint.h>   /* Integer types */
 #include <sys/types.h> /* System types */
 #include <time.h>     /* Time functions */
 
 /* Boolean constants */
 #ifndef TRUE
 #define TRUE (1 == 1)
 #define FALSE (1 == 2)
 #endif
 
 /* Memory mapping constants */
 #define PAGE_SIZE (4 * 1024)
 #define BLOCK_SIZE (4 * 1024)
 
 /* GPIO pin modes */
 #define INPUT 0
 #define OUTPUT 1
 
 /* GPIO pin states */
 #define LOW 0
 #define HIGH 1
 
 /* LCD wiring (BCM GPIO numbering) */
 #define STRB_PIN 24
 #define RS_PIN 25
 #define DATA0_PIN 23
 #define DATA1_PIN 10
 #define DATA2_PIN 27
 #define DATA3_PIN 22
 
 /* Basic hardware control functions */
 int failure(int fatal, const char *message, ...);  /* Report error condition */
 void digitalWrite(uint32_t *gpio, int pin, int value);  /* Set pin state */
 void pinMode(uint32_t *gpio, int pin, int mode);  /* Set pin mode */
 void writeLED(uint32_t *gpio, int led, int value);  /* Control LED */
 int readButton(uint32_t *gpio, int button);  /* Read button state */
 void waitForButton(uint32_t *gpio, int button);  /* Wait for button press */
 
 /* Advanced button handling */
 int detectButtonPress(uint32_t *gpio, int button);  /* Detect new press */
 int detectButtonRelease(uint32_t *gpio, int button);  /* Detect release */
 int getButtonInput(uint32_t *gpio, int button, int maxValue, int timeoutSec, int confirmMethod);  /* Get input value */
 void delay(unsigned int howLong);  /* Delay in milliseconds */
 
 #endif /* LCD_BINARY_H */