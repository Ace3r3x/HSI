@ This ARM Assembler code implements a matching function for the MasterMind game.
@ It counts exact matches (right color in right position) and approximate matches
@ (right color but wrong position), ensuring each peg is counted only once.
	
@ Example (first sequence is secret, second sequence is guess):
@ 1 2 1
@ 3 1 3 ==> 0 1 (0 exact matches, 1 approximate match)
@
@ -----------------------------------------------------------------------------

.text
@ this is the matching fct that should be called from the C part of the CW	
.global         matches
@ use the name `main` here, for standalone testing of the assembler code
.global         main

@ -----------------------------------------------------------------------------
@ Main function for standalone testing
main: 
    LDR  R0, =secret    @ Load address of secret sequence
    LDR  R1, =guess     @ Load address of guess sequence
    BL   matches        @ Call the matching function
    
    @ Display the result
    MOV  R1, R0         @ Move result to R1 for printing
    LDR  R0, =result_str @ Load format string
    BL   printf         @ Call printf
    
exit:
    MOV  R7, #1         @ syscall number for exit
    MOV  R0, #0         @ exit code 0
    SWI  0              @ software interrupt

@ -----------------------------------------------------------------------------
@ matches function - compares two sequences and returns exact and approximate matches
@ Input:  R0 = pointer to secret sequence, R1 = pointer to guess sequence
@ Output: R0 = result encoded as (exact*10 + approximate)
matches:
    PUSH {R4-R11, LR}   @ Save registers we'll use
    
    @ Initialize counters and arrays
    MOV  R4, #0         @ R4 = exact matches counter
    MOV  R5, #0         @ R5 = approximate matches counter
    MOV  R6, R0         @ R6 = pointer to secret sequence
    MOV  R7, R1         @ R7 = pointer to guess sequence
    
    @ Allocate space on stack for used flags
    SUB  SP, SP, #24    @ 6 words: 3 for secret_used, 3 for guess_used
    MOV  R0, SP         @ R0 = pointer to secret_used array
    MOV  R1, #0         @ Initialize with 0
    STR  R1, [R0]       @ secret_used[0] = 0
    STR  R1, [R0, #4]   @ secret_used[1] = 0
    STR  R1, [R0, #8]   @ secret_used[2] = 0
    
    ADD  R1, R0, #12    @ R1 = pointer to guess_used array
    MOV  R2, #0         @ Initialize with 0
    STR  R2, [R1]       @ guess_used[0] = 0
    STR  R2, [R1, #4]   @ guess_used[1] = 0
    STR  R2, [R1, #8]   @ guess_used[2] = 0
    
    @ First pass: Find exact matches
    MOV  R8, #0         @ R8 = loop counter (i)
exact_loop:
    CMP  R8, #LEN       @ Compare i with sequence length
    BGE  exact_done     @ If i >= LEN, exit loop
    
    @ Load secret[i] and guess[i]
    LSL  R9, R8, #2     @ R9 = i * 4 (word size)
    LDR  R10, [R6, R9]  @ R10 = secret[i]
    LDR  R11, [R7, R9]  @ R11 = guess[i]
    
    @ Compare for exact match
    CMP  R10, R11       @ Compare secret[i] with guess[i]
    BNE  next_exact     @ If not equal, skip to next iteration
    
    @ Found exact match
    ADD  R4, R4, #1     @ Increment exact matches counter
    
    @ Mark as used
    MOV  R2, #1         @ Used flag = 1
    STR  R2, [R0, R9]   @ secret_used[i] = 1
    STR  R2, [R1, R9]   @ guess_used[i] = 1
    
next_exact:
    ADD  R8, R8, #1     @ Increment loop counter
    B    exact_loop     @ Continue loop
    
exact_done:
    @ Second pass: Find approximate matches
    MOV  R8, #0         @ R8 = outer loop counter (i)
approx_outer_loop:
    CMP  R8, #LEN       @ Compare i with sequence length
    BGE  approx_done    @ If i >= LEN, exit loop
    
    @ Check if secret[i] is already used
    LSL  R9, R8, #2     @ R9 = i * 4
    LDR  R2, [R0, R9]   @ R2 = secret_used[i]
    CMP  R2, #1         @ Check if already used
    BEQ  next_outer     @ If used, skip to next iteration
    
    @ Inner loop: check all positions in guess
    MOV  R10, #0        @ R10 = inner loop counter (j)
approx_inner_loop:
    CMP  R10, #LEN      @ Compare j with sequence length
    BGE  next_outer     @ If j >= LEN, exit inner loop
    
    @ Check if guess[j] is already used
    LSL  R11, R10, #2   @ R11 = j * 4
    LDR  R2, [R1, R11]  @ R2 = guess_used[j]
    CMP  R2, #1         @ Check if already used
    BEQ  next_inner     @ If used, skip to next iteration
    
    @ Compare secret[i] with guess[j]
    LDR  R2, [R6, R9]   @ R2 = secret[i]
    LDR  R3, [R7, R11]  @ R3 = guess[j]
    CMP  R2, R3         @ Compare values
    BNE  next_inner     @ If not equal, skip to next iteration
    
    @ Found approximate match
    ADD  R5, R5, #1     @ Increment approximate matches counter
    
    @ Mark both as used
    MOV  R2, #1         @ Used flag = 1
    STR  R2, [R0, R9]   @ secret_used[i] = 1
    STR  R2, [R1, R11]  @ guess_used[j] = 1
    
    @ Exit inner loop after finding a match
    B    next_outer
    
next_inner:
    ADD  R10, R10, #1   @ Increment inner loop counter
    B    approx_inner_loop @ Continue inner loop
    
next_outer:
    ADD  R8, R8, #1     @ Increment outer loop counter
    B    approx_outer_loop @ Continue outer loop
    
approx_done:
    @ Calculate final result: exact*10 + approximate
    MOV  R0, R4         @ R0 = exact matches
    MOV  R1, #10        @ R1 = 10
    MUL  R0, R0, R1     @ R0 = exact * 10
    ADD  R0, R0, R5     @ R0 = (exact * 10) + approximate
    
    @ Clean up and return
    ADD  SP, SP, #24    @ Deallocate stack space
    POP  {R4-R11, PC}   @ Restore registers and return
    
@ -----------------------------------------------------------------------------
@ showseq - displays a sequence for debugging
@ Input: R0 = pointer to a sequence of 3 int values to show
showseq:
    PUSH {R0-R3, LR}    @ Save registers
    
    @ Load the three values from the sequence
    LDR  R1, [R0]       @ R1 = seq[0]
    LDR  R2, [R0, #4]   @ R2 = seq[1]
    LDR  R3, [R0, #8]   @ R3 = seq[2]
    
    @ Call printf with format string and values
    LDR  R0, =f4str     @ R0 = format string
    BL   printf         @ Call printf
    
    POP  {R0-R3, PC}    @ Restore registers and return
    
@ =============================================================================

.data

@ Constants about the basic setup of the game
.equ LEN, 3             @ Length of sequence
.equ COL, 3             @ Number of colors
.equ NAN1, 8            @ Not-a-number value 1
.equ NAN2, 9            @ Not-a-number value 2

@ Format strings for output
f4str:      .asciz "Seq:    %d %d %d\n"
result_str: .asciz "Result: %d (exact=%d, approx=%d)\n"

@ Memory location for temporary storage
n: .word 0x00
	
@ Test case 1: Expect 0 exact, 1 approximate
.align 4
secret: .word 1, 2, 1    @ Secret sequence: 1 2 1
guess:  .word 3, 1, 3    @ Guess sequence:  3 1 3
expect: .byte 0, 1       @ Expected result: 0 exact, 1 approximate

@ Test case 2: Expect 1 exact, 1 approximate
.align 4
secret1: .word 1, 2, 3   @ Secret sequence: 1 2 3
guess1:  .word 1, 1, 2   @ Guess sequence:  1 1 2
expect1: .byte 1, 1      @ Expected result: 1 exact, 1 approximate

@ Test case 3: Expect 1 exact, 0 approximate
.align 4
secret2: .word 2, 3, 2   @ Secret sequence: 2 3 2
guess2:  .word 3, 3, 1   @ Guess sequence:  3 3 1
expect2: .byte 1, 0      @ Expected result: 1 exact, 0 approximate