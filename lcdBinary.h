#ifndef LCD_BINARY_H
#define LCD_BINARY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifndef TRUE
#define TRUE (1 == 1)
#define FALSE (1 == 2)
#endif

#define PAGE_SIZE (4 * 1024)
#define BLOCK_SIZE (4 * 1024)

#define INPUT 0
#define OUTPUT 1

#define LOW 0
#define HIGH 1

// Wiring (see call to lcdInit in main, using BCM numbering)
#define STRB_PIN 24
#define RS_PIN 25
#define DATA0_PIN 23
#define DATA1_PIN 10
#define DATA2_PIN 27
#define DATA3_PIN 22

// Function prototypes
int failure(int fatal, const char *message, ...);
void digitalWrite(uint32_t *gpio, int pin, int value);
void pinMode(uint32_t *gpio, int pin, int mode);
void writeLED(uint32_t *gpio, int led, int value);
int readButton(uint32_t *gpio, int button);
void waitForButton(uint32_t *gpio, int button);

#endif /* LCD_BINARY_H */