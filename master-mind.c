/*
 * MasterMind implementation: template; see comments below on which parts need to be completed
 * CW spec: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf
 * This repo: https://gitlab-student.macs.hw.ac.uk/f28hs-2021-22/f28hs-2021-22-staff/f28hs-2021-22-cwk2-sys

 * Compile: 
 gcc -c -o lcdBinary.o lcdBinary.c
 gcc -c -o master-mind.o master-mind.c
 gcc -o master-mind master-mind.o lcdBinary.o
 * Run:     
 sudo ./master-mind

 OR use the Makefile to build
 > make all
 and run
 > make run
 and test
 > make test

 ***********************************************************************
 * The Low-level interface to LED, button, and LCD is based on:
 * wiringPi libraries by
 * Copyright (c) 2012-2013 Gordon Henderson.
 ***********************************************************************
 * See:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
*/

/* ======================================================= */
/* SECTION: includes                                       */
/* ------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <unistd.h>
#include <string.h>
#include <time.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <signal.h>

#include "lcdBinary.h"
#include <ctype.h>

/* --------------------------------------------------------------------------- */
/* Config settings */
/* you can use CPP flags to e.g. print extra debugging messages */
/* or switch between different versions of the code e.g. digitalWrite() in Assembler */
#define DEBUG
#undef ASM_CODE

// =======================================================
// Tunables
// PINs (based on BCM numbering)
// For wiring see CW spec: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf
// GPIO pin for green LED
#define LED 26
// GPIO pin for red LED
#define LED2 5
// GPIO pin for button
#define BUTTON 19
// =======================================================
// delay for loop iterations (mainly), in ms
// in mili-seconds: 0.2s
#define DELAY   200
// in micro-seconds: 3s
#define TIMEOUT 3000000
// in seconds: time window for button input
#define INPUT_TIMEOUT 5  

// =======================================================
// APP constants   ---------------------------------
// number of colours, length of the sequence and maximum number of attempts
#define COLS 3
#define SEQL 3
#define MAX_ATTEMPTS 5

// =======================================================

// generic constants

#ifndef	TRUE
#  define	TRUE	(1==1)
#  define	FALSE	(1==2)
#endif

#define	PAGE_SIZE		(4*1024)
#define	BLOCK_SIZE		(4*1024)

#define	INPUT			 0
#define	OUTPUT			 1

#define	LOW			 0
#define	HIGH			 1


// =======================================================
// Wiring (see inlined initialisation routine)

#define STRB_PIN 24
#define RS_PIN   25
#define DATA0_PIN 23
#define DATA1_PIN 10
#define DATA2_PIN 27
#define DATA3_PIN 22

/* ======================================================= */
/* SECTION: constants and prototypes                       */
/* ------------------------------------------------------- */

// =======================================================
// char data for the CGRAM, i.e. defining new characters for the display

static unsigned char newChar [8] = 
{
  0b11111,
  0b10001,
  0b10001,
  0b10101,
  0b11111,
  0b10001,
  0b10001,
  0b11111,
} ;

/* Constants */

static const int colors = COLS;
static const int seqlen = SEQL;

static char* color_names[] = { "red", "green", "blue" };

static int* theSeq = NULL;

static int *seq1, *seq2, *cpy1, *cpy2;

/* --------------------------------------------------------------------------- */

// data structure holding data on the representation of the LCD
struct lcdDataStruct
{
  int bits, rows, cols ;
  int rsPin, strbPin ;
  int dataPins [8] ;
  int cx, cy ;
} ;

static int lcdControl ;

/* ***************************************************************************** */
/* INLINED fcts from wiringPi/devLib/lcd.c: */
// HD44780U Commands (see Fig 11, p28 of the Hitachi HD44780U datasheet)

#define	LCD_CLEAR	0x01
#define	LCD_HOME	0x02
#define	LCD_ENTRY	0x04
#define	LCD_CTRL	0x08
#define	LCD_CDSHIFT	0x10
#define	LCD_FUNC	0x20
#define	LCD_CGRAM	0x40
#define	LCD_DGRAM	0x80

// Bits in the entry register

#define	LCD_ENTRY_SH		0x01
#define	LCD_ENTRY_ID		0x02

// Bits in the control register

#define	LCD_BLINK_CTRL		0x01
#define	LCD_CURSOR_CTRL		0x02
#define	LCD_DISPLAY_CTRL	0x04

// Bits in the function register

#define	LCD_FUNC_F	0x04
#define	LCD_FUNC_N	0x08
#define	LCD_FUNC_DL	0x10

#define	LCD_CDSHIFT_RL	0x04

// Mask for the bottom 64 pins which belong to the Raspberry Pi
//	The others are available for the other devices

#define	PI_GPIO_MASK	(0xFFFFFFC0)

static unsigned int gpiobase ;
static uint32_t *gpio ;

static int timed_out = 0;

/* ------------------------------------------------------- */
// misc prototypes

int failure(int fatal, const char *message, ...);
void waitForEnter(void);
void waitForButton(uint32_t *gpio, int button);
void cleanupResources(void);
void acknowledgeInput(uint32_t *gpio, int redLED);
void echoInput(uint32_t *gpio, int greenLED, int count);
void signalEndOfInput(uint32_t *gpio, int redLED);
void displayMatchResults(uint32_t *gpio, int greenLED, int redLED, int exact, int approx);
void signalNewRound(uint32_t *gpio, int redLED);
void displaySuccess(uint32_t *gpio, int greenLED, int redLED);
void displaySurnameGreeting(uint32_t *gpio, int redLED, int greenLED, const char *surname, struct lcdDataStruct *lcd);


/* ======================================================= */
/* SECTION: game logic                                     */
/* ------------------------------------------------------- */
/* AUX fcts of the game logic */

/* ********************************************************** */
/* COMPLETE the code for all of the functions in this SECTION */
/* Implement these as C functions in this file                */
/* ********************************************************** */

/* initialise the secret sequence; by default it should be a random sequence */
void initSeq()
{
    int i;
    
    /* Allocate memory for the secret sequence if not already done */
    if (theSeq == NULL) {
        theSeq = (int*)malloc(seqlen * sizeof(int));
        if (theSeq == NULL) {
            fprintf(stderr, "Memory allocation for the Sequence failed\n");
            exit(EXIT_FAILURE);
        }
    }
    
    /* Seed the random number generator */
    srand(time(NULL));
    
    /* Generate random sequence with values between 1 and colors */
    for (i = 0; i < seqlen; i++) {
        theSeq[i] = (rand() % colors) + 1;
    }
}

/* display the sequence on the terminal window, using the format from the sample run in the spec */
void showSeq(int *seq)
{
    int i;
    
    printf("Secret: ");
    for (i = 0; i < seqlen; i++) {
        printf("%d ", seq[i]);
    }
    printf("\n");
}

#define NAN1 8
#define NAN2 9

/* counts how many entries in seq2 match entries in seq1 */
/* returns exact and approximate matches, either both encoded in one value, */
/* or as a pointer to a pair of values */
int countMatches(int *seq1, int *seq2)
{
    int i, j;
    int exact = 0;
    int approx = 0;
    int *used1, *used2;
    
    /* Allocate arrays to track which elements have been matched */
    used1 = (int*)malloc(seqlen * sizeof(int));
    used2 = (int*)malloc(seqlen * sizeof(int));
    
    if (used1 == NULL || used2 == NULL) {
        fprintf(stderr, "Memory allocation failed in countMatches\n");
        exit(EXIT_FAILURE);
    }
    
    /* Initialize arrays */
    for (i = 0; i < seqlen; i++) {
        used1[i] = 0;
        used2[i] = 0;
    }
    
    /* First pass: Count exact matches */
    for (i = 0; i < seqlen; i++) {
        if (seq1[i] == seq2[i]) {
            exact++;
            used1[i] = 1;
            used2[i] = 1;
        }
    }
    
    /* Second pass: Count approximate matches */
    for (i = 0; i < seqlen; i++) {
        if (!used1[i]) {
            for (j = 0; j < seqlen; j++) {
                if (!used2[j] && seq1[i] == seq2[j]) {
                    approx++;
                    used1[i] = 1;
                    used2[j] = 1;
                    break;
                }
            }
        }
    }
    
    /* Free memory */
    free(used1);
    free(used2);
    
    /* Return result encoded: exact in tens, approximate in ones */
    return (exact * 10) + approx;
}

/* show the results from calling countMatches on seq1 and seq1 */
void showMatches(int code, int *seq1, int *seq2, int lcd_format)
{
    int exact = code / 10;
    int approx = code % 10;
    
    if (lcd_format) {
        /* Format for LCD display */
        printf("%d exact\n%d approximate\n", exact, approx);
    } else {
        /* Format for terminal */
        printf("Exact matches: %d\n", exact);
        printf("Approximate matches: %d\n", approx);
    }
}

/* parse an integer value as a list of digits, and put them into @seq@ */
/* needed for processing command-line with options -s or -u            */
void readSeq(int *seq, int val)
{
    int i;
    int temp = val;
    int divisor;
    
    /* Process each digit from left to right */
    for (i = 0; i < seqlen; i++) {
        divisor = 1;
        for (int j = 0; j < seqlen - i - 1; j++) {
            divisor *= 10;
        }
        
        seq[i] = (temp / divisor);
        temp %= divisor;
        
        /* Ensure values are in range 1-colors */
        if (seq[i] < 1 || seq[i] > colors) {
            seq[i] = 1;
        }
    }
}

/* read a guess sequence fron stdin and store the values in arr */
/* only needed for testing the game logic, without button input */
int readNum(int max)
{
    int i;
    int val;
    
    printf("Enter %d numbers (1-%d) separated by spaces:\n", seqlen, max);
    
    for (i = 0; i < seqlen; i++) {
        while (1) {
            if (scanf("%d", &val) != 1) {
                /* Clear input buffer on error */
                while (getchar() != '\n');
                printf("Invalid input. Please enter a number (1-%d): ", max);
                continue;
            }
            
            if (val < 1 || val > max) {
                printf("Number must be between 1 and %d. Try again: ", max);
                continue;
            }
            
            break;
        }
        
        /* Store the valid input */
        seq1[i] = val;
    }
    
    return 0;
}

/* ======================================================= */
/* SECTION: TIMER code                                     */
/* ------------------------------------------------------- */
/* TIMER code */

/* timestamps needed to implement a time-out mechanism */
static uint64_t startT, stopT;

/* ********************************************************** */
/* COMPLETE the code for all of the functions in this SECTION */
/* Implement these as C functions in this file                */
/* ********************************************************** */

/* you may need this function in timer_handler() below  */
/* use the libc fct gettimeofday() to implement it      */
uint64_t timeInMicroseconds()
{
    struct timeval tv;
    uint64_t micros;
    
    gettimeofday(&tv, NULL);
    micros = (uint64_t)(tv.tv_sec) * 1000000 + (uint64_t)(tv.tv_usec);
    
    return micros;
}

/* this should be the callback, triggered via an interval timer, */
/* that is set-up through a call to sigaction() in the main fct. */
void timer_handler(int signum)
{
    stopT = timeInMicroseconds();
    
    /* Check if timeout has occurred */
    if (stopT - startT >= TIMEOUT) {
        timed_out = 1;
        printf("Time out!\n");
    }
}

/* initialise time-stamps, setup an interval timer, and install the timer_handler callback */
void initITimer(uint64_t timeout)
{
    struct sigaction sa;
    struct itimerval timer;
    
    /* Setup the signal handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_handler;
    sigaction(SIGALRM, &sa, NULL);
    
    /* Configure the timer to expire after timeout/1000000 sec */
    timer.it_value.tv_sec = timeout / 1000000;
    timer.it_value.tv_usec = timeout % 1000000;
    
    /* ... and every timeout/1000000 sec after that */
    timer.it_interval.tv_sec = timeout / 1000000;
    timer.it_interval.tv_usec = timeout % 1000000;
    
    /* Start the timer */
    setitimer(ITIMER_REAL, &timer, NULL);
    
    /* Record start time */
    startT = timeInMicroseconds();
    timed_out = 0;
}

/* ======================================================= */
/* SECTION: Aux function                                   */
/* ------------------------------------------------------- */
/* misc aux functions */

int failure (int fatal, const char *message, ...)
{
  va_list argp ;
  char buffer [1024] ;

  if (!fatal) //  && wiringPiReturnCodes)
    return -1 ;

  va_start (argp, message) ;
  vsnprintf (buffer, 1023, message, argp) ;
  va_end (argp) ;

  fprintf (stderr, "%s", buffer) ;
  exit (EXIT_FAILURE) ;

  return 0 ;
}

/*
 * waitForEnter:
 *********************************************************************************
 */

void waitForEnter (void)
{
  printf ("Press ENTER to continue: ") ;
  (void)fgetc (stdin) ;
}

/*
 * delay:
 *	Wait for some number of milliseconds
 *********************************************************************************
 */

void delay (unsigned int howLong)
{
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(howLong / 1000) ;
  sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

  nanosleep (&sleeper, &dummy) ;
}

/* From wiringPi code; comment by Gordon Henderson
 * delayMicroseconds:
 *	This is somewhat intersting. It seems that on the Pi, a single call
 *	to nanosleep takes some 80 to 130 microseconds anyway, so while
 *	obeying the standards (may take longer), it's not always what we
 *	want!
 *
 *	So what I'll do now is if the delay is less than 100uS we'll do it
 *	in a hard loop, watching a built-in counter on the ARM chip. This is
 *	somewhat sub-optimal in that it uses 100% CPU, something not an issue
 *	in a microcontroller, but under a multi-tasking, multi-user OS, it's
 *	wastefull, however we've no real choice )-:
 *
 *      Plan B: It seems all might not be well with that plan, so changing it
 *      to use gettimeofday () and poll on that instead...
 *********************************************************************************
 */

 void delayMicroseconds(unsigned int howLong)
 {
     struct timespec sleeper;
     unsigned int uSecs = howLong % 1000000;
     unsigned int wSecs = howLong / 1000000;
     
     if (howLong == 0)
         return;
     else {
         sleeper.tv_sec = wSecs;
         sleeper.tv_nsec = (long)(uSecs * 1000L);
         nanosleep(&sleeper, NULL);
     }
 }

 /* Clean up resources */
void cleanupResources(void)
{
    /* Free allocated memory */
    if (theSeq != NULL) {
        free(theSeq);
        theSeq = NULL;
    }
    
    if (seq1 != NULL) {
        free(seq1);
        seq1 = NULL;
    }
    
    if (seq2 != NULL) {
        free(seq2);
        seq2 = NULL;
    }
    
    if (cpy1 != NULL) {
        free(cpy1);
        cpy1 = NULL;
    }
    
    if (cpy2 != NULL) {
        free(cpy2);
        cpy2 = NULL;
    }
    
    /* Unmap GPIO memory */
    if (gpio != MAP_FAILED && gpio != NULL) {
        munmap((void*)gpio, BLOCK_SIZE);
    }
}

/* ======================================================= */
/* SECTION: LCD functions                                  */
/* ------------------------------------------------------- */
/* medium-level interface functions (all in C) */

/* from wiringPi:
 * strobe:
 *	Toggle the strobe (Really the "E") pin to the device.
 *	According to the docs, data is latched on the falling edge.
 *********************************************************************************
 */

 void strobe(const struct lcdDataStruct *lcd)
 {
     digitalWrite(gpio, lcd->strbPin, 1);
     delayMicroseconds(50);
     digitalWrite(gpio, lcd->strbPin, 0);
     delayMicroseconds(50);
 }

/*
 * sentDataCmd:
 *	Send an data or command byte to the display.
 *********************************************************************************
 */

 void sendDataCmd(const struct lcdDataStruct *lcd, unsigned char data)
 {
     register unsigned char myData = data;
     unsigned char i, d4;
     
     if (lcd->bits == 4) {
         d4 = (myData >> 4) & 0x0F;
         for (i = 0; i < 4; ++i) {
             digitalWrite(gpio, lcd->dataPins[i], (d4 & 1));
             d4 >>= 1;
         }
         strobe(lcd);
         
         d4 = myData & 0x0F;
         for (i = 0; i < 4; ++i) {
             digitalWrite(gpio, lcd->dataPins[i], (d4 & 1));
             d4 >>= 1;
         }
     } else {
         for (i = 0; i < 8; ++i) {
             digitalWrite(gpio, lcd->dataPins[i], (myData & 1));
             myData >>= 1;
         }
     }
     strobe(lcd);
 }

/*
 * lcdPutCommand:
 *	Send a command byte to the display
 *********************************************************************************
 */

 void lcdPutCommand(const struct lcdDataStruct *lcd, unsigned char command)
 {
 #ifdef DEBUG
     fprintf(stderr, "lcdPutCommand: digitalWrite(%d,%d) and sendDataCmd(%d,%d)\n", lcd->rsPin, 0, lcd, command);
 #endif
     digitalWrite(gpio, lcd->rsPin, 0);
     sendDataCmd(lcd, command);
     delay(2);
 }

 void lcdPut4Command(const struct lcdDataStruct *lcd, unsigned char command)
 {
     register unsigned char myCommand = command;
     register unsigned char i;
     
     digitalWrite(gpio, lcd->rsPin, 0);
     
     for (i = 0; i < 4; ++i) {
         digitalWrite(gpio, lcd->dataPins[i], (myCommand & 1));
         myCommand >>= 1;
     }
     strobe(lcd);
 }

/*
 * lcdHome: lcdClear:
 *	Home the cursor or clear the screen.
 *********************************************************************************
 */

 void lcdHome(struct lcdDataStruct *lcd)
 {
 #ifdef DEBUG
     fprintf(stderr, "lcdHome: lcdPutCommand(%d,%d)\n", lcd, LCD_HOME);
 #endif
     lcdPutCommand(lcd, LCD_HOME);
     lcd->cx = lcd->cy = 0;
     delay(5);
 }
 
 void lcdClear(struct lcdDataStruct *lcd)
 {
 #ifdef DEBUG
     fprintf(stderr, "lcdClear: lcdPutCommand(%d,%d) and lcdPutCommand(%d,%d)\n", lcd, LCD_CLEAR, lcd, LCD_HOME);
 #endif
     lcdPutCommand(lcd, LCD_CLEAR);
     lcdPutCommand(lcd, LCD_HOME);
     lcd->cx = lcd->cy = 0;
     delay(5);
 }

/*
 * lcdPosition:
 *	Update the position of the cursor on the display.
 *	Ignore invalid locations.
 *********************************************************************************
 */

 void lcdPosition(struct lcdDataStruct *lcd, int x, int y)
 {
     if ((x > lcd->cols) || (x < 0))
         return;
     if ((y > lcd->rows) || (y < 0))
         return;
     
     lcdPutCommand(lcd, x + (LCD_DGRAM | (y > 0 ? 0x40 : 0x00)));
     
     lcd->cx = x;
     lcd->cy = y;
 }


/*
 * lcdDisplay: lcdCursor: lcdCursorBlink:
 *	Turn the display, cursor, cursor blinking on/off
 *********************************************************************************
 */

 void lcdDisplay(struct lcdDataStruct *lcd, int state)
 {
     if (state)
         lcdControl |= LCD_DISPLAY_CTRL;
     else
         lcdControl &= ~LCD_DISPLAY_CTRL;
     
     lcdPutCommand(lcd, LCD_CTRL | lcdControl);
 }
 
 void lcdCursor(struct lcdDataStruct *lcd, int state)
 {
     if (state)
         lcdControl |= LCD_CURSOR_CTRL;
     else
         lcdControl &= ~LCD_CURSOR_CTRL;
     
     lcdPutCommand(lcd, LCD_CTRL | lcdControl);
 }
 
 void lcdCursorBlink(struct lcdDataStruct *lcd, int state)
 {
     if (state)
         lcdControl |= LCD_BLINK_CTRL;
     else
         lcdControl &= ~LCD_BLINK_CTRL;
     
     lcdPutCommand(lcd, LCD_CTRL | lcdControl);
 }

/*
 * lcdPutchar:
 *	Send a data byte to be displayed on the display. We implement a very
 *	simple terminal here - with line wrapping, but no scrolling. Yet.
 *********************************************************************************
 */

 void lcdPutchar(struct lcdDataStruct *lcd, unsigned char data)
 {
     digitalWrite(gpio, lcd->rsPin, 1);
     sendDataCmd(lcd, data);
     
     if (++lcd->cx == lcd->cols) {
         lcd->cx = 0;
         if (++lcd->cy == lcd->rows)
             lcd->cy = 0;
         
         lcdPutCommand(lcd, lcd->cx + (LCD_DGRAM | (lcd->cy > 0 ? 0x40 : 0x00)));
     }
 }


/*
 * lcdPuts:
 *	Send a string to be displayed on the display
 *********************************************************************************
 */

 void lcdPuts(struct lcdDataStruct *lcd, const char *string)
 {
     while (*string)
         lcdPutchar(lcd, *string++);
 }


/* ======================================================= */
/* SECTION: aux functions for game logic                   */
/* ------------------------------------------------------- */

/* ********************************************************** */
/* COMPLETE the code for all of the functions in this SECTION */
/* Implement these as C functions in this file                */
/* ********************************************************** */

/* --------------------------------------------------------------------------- */
/* interface on top of the low-level pin I/O code */

/* blink the led on pin @led@, @c@ times */
void blinkN(uint32_t *gpio, int led, int c)
{
    int i;
    
    for (i = 0; i < c; i++) {
        writeLED(gpio, led, HIGH);
        delay(DELAY);
        writeLED(gpio, led, LOW);
        delay(DELAY);
    }
}

/* Blink red LED once to acknowledge input */
void acknowledgeInput(uint32_t *gpio, int redLED) {
    writeLED(gpio, redLED, HIGH);
    delay(DELAY);
    writeLED(gpio, redLED, LOW);
}

/* Blink green LED n times to echo input value */
void echoInput(uint32_t *gpio, int greenLED, int count) {
    for (int i = 0; i < count; i++) {
        writeLED(gpio, greenLED, HIGH);
        delay(DELAY);
        writeLED(gpio, greenLED, LOW);
        delay(DELAY/2); // Shorter delay between blinks
    }
}

/* Blink red LED twice to indicate end of input */
void signalEndOfInput(uint32_t *gpio, int redLED) {
    for (int i = 0; i < 2; i++) {
        writeLED(gpio, redLED, HIGH);
        delay(DELAY);
        writeLED(gpio, redLED, LOW);
        delay(DELAY);
    }
}

/* Display match results with LED pattern */
void displayMatchResults(uint32_t *gpio, int greenLED, int redLED, int exact, int approx) {
    // Blink green LED for exact matches
    for (int i = 0; i < exact; i++) {
        writeLED(gpio, greenLED, HIGH);
        delay(DELAY);
        writeLED(gpio, greenLED, LOW);
        delay(DELAY/2);
    }
    
    // Red LED separator
    delay(DELAY);
    writeLED(gpio, redLED, HIGH);
    delay(DELAY);
    writeLED(gpio, redLED, LOW);
    delay(DELAY);
    
    // Blink green LED for approximate matches
    for (int i = 0; i < approx; i++) {
        writeLED(gpio, greenLED, HIGH);
        delay(DELAY);
        writeLED(gpio, greenLED, LOW);
        delay(DELAY/2);
    }
}

/* Blink red LED three times to indicate new round */
void signalNewRound(uint32_t *gpio, int redLED) {
    for (int i = 0; i < 3; i++) {
        writeLED(gpio, redLED, HIGH);
        delay(DELAY);
        writeLED(gpio, redLED, LOW);
        delay(DELAY);
    }
}

/* Success pattern: green LED blinks three times while red LED is on */
void displaySuccess(uint32_t *gpio, int greenLED, int redLED) {
    writeLED(gpio, redLED, HIGH);
    for (int i = 0; i < 3; i++) {
        writeLED(gpio, greenLED, HIGH);
        delay(DELAY);
        writeLED(gpio, greenLED, LOW);
        delay(DELAY);
    }
    writeLED(gpio, redLED, LOW);
}

/* Surname-based greeting function */
void displaySurnameGreeting(uint32_t *gpio, int redLED, int greenLED, const char *surname, struct lcdDataStruct *lcd) {
    int len = strlen(surname);
    
    // Display surname on LCD
    lcdClear(lcd);
    lcdPuts(lcd, "Hello");
    lcdPosition(lcd, 0, 1);
    lcdPuts(lcd, surname);
    delay(2000);
    
    // Turn off both LEDs initially
    writeLED(gpio, redLED, LOW);
    writeLED(gpio, greenLED, LOW);
    delay(DELAY);
    
    // Blink LEDs based on surname
    for (int i = 0; i < len; i++) {
        char c = tolower(surname[i]);
        // Check if vowel (a, e, i, o, u)
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') {
            // Blink green LED for vowel
            writeLED(gpio, greenLED, HIGH);
            delay(DELAY);
            writeLED(gpio, greenLED, LOW);
        } else if (c >= 'a' && c <= 'z') {
            // Blink red LED for consonant (only for letters)
            writeLED(gpio, redLED, HIGH);
            delay(DELAY);
            writeLED(gpio, redLED, LOW);
        }
        delay(DELAY/2);
    }
    
    // Final confirmation pattern
    for (int i = 0; i < 2; i++) {
        writeLED(gpio, greenLED, HIGH);
        writeLED(gpio, redLED, HIGH);
        delay(DELAY);
        writeLED(gpio, greenLED, LOW);
        writeLED(gpio, redLED, LOW);
        delay(DELAY);
    }
    
    // Clear LCD for next message
    delay(1000);
}


/* ======================================================= */
/* SECTION: main fct                                       */
/* ------------------------------------------------------- */

int main(int argc, char *argv[])
{
    struct lcdDataStruct *lcd;
    int bits, rows, cols;
    unsigned char func;
    
    int found = 0, attempts = 0, i, j, code;
    int c, d, buttonPressed, rel, foo;
    int *attSeq;
    
    int pinLED = LED, pin2LED2 = LED2, pinButton = BUTTON;
    int fSel, shift, pin, clrOff, setOff, off, res;
    int fd;
    
    int exact, contained;
    char str1[32];
    char str2[32];
    
    struct timeval t1, t2;
    int t;
    
    char buf[32];
    
    // variables for command-line processing
    char str_in[20], str[20] = "some text";
    int verbose = 0, debug = 0, help = 0, opt_m = 0, opt_n = 0, opt_s = 0, unit_test = 0, res_matches = 0;
    
    // Register cleanup function to be called on exit
    atexit(cleanupResources);
  
  // -------------------------------------------------------
  // process command-line arguments

  // see: man 3 getopt for docu and an example of command line parsing
  { // see the CW spec for the intended meaning of these options
      int opt;
      while ((opt = getopt(argc, argv, "hvdus:")) != -1) {
          switch (opt) {
              case 'v':
                  verbose = 1;
                  break;
              case 'h':
                  help = 1;
                  break;
              case 'd':
                  debug = 1;
                  break;
              case 'u':
                  unit_test = 1;
                  break;
              case 's':
                  opt_s = atoi(optarg);
                  break;
              default: /* '?' */
                  fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-u <seq1> <seq2>] [-s <secret seq>]  \n", argv[0]);
                  exit(EXIT_FAILURE);
          }
      }
  }

  if (help) {
    fprintf(stderr, "MasterMind program, running on a Raspberry Pi, with connected LED, button and LCD display\n");
    fprintf(stderr, "Use the button for input of numbers. The LCD display will show the matches with the secret sequence.\n");
    fprintf(stderr, "For full specification of the program see: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf\n");
    fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-u <seq1> <seq2>] [-s <secret seq>]  \n", argv[0]);
    exit(EXIT_SUCCESS);
}

if (unit_test && optind >= argc - 1) {
    fprintf(stderr, "Expected 2 arguments after option -u\n");
    exit(EXIT_FAILURE);
}

if (verbose && unit_test) {
    printf("1st argument = %s\n", argv[optind]);
    printf("2nd argument = %s\n", argv[optind + 1]);
}

if (verbose) {
    fprintf(stdout, "Settings for running the program\n");
    fprintf(stdout, "Verbose is %s\n", (verbose ? "ON" : "OFF"));
    fprintf(stdout, "Debug is %s\n", (debug ? "ON" : "OFF"));
    fprintf(stdout, "Unittest is %s\n", (unit_test ? "ON" : "OFF"));
    if (opt_s)  fprintf(stdout, "Secret sequence set to %d\n", opt_s);
}

// Allocate memory for sequences
seq1 = (int *)malloc(seqlen * sizeof(int));
seq2 = (int *)malloc(seqlen * sizeof(int));
cpy1 = (int *)malloc(seqlen * sizeof(int));
cpy2 = (int *)malloc(seqlen * sizeof(int));

if (seq1 == NULL || seq2 == NULL || cpy1 == NULL || cpy2 == NULL) {
  fprintf(stderr, "Memory allocation failed\n");
  exit(EXIT_FAILURE);
}

  // check for -u option, and if so run a unit test on the matching function
  if (unit_test && argc > optind+1) { // more arguments to process; only needed with -u 
    strcpy(str_in, argv[optind]);
    opt_m = atoi(str_in);
    strcpy(str_in, argv[optind+1]);
    opt_n = atoi(str_in);
    // CALL a test-matches function; see testm.c for an example implementation
    readSeq(seq1, opt_m); // turn the integer number into a sequence of numbers
    readSeq(seq2, opt_n); // turn the integer number into a sequence of numbers
    if (verbose)
      fprintf(stdout, "Testing matches function with sequences %d and %d\n", opt_m, opt_n);
    res_matches = countMatches(seq1, seq2);
    showMatches(res_matches, seq1, seq2, 1);
    exit(EXIT_SUCCESS);
  } 

  if (opt_s) { // if -s option is given, use the sequence as secret sequence
    if (theSeq==NULL)
      theSeq = (int*)malloc(seqlen*sizeof(int));
    readSeq(theSeq, opt_s);
    if (verbose) {
      fprintf(stderr, "Running program with secret sequence:\n");
      showSeq(theSeq);
    }
  }
  
  // -------------------------------------------------------
  // LCD constants, hard-coded: 16x2 display, using a 4-bit connection
  bits = 4; 
  cols = 16; 
  rows = 2; 
  // -------------------------------------------------------

  printf("Raspberry Pi LCD driver, for a %dx%d display (%d-bit wiring) \n", cols, rows, bits);
    
    if (geteuid() != 0)
        fprintf(stderr, "setup: Must be root. (Did you forget sudo?)\n");
    
    // init of guess sequence, and copies (for use in countMatches)
    attSeq = (int *)malloc(seqlen * sizeof(int));
    if (attSeq == NULL) {
        fprintf(stderr, "Memory allocation failed for attSeq\n");
        exit(EXIT_FAILURE);
    }

  // -----------------------------------------------------------------------------
  // constants for RPi2
  gpiobase = 0x3F200000 ;

  // -----------------------------------------------------------------------------
  // memory mapping 
  // Open the master /dev/memory device

  if ((fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
    return failure (FALSE, "setup: Unable to open /dev/mem: %s\n", strerror (errno)) ;

  // GPIO:
  gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpiobase) ;
  if ((int32_t)gpio == -1)
    return failure (FALSE, "setup: mmap (GPIO) failed: %s\n", strerror (errno)) ;

  // -------------------------------------------------------
  // Configuration of LED and BUTTON

  /* ***  COMPLETE the code here  ***  */
  pinMode(gpio, pinLED, OUTPUT);
  pinMode(gpio, pin2LED2, OUTPUT);
  pinMode(gpio, pinButton, INPUT);
  pinMode(gpio, STRB_PIN, OUTPUT);
  pinMode(gpio, RS_PIN, OUTPUT);
  pinMode(gpio, DATA0_PIN, OUTPUT);
  pinMode(gpio, DATA1_PIN, OUTPUT);
  pinMode(gpio, DATA2_PIN, OUTPUT);
  pinMode(gpio, DATA3_PIN, OUTPUT);
  
  // -------------------------------------------------------
  // INLINED version of lcdInit (can only deal with one LCD attached to the RPi):
  // you can use this code as-is, but you need to implement digitalWrite() and
  // pinMode() which are called from this code
  // Create a new LCD:
  lcd = (struct lcdDataStruct *)malloc (sizeof (struct lcdDataStruct)) ;
  if (lcd == NULL)
    return -1 ;

  // hard-wired GPIO pins
  lcd->rsPin   = RS_PIN ;
  lcd->strbPin = STRB_PIN ;
  lcd->bits    = 4 ;
  lcd->rows    = rows ;  // # of rows on the display
  lcd->cols    = cols ;  // # of cols on the display
  lcd->cx      = 0 ;     // x-pos of cursor
  lcd->cy      = 0 ;     // y-pos of curosr

  lcd->dataPins [0] = DATA0_PIN ;
  lcd->dataPins [1] = DATA1_PIN ;
  lcd->dataPins [2] = DATA2_PIN ;
  lcd->dataPins [3] = DATA3_PIN ;
  // lcd->dataPins [4] = d4 ;
  // lcd->dataPins [5] = d5 ;
  // lcd->dataPins [6] = d6 ;
  // lcd->dataPins [7] = d7 ;

  // lcds [lcdFd] = lcd ;

  digitalWrite (gpio, lcd->rsPin,   0) ; 
  pinMode (gpio, lcd->rsPin,   OUTPUT) ;
  digitalWrite (gpio, lcd->strbPin, 0) ; 
  pinMode (gpio, lcd->strbPin, OUTPUT) ;

  for (i = 0 ; i < bits ; ++i)
  {
    digitalWrite (gpio, lcd->dataPins [i], 0) ;
    pinMode      (gpio, lcd->dataPins [i], OUTPUT) ;
  }
  delay (35) ; // mS

// Gordon Henderson's explanation of this part of the init code (from wiringPi):
// 4-bit mode?
//	OK. This is a PIG and it's not at all obvious from the documentation I had,
//	so I guess some others have worked through either with better documentation
//	or more trial and error... Anyway here goes:
//
//	It seems that the controller needs to see the FUNC command at least 3 times
//	consecutively - in 8-bit mode. If you're only using 8-bit mode, then it appears
//	that you can get away with one func-set, however I'd not rely on it...
//
//	So to set 4-bit mode, you need to send the commands one nibble at a time,
//	the same three times, but send the command to set it into 8-bit mode those
//	three times, then send a final 4th command to set it into 4-bit mode, and only
//	then can you flip the switch for the rest of the library to work in 4-bit
//	mode which sends the commands as 2 x 4-bit values.

  if (bits == 4)
  {
    func = LCD_FUNC | LCD_FUNC_DL ;			// Set 8-bit mode 3 times
    lcdPut4Command (lcd, func >> 4) ; 
    delay (35) ;
    lcdPut4Command (lcd, func >> 4) ; 
    delay (35) ;
    lcdPut4Command (lcd, func >> 4) ; 
    delay (35) ;
    func = LCD_FUNC ;					// 4th set: 4-bit mode
    lcdPut4Command (lcd, func >> 4) ; 
    delay (35) ;
    lcd->bits = 4 ;
  }
  else
  {
    failure(TRUE, "setup: only 4-bit connection supported\n");
    func = LCD_FUNC | LCD_FUNC_DL ;
    lcdPutCommand  (lcd, func     ) ; delay (35) ;
    lcdPutCommand  (lcd, func     ) ; delay (35) ;
    lcdPutCommand  (lcd, func     ) ; delay (35) ;
  }

  if (lcd->rows > 1)
  {
    func |= LCD_FUNC_N ;
    lcdPutCommand (lcd, func) ; delay (35) ;
  }

  // Rest of the initialisation sequence
  lcdDisplay     (lcd, TRUE) ;
  lcdCursor      (lcd, FALSE) ;
  lcdCursorBlink (lcd, FALSE) ;
  lcdClear       (lcd) ;

  lcdPutCommand (lcd, LCD_ENTRY   | LCD_ENTRY_ID) ;    // set entry mode to increment address counter after write
  lcdPutCommand (lcd, LCD_CDSHIFT | LCD_CDSHIFT_RL) ;  // set display shift to right-to-left

  // END lcdInit ------
  // -----------------------------------------------------------------------------
  // Start of game
  fprintf(stderr,"Printing welcome message on the LCD display ...\n");
  /* ***  COMPLETE the code here  ***  */
  fprintf(stderr, "Printing welcome message on the LCD display ...\n");
    
  // Welcome message
  lcdClear(lcd);
  lcdPuts(lcd, "Welcome to");
  lcdPosition(lcd, 1, 1);
  lcdPuts(lcd, "MasterMind");
  delay(2000);
  lcdClear(lcd);
  
  displaySurnameGreeting(gpio, pin2LED2, pinLED, "Dsouza & Ahmed", lcd);

  /* initialise the secret sequence */
  if (!opt_s)
    initSeq();
  if (debug)
    showSeq(theSeq);

  // Wait for user to start
  lcdPuts(lcd, "Press enter");
  lcdPosition(lcd, 0, 1);
  lcdPuts(lcd, "to start");
  waitForEnter();

  // -----------------------------------------------------------------------------
  // +++++ main loop

// Turn LEDs off at start
  writeLED(gpio, pinLED, LOW);
  writeLED(gpio, pin2LED2, LOW);
// Main game loop - player has MAX_ATTEMPTS attempts to guess the sequence
while (!found && attempts < MAX_ATTEMPTS) {

    /* ******************************************************* */
    /* ***  COMPLETE the code here  ***                        */
    /* this needs to implement the main loop of the game:      */
    /* check for button presses and count them                 */
    /* store the input numbers in the sequence @attSeq@        */
    /* compute the match with the secret sequence, and         */
    /* show the result                                         */
    /* see CW spec for details                                 */
    /* ******************************************************* */
    /* ***  COMPLETE the code here  ***  */

  // Clear LCD for new attempt
        lcdClear(lcd);
        
        // Print attempt number
        printf("Attempt: %d\n", attempts + 1);
        
        // Show attempt number on LCD
        lcdPuts(lcd, "Starting");
        lcdPosition(lcd, 0, 1);
        sprintf(buf, "Attempt: %d", attempts + 1);
        lcdPuts(lcd, buf);
        delay(2000);
        
    // Get input for each position in the sequence
for (i = 0; i < seqlen; i++) {
    lcdClear(lcd);
    lcdPuts(lcd, "Position ");
    sprintf(buf, "%d", i + 1);
    lcdPuts(lcd, buf);
    lcdPosition(lcd, 0, 1);
    lcdPuts(lcd, "Press button");
    
    // Use the improved function to get input
    int selectedValue = getButtonInput(gpio, pinButton, colors, INPUT_TIMEOUT, 2);
    
    // Acknowledge input with red LED
    acknowledgeInput(gpio, pin2LED2);
    
    // Echo input with green LED
    echoInput(gpio, pinLED, selectedValue);
    
    // Store the value
    attSeq[i] = selectedValue;
    
    // Show the selected value on LCD
    lcdClear(lcd);
    lcdPuts(lcd, "Position ");
    sprintf(buf, "%d: %d", i + 1, attSeq[i]);
    lcdPuts(lcd, buf);
    delay(1000);
}

// Signal end of input sequence
signalEndOfInput(gpio, pin2LED2);
        
        // Display the entered sequence
        if (debug) {
            printf("Attempt %d: ", attempts + 1);
            showSeq(attSeq);
        }
        
        // Calculate matches
code = countMatches(theSeq, attSeq);
exact = code / 10;
contained = code % 10;

// Display result on LCD
lcdClear(lcd);
lcdPosition(lcd, 0, 0);
sprintf(buf, "Exact: %d", exact);
lcdPuts(lcd, buf);
lcdPosition(lcd, 0, 1);
sprintf(buf, "Approx: %d", contained);
lcdPuts(lcd, buf);

// Display match results with LED pattern
displayMatchResults(gpio, pinLED, pin2LED2, exact, contained);

// Check if the sequence is found
if (exact == seqlen) {
    found = 1;
    
    // Display success pattern
    displaySuccess(gpio, pinLED, pin2LED2);
} else {
    // Wait for button press to continue
    delay(2000);
    lcdPosition(lcd, 10, 1);
    lcdPuts(lcd, "Next?");
    waitForButton(gpio, pinButton);
    attempts++;
    
    // Signal start of new round
    signalNewRound(gpio, pin2LED2);
    }
    }
    
// Game over - display result
if (found) {
    lcdClear(lcd);
    lcdPosition(lcd, 0, 0);
    lcdPuts(lcd, "SUCCESS!");
    lcdPosition(lcd, 0, 1);
    sprintf(buf, "Solved in %d try", attempts + 1);
    lcdPuts(lcd, buf);
    
    // Display success pattern again
    displaySuccess(gpio, pinLED, pin2LED2);
} else {
    lcdClear(lcd);
    lcdPosition(lcd, 0, 0);
    lcdPuts(lcd, "Game Over!");
    lcdPosition(lcd, 0, 1);
    lcdPuts(lcd, "Secret was:");
    
    // Show the secret sequence
    if (debug) {
        printf("Secret: ");
        showSeq(theSeq);
    }
    
    // Failure pattern
    for (i = 0; i < 3; i++) {
        writeLED(gpio, pin2LED2, HIGH);
        delay(DELAY*2);
        writeLED(gpio, pin2LED2, LOW);
        delay(DELAY);
    }
}
    
    // Clean up and exit
    free(lcd);
    close(fd);
    
    return 0;
}