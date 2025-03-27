/*
 * MasterMind implementation for Raspberry Pi
 * Combines the best parts of both implementations
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

/* --------------------------------------------------------------------------- */
/* Config settings */
#define DEBUG
#undef ASM_CODE

// =======================================================
// Tunables
// PINs (based on BCM numbering)
#define LED 13      // GPIO pin for green LED
#define LED2 5      // GPIO pin for red LED
#define BUTTON 19   // GPIO pin for button

// =======================================================
// delay for loop iterations (mainly), in ms
#define DELAY 200   // in mili-seconds: 0.2s
#define TIMEOUT 3000000  // in micro-seconds: 3s

// =======================================================
// APP constants
#define COLS 3      // number of colors
#define SEQL 3      // length of the sequence
#define MAX_ATTEMPTS 5  // maximum number of attempts

// =======================================================
// Generic constants
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

// =======================================================
// Wiring for LCD
#define STRB_PIN 24
#define RS_PIN 25
#define DATA0_PIN 23
#define DATA1_PIN 10
#define DATA2_PIN 27
#define DATA3_PIN 22

/* ======================================================= */
/* SECTION: constants and prototypes                       */
/* ------------------------------------------------------- */

// Custom character data for LCD
static unsigned char newChar[8] = {
    0b11111, 0b10001, 0b10001, 0b10101,
    0b11111, 0b10001, 0b10001, 0b11111,
};

/* Constants */
static const int colors = COLS;
static const int seqlen = SEQL;
static char *color_names[] = {"red", "green", "blue"};
static int* theSeq = NULL;
static int *seq1, *seq2, *cpy1, *cpy2;

/* --------------------------------------------------------------------------- */
// LCD data structure
struct lcdDataStruct {
    int bits, rows, cols;
    int rsPin, strbPin;
    int dataPins[8];
    int cx, cy;
};

static int lcdControl;

/* LCD Commands */
#define LCD_CLEAR 0x01
#define LCD_HOME 0x02
#define LCD_ENTRY 0x04
#define LCD_CTRL 0x08
#define LCD_CDSHIFT 0x10
#define LCD_FUNC 0x20
#define LCD_CGRAM 0x40
#define LCD_DGRAM 0x80

// Bits in the entry register
#define LCD_ENTRY_SH 0x01
#define LCD_ENTRY_ID 0x02

// Bits in the control register
#define LCD_BLINK_CTRL 0x01
#define LCD_CURSOR_CTRL 0x02
#define LCD_DISPLAY_CTRL 0x04

// Bits in the function register
#define LCD_FUNC_F 0x04
#define LCD_FUNC_N 0x08
#define LCD_FUNC_DL 0x10

#define LCD_CDSHIFT_RL 0x04

// Mask for the bottom 64 pins which belong to the Raspberry Pi
#define PI_GPIO_MASK (0xFFFFFFC0)

static unsigned int gpiobase;
static uint32_t *gpio;
static int timed_out = 0;

/* ------------------------------------------------------- */
// Prototypes
int failure(int fatal, const char *message, ...);
void waitForEnter(void);
void waitForButton(uint32_t *gpio, int button);
void cleanupResources(void);

/* ======================================================= */
/* SECTION: hardware interface (LED, button, LCD display)  */
/* ------------------------------------------------------- */

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

/* Set a GPIO pin mode to INPUT or OUTPUT */
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

/* Control an LED */
void writeLED(uint32_t *gpio, int led, int value)
{
    /* Set the pin as OUTPUT */
    pinMode(gpio, led, OUTPUT);
    
    /* Set the pin value */
    digitalWrite(gpio, led, value);
}

/* Read the state of a button */
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
    
    while (1) {
        currState = readButton(gpio, button);
        
        /* If button state changed from not pressed to pressed */
        if (currState == HIGH && prevState == LOW) {
            delay(debounceTime); /* Debounce delay */
            
            /* Check if button is still pressed */
            currState = readButton(gpio, button);
            if (currState == HIGH) {
                /* Wait for button release */
                while (readButton(gpio, button) == HIGH) {
                    delay(10);
                }
                break;
            }
        }
        
        prevState = currState;
        delay(10); /* Small polling delay */
    }
}

/* ======================================================= */
/* SECTION: game logic                                     */
/* ------------------------------------------------------- */

/* Initialize the secret sequence with random values */
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

/* Display the sequence on the terminal */
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

/* Count exact and approximate matches between two sequences */
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

/* Show the results from calling countMatches */
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

/* Parse an integer value as a list of digits */
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

/* Read a guess sequence from stdin */
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

/* timestamps needed to implement a time-out mechanism */
static uint64_t startT, stopT;

/* Get current time in microseconds */
uint64_t timeInMicroseconds()
{
    struct timeval tv;
    uint64_t micros;
    
    gettimeofday(&tv, NULL);
    micros = (uint64_t)(tv.tv_sec) * 1000000 + (uint64_t)(tv.tv_usec);
    
    return micros;
}

/* Timer handler callback */
void timer_handler(int signum)
{
    stopT = timeInMicroseconds();
    
    /* Check if timeout has occurred */
    if (stopT - startT >= TIMEOUT) {
        timed_out = 1;
        printf("Time out!\n");
    }
}

/* Initialize interval timer */
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

int failure(int fatal, const char *message, ...)
{
    va_list argp;
    char buffer[1024];
    
    if (!fatal)
        return -1;
    
    va_start(argp, message);
    vsnprintf(buffer, 1023, message, argp);
    va_end(argp);
    
    fprintf(stderr, "%s", buffer);
    exit(EXIT_FAILURE);
    
    return 0;
}

void waitForEnter(void)
{
    printf("Press ENTER to continue: ");
    (void)fgetc(stdin);
}

void delay(unsigned int howLong)
{
    struct timespec sleeper, dummy;
    
    sleeper.tv_sec = (time_t)(howLong / 1000);
    sleeper.tv_nsec = (long)(howLong % 1000) * 1000000;
    
    nanosleep(&sleeper, &dummy);
}

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

/* Blink the LED n times */
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

/* ======================================================= */
/* SECTION: LCD functions                                  */
/* ------------------------------------------------------- */

void strobe(const struct lcdDataStruct *lcd)
{
    digitalWrite(gpio, lcd->strbPin, 1);
    delayMicroseconds(50);
    digitalWrite(gpio, lcd->strbPin, 0);
    delayMicroseconds(50);
}

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

void lcdPuts(struct lcdDataStruct *lcd, const char *string)
{
    while (*string)
        lcdPutchar(lcd, *string++);
}

/* ======================================================= */
/* SECTION: main function                                  */
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
    {
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
    if (unit_test && argc > optind + 1) {
        strcpy(str_in, argv[optind]);
        opt_m = atoi(str_in);
        strcpy(str_in, argv[optind + 1]);
        opt_n = atoi(str_in);
        
        readSeq(seq1, opt_m);
        readSeq(seq2, opt_n);
        
        if (verbose)
            fprintf(stdout, "Testing matches function with sequences %d and %d\n", opt_m, opt_n);
        
        res_matches = countMatches(seq1, seq2);
        showMatches(res_matches, seq1, seq2, 1);
        exit(EXIT_SUCCESS);
    }
    
    if (opt_s) { // if -s option is given, use the sequence as secret sequence
        if (theSeq == NULL)
            theSeq = (int*)malloc(seqlen * sizeof(int));
        
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
    // constants for RPi3
    gpiobase = 0x3F200000;
    
    // -----------------------------------------------------------------------------
    // memory mapping
    // Open the master /dev/memory device
    if ((fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC)) < 0)
        return failure(FALSE, "setup: Unable to open /dev/mem: %s\n", strerror(errno));
    
    // GPIO:
    gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpiobase);
    if ((int32_t)gpio == -1)
        return failure(FALSE, "setup: mmap (GPIO) failed: %s\n", strerror(errno));
    
    // -------------------------------------------------------
    // Configuration of LED, BUTTON and LCD pins
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
    // INLINED version of lcdInit
    lcd = (struct lcdDataStruct *)malloc(sizeof(struct lcdDataStruct));
    if (lcd == NULL)
        return -1;
    
    // hard-wired GPIO pins
    lcd->rsPin = RS_PIN;
    lcd->strbPin = STRB_PIN;
    lcd->bits = 4;
    lcd->rows = rows;
    lcd->cols = cols;
    lcd->cx = 0;
    lcd->cy = 0;
    
    lcd->dataPins[0] = DATA0_PIN;
    lcd->dataPins[1] = DATA1_PIN;
    lcd->dataPins[2] = DATA2_PIN;
    lcd->dataPins[3] = DATA3_PIN;
    
    digitalWrite(gpio, lcd->rsPin, 0);
    pinMode(gpio, lcd->rsPin, OUTPUT);
    digitalWrite(gpio, lcd->strbPin, 0);
    pinMode(gpio, lcd->strbPin, OUTPUT);
    
    for (i = 0; i < bits; ++i) {
        digitalWrite(gpio, lcd->dataPins[i], 0);
        pinMode(gpio, lcd->dataPins[i], OUTPUT);
    }
    delay(35); // mS
    
    if (bits == 4) {
        func = LCD_FUNC | LCD_FUNC_DL; // Set 8-bit mode 3 times
        lcdPut4Command(lcd, func >> 4);
        delay(35);
        lcdPut4Command(lcd, func >> 4);
        delay(35);
        lcdPut4Command(lcd, func >> 4);
        delay(35);
        func = LCD_FUNC; // 4th set: 4-bit mode
        lcdPut4Command(lcd, func >> 4);
        delay(35);
        lcd->bits = 4;
    } else {
        failure(TRUE, "setup: only 4-bit connection supported\n");
        func = LCD_FUNC | LCD_FUNC_DL;
        lcdPutCommand(lcd, func);
        delay(35);
        lcdPutCommand(lcd, func);
        delay(35);
        lcdPutCommand(lcd, func);
        delay(35);
    }
    
    if (lcd->rows > 1) {
        func |= LCD_FUNC_N;
        lcdPutCommand(lcd, func);
        delay(35);
    }
    
    // Rest of the initialisation sequence
    lcdDisplay(lcd, TRUE);
    lcdCursor(lcd, FALSE);
    lcdCursorBlink(lcd, FALSE);
    lcdClear(lcd);
    
    lcdPutCommand(lcd, LCD_ENTRY | LCD_ENTRY_ID);
    lcdPutCommand(lcd, LCD_CDSHIFT | LCD_CDSHIFT_RL);
    
    // END lcdInit ------
    // -----------------------------------------------------------------------------
    // Start of game
    fprintf(stderr, "Printing welcome message on the LCD display ...\n");
    
    // Welcome message
    lcdClear(lcd);
    lcdPuts(lcd, "Welcome to");
    lcdPosition(lcd, 1, 1);
    lcdPuts(lcd, "MasterMind");
    delay(2000);
    lcdClear(lcd);
    
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
    // Main game loop
    // Turn LEDs off at start
    writeLED(gpio, pinLED, LOW);
    writeLED(gpio, pin2LED2, LOW);
    
    // Main game loop - player has MAX_ATTEMPTS attempts to guess the sequence
    while (!found && attempts < MAX_ATTEMPTS) {
        int turn = 0;
        
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
            
            // Wait for button presses to select a color
            int buttonCount = 0;
            int confirmed = 0;
            time_t startTime = time(NULL);
            time_t endTime = startTime + 5; // 5 second window
            
            while (time(NULL) < endTime && !confirmed) {
                // Visual indicator - blink green LED
                writeLED(gpio, pinLED, HIGH);
                delay(100);
                writeLED(gpio, pinLED, LOW);
                delay(100);
                
                // Check for button press with improved debouncing
                int buttonState = readButton(gpio, pinButton);
                static int lastButtonState = LOW;
                
                if (buttonState == HIGH && lastButtonState == LOW) {
                    // Button pressed, increment count
                    buttonCount = (buttonCount % colors) + 1;
                    
                    // Show current selection on LCD
                    lcdPosition(lcd, 10, 1);
                    sprintf(buf, "Val: %d", buttonCount);
                    lcdPuts(lcd, buf);
                    
                    // Visual feedback - blink red LED
                    writeLED(gpio, pin2LED2, HIGH);
                    delay(200);
                    writeLED(gpio, pin2LED2, LOW);
                    
                    // Wait for button release with debouncing
                    delay(50);
                    while (readButton(gpio, pinButton) == HIGH) {
                        delay(10);
                    }
                    delay(50);
                    
                    // Reset timer to give more time after a button press
                    endTime = time(NULL) + 5;
                }
                
                lastButtonState = buttonState;
                
                // Check for double-press to confirm (within 1 second)
                if (buttonCount > 0) {
                    for (j = 0; j < 10 && !confirmed; j++) {
                        delay(100);
                        buttonState = readButton(gpio, pinButton);
                        
                        if (buttonState == HIGH && lastButtonState == LOW) {
                            confirmed = 1;
                            
                            // Visual confirmation - double blink green LED
                            blinkN(gpio, pinLED, 2);
                            
                            // Wait for button release
                            while (readButton(gpio, pinButton) == HIGH) {
                                delay(10);
                            }
                            break;
                        }
                        
                        lastButtonState = buttonState;
                    }
                }
            }
            
            // If time expired or not confirmed, use the last value
            if (buttonCount == 0) {
                buttonCount = 1; // Default to 1 if no button press
            }
            
            // Store the value
            attSeq[i] = buttonCount;
            
            // Show the selected value
            lcdClear(lcd);
            lcdPuts(lcd, "Position ");
            sprintf(buf, "%d: %d", i + 1, attSeq[i]);
            lcdPuts(lcd, buf);
            delay(1000);
        }
        
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
        
        // Visual feedback for results
        delay(1000);
        blinkN(gpio, pinLED, exact);    // Blink green LED for exact matches
        delay(500);
        blinkN(gpio, pin2LED2, contained); // Blink red LED for approximate matches
        
        // Check if the sequence is found
        if (exact == seqlen) {
            found = 1;
        } else {
            // Wait for button press to continue
            delay(2000);
            lcdPosition(lcd, 10, 1);
            lcdPuts(lcd, "Next?");
            waitForButton(gpio, pinButton);
            attempts++;
        }
    }
    
    // Game over - display result
    if (found) {
        lcdClear(lcd);
        lcdPosition(lcd, 0, 0);
        lcdPuts(lcd, "Congratulations!");
        lcdPosition(lcd, 0, 1);
        sprintf(buf, "Solved in %d try", attempts + 1);
        lcdPuts(lcd, buf);
        
        // Celebration pattern
        for (i = 0; i < 5; i++) {
            writeLED(gpio, pinLED, HIGH);
            writeLED(gpio, pin2LED2, LOW);
            delay(DELAY);
            writeLED(gpio, pinLED, LOW);
            writeLED(gpio, pin2LED2, HIGH);
            delay(DELAY);
        }
        writeLED(gpio, pinLED, LOW);
        writeLED(gpio, pin2LED2, LOW);
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
