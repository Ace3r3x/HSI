/*
  A C program to test the matching function (for master-mind) as implemented in matches.s

$ as  -o mm-matches.o mm-matches.s
$ gcc -c -o testm.o testm.c
$ gcc -o testm testm.o mm-matches.o
$ ./testm
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

/* Constants for the game */
#define LENGTH 3
#define COLORS 3
#define NAN1 8
#define NAN2 9

const int seqlen = LENGTH;
const int seqmax = COLORS;

/* ********************************** */
/* Functions for the MasterMind game  */
/* ********************************** */

/* 
 * Display the sequence on the terminal window
 * Format: "Secret: 1 2 3"
 */
void showSeq(int *seq)
{
    printf("Secret: ");
    for (int i = 0; i < LENGTH; i++)
    {
        printf("%d ", seq[i]);
    }
    printf("\n");
}

/*
 * Parse an integer value as a list of digits and put them into seq
 * For example: 123 -> [1, 2, 3]
 */
void readSeq(int *seq, int val)
{
    if (seq == NULL)
    {
        seq = (int *)malloc(LENGTH * sizeof(int));
        if (seq == NULL) {
            fprintf(stderr, "Memory allocation failed in readSeq\n");
            exit(EXIT_FAILURE);
        }
    }

    /* Extract digits from the number */
    int divisor = 1;
    for (int i = 1; i < LENGTH; i++) {
        divisor *= 10;
    }
    
    for (int i = 0; i < LENGTH; i++) {
        seq[i] = (val / divisor) % 10;
        divisor /= 10;
        
        /* Ensure values are in range 1-COLORS */
        if (seq[i] < 1 || seq[i] > COLORS) {
            seq[i] = 1;  /* Default to 1 if out of range */
        }
    }
}

/*
 * Count exact matches (right color, right position) and 
 * approximate matches (right color, wrong position)
 * Returns result encoded as: (exact * 10) + approximate
 */
int countMatches(int *seq1, int *seq2)
{
    int exactMatches = 0;
    int approximateMatches = 0;

    /* Arrays to track which positions have been matched */
    int seq1Matched[LENGTH] = {0};
    int seq2Matched[LENGTH] = {0};

    /* First pass: Count exact matches */
    for (int i = 0; i < LENGTH; i++)
    {
        if (seq1[i] == seq2[i])
        {
            exactMatches++;
            seq1Matched[i] = 1;
            seq2Matched[i] = 1;
        }
    }

    /* Second pass: Count approximate matches */
    for (int i = 0; i < LENGTH; i++)
    {
        if (!seq1Matched[i])
        {
            for (int j = 0; j < LENGTH; j++)
            {
                if (!seq2Matched[j] && seq1[i] == seq2[j])
                {
                    approximateMatches++;
                    seq1Matched[i] = 1;
                    seq2Matched[j] = 1;
                    break;
                }
            }
        }
    }

    return exactMatches * 10 + approximateMatches;
}

/*
 * Display the results from calling countMatches
 * Shows exact and approximate matches
 */
void showMatches(int code, int *seq1, int *seq2, int lcd_format)
{
    int approx = code % 10;
    int exact = code / 10;

    if (lcd_format) {
        /* Format for LCD display */
        printf("%d exact\n%d approximate\n", exact, approx);
    } else {
        /* Format for terminal */
        printf("Exact matches: %d\nApproximate matches: %d\n", exact, approx);
    }
}

/*
 * Read a guess sequence from stdin and store the values in arr
 * Only needed for testing the game logic, without button input
 */
int readNum(int max)
{
    printf("Enter %d numbers (1-%d) separated by spaces:\n", LENGTH, max);

    int input;
    for (int i = 0; i < LENGTH; i++)
    {
        if (scanf("%d", &input) != 1) {
            /* Clear input buffer on error */
            while (getchar() != '\n');
            printf("Invalid input. Please enter a number (1-%d): ", max);
            i--; /* Retry this position */
            continue;
        }

        if (input < 1 || input > max) {
            printf("Number must be between 1 and %d. Try again: ", max);
            i--; /* Retry this position */
            continue;
        }

        /* Store valid input (assuming seq1 is a global or passed in) */
        /* Note: In actual implementation, this should be passed as a parameter */
    }

    return 0;
}

/* The ARM assembler version of the matching function */
extern int matches(int *val1, int *val2);

/* ********************************** */
/* Main function                      */
/* ********************************** */

int main(int argc, char **argv)
{
    int res, res_c, t, t_c, m, n;
    int *seq1, *seq2, *cpy1, *cpy2;
    struct timeval t1, t2;
    char str_in[20];
    int verbose = 0, debug = 0, help = 0, opt_s = 0, opt_n = 0;

    /* Process command line arguments */
    int opt;
    while ((opt = getopt(argc, argv, "hvds:n:")) != -1)
    {
        switch (opt)
        {
        case 'v':
            verbose = 1;
            break;
        case 'h':
            help = 1;
            break;
        case 'd':
            debug = 1;
            break;
        case 's':
            opt_s = atoi(optarg);
            break;
        case 'n':
            opt_n = atoi(optarg);
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-s <seed>] [-n <no. of iterations>] [seq1 seq2]\n", argv[0]);
            fprintf(stderr, "  -h: Show help\n");
            fprintf(stderr, "  -v: Verbose output\n");
            fprintf(stderr, "  -d: Debug output\n");
            fprintf(stderr, "  -s: Random seed\n");
            fprintf(stderr, "  -n: Number of test iterations\n");
            exit(EXIT_FAILURE);
        }
    }

    if (help) {
        printf("MasterMind Matching Function Tester\n");
        printf("This program tests the matching function for the MasterMind game.\n");
        printf("It compares the C implementation with the Assembly implementation.\n");
        printf("Usage: %s [-h] [-v] [-d] [-s <seed>] [-n <no. of iterations>] [seq1 seq2]\n", argv[0]);
        exit(EXIT_SUCCESS);
    }

    /* Allocate memory for sequences */
    seq1 = (int *)malloc(seqlen * sizeof(int));
    seq2 = (int *)malloc(seqlen * sizeof(int));
    cpy1 = (int *)malloc(seqlen * sizeof(int));
    cpy2 = (int *)malloc(seqlen * sizeof(int));
    
    if (seq1 == NULL || seq2 == NULL || cpy1 == NULL || cpy2 == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    /* If sequences are provided as command line arguments */
    if (argc > optind + 1)
    {
        strcpy(str_in, argv[optind]);
        m = atoi(str_in);
        strcpy(str_in, argv[optind + 1]);
        n = atoi(str_in);
        fprintf(stderr, "Testing matches function with sequences %d and %d\n", m, n);
        
        /* Read sequences from command line arguments */
        readSeq(seq1, m);
        readSeq(seq2, n);
        
        /* Make copies for later use */
        memcpy(cpy1, seq1, seqlen * sizeof(int));
        memcpy(cpy2, seq2, seqlen * sizeof(int));
        
        /* Display the sequences */
        if (verbose) {
            printf("Sequences to test:\n");
            showSeq(seq1);
            showSeq(seq2);
        }
        
        /* Test C implementation */
        gettimeofday(&t1, NULL);
        res_c = countMatches(seq1, seq2);
        gettimeofday(&t2, NULL);
        
        /* Calculate elapsed time */
        if (t2.tv_usec < t1.tv_usec) /* Counter wrapped */
            t_c = (1000000 + t2.tv_usec) - t1.tv_usec;
        else
            t_c = t2.tv_usec - t1.tv_usec;
        
        /* Restore original sequences */
        memcpy(seq1, cpy1, seqlen * sizeof(int));
        memcpy(seq2, cpy2, seqlen * sizeof(int));
        
        /* Test Assembly implementation */
        gettimeofday(&t1, NULL);
        res = matches(seq1, seq2);
        gettimeofday(&t2, NULL);
        
        /* Calculate elapsed time */
        if (t2.tv_usec < t1.tv_usec) /* Counter wrapped */
            t = (1000000 + t2.tv_usec) - t1.tv_usec;
        else
            t = t2.tv_usec - t1.tv_usec;
        
        /* Display results */
        printf("Matches (encoded) (in C):   %d\n", res_c);
        printf("Matches (encoded) (in Asm): %d\n", res);
        
        memcpy(seq1, cpy1, seqlen * sizeof(int));
        memcpy(seq2, cpy2, seqlen * sizeof(int));
        
        showMatches(res_c, seq1, seq2, 0);
        showMatches(res, seq1, seq2, 0);
        
        if (res == res_c) {
            printf("__ result OK\n");
        } else {
            printf("** result WRONG\n");
        }
        
        fprintf(stderr, "C   version:\t\tresult=%d (elapsed time: %dμs)\n", res_c, t_c);
        fprintf(stderr, "Asm version:\t\tresult=%d (elapsed time: %dμs)\n", res, t);
    }
    else
    {
        /* Run multiple random tests */
        int n_tests = 10; /* Default number of tests */
        if (opt_n != 0)
            n_tests = opt_n;
        
        fprintf(stderr, "Running tests of matches function with %d pairs of random input sequences ...\n", n_tests);
        
        /* Set random seed */
        if (opt_s != 0)
            srand(opt_s);
        else
            srand(1701); /* Default seed for reproducibility */
        
        int oks = 0, tot = 0;
        
        for (int i = 0; i < n_tests; i++)
        {
            /* Generate random sequences */
            for (int j = 0; j < seqlen; j++)
            {
                seq1[j] = (rand() % seqmax) + 1;
                seq2[j] = (rand() % seqmax) + 1;
            }
            
            /* Make copies for later use */
            memcpy(cpy1, seq1, seqlen * sizeof(int));
            memcpy(cpy2, seq2, seqlen * sizeof(int));
            
            if (verbose)
            {
                fprintf(stderr, "Test %d - Random sequences are:\n", i+1);
                showSeq(seq1);
                showSeq(seq2);
            }
            
            /* Test Assembly implementation */
            res = matches(seq1, seq2);
            
            /* Restore original sequences */
            memcpy(seq1, cpy1, seqlen * sizeof(int));
            memcpy(seq2, cpy2, seqlen * sizeof(int));
            
            /* Test C implementation */
            res_c = countMatches(seq1, seq2);
            
            if (debug)
            {
                fprintf(stdout, "DBG: sequences after matching:\n");
                showSeq(seq1);
                showSeq(seq2);
            }
            
            fprintf(stdout, "Test %d - Matches (encoded) (in C):   %d\n", i+1, res_c);
            fprintf(stdout, "Test %d - Matches (encoded) (in Asm): %d\n", i+1, res);
            
            /* Restore original sequences */
            memcpy(seq1, cpy1, seqlen * sizeof(int));
            memcpy(seq2, cpy2, seqlen * sizeof(int));
            
            showMatches(res_c, seq1, seq2, 0);
            showMatches(res, seq1, seq2, 0);
            
            tot++;
            if (res == res_c)
            {
                fprintf(stdout, "__ result OK\n\n");
                oks++;
            }
            else
            {
                fprintf(stdout, "** result WRONG\n\n");
            }
        }
        
        fprintf(stderr, "%d out of %d tests OK\n", oks, tot);
        
        /* Return success only if all tests passed */
        exit(oks == tot ? 0 : 1);
    }

    /* Free allocated memory */
    free(seq1);
    free(seq2);
    free(cpy1);
    free(cpy2);

    return 0;
}