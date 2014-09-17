// os345interrupts.c - pollInterrupts	08/08/2013
// ***********************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the BYU CS345 projects.      **
// ** It comes "as is" and "unwarranted."  As such, when you use part   **
// ** or all of the code, it becomes "yours" and you are responsible to **
// ** understand any algorithm or method presented.  Likewise, any      **
// ** errors or problems become your responsibility to fix.             **
// **                                                                   **
// ** NOTES:                                                            **
// ** -Comments beginning with "// ??" may require some implementation. **
// ** -Tab stops are set at every 3 spaces.                             **
// ** -The function API's in "OS345.h" should not be altered.           **
// **                                                                   **
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// ***********************************************************************

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <assert.h>

#include "os345.h"
#include "os345config.h"
#include "os345signals.h"

// **********************************************************************
//	local prototypes
//
void pollInterrupts(void);
static void keyboard_isr(void);
static void timer_isr(void);
void clearBuffer(void);

// **********************************************************************
// **********************************************************************
// global semaphores

extern Semaphore* keyboard;				// keyboard semaphore
extern Semaphore* charReady;				// character has been entered
extern Semaphore* inBufferReady;			// input buffer ready semaphore

extern Semaphore* tics1sec;				// 1 second semaphore
extern Semaphore* tics10thsec;				// 1/10 second semaphore

extern char inChar;				// last entered character
extern int charFlag;				// 0 => buffered input
extern int inBufIndx;				// input pointer into input buffer
extern int cursor;
extern char inBuffer[INBUF_SIZE+1];	// character input buffer

extern time_t oldTime1;					// old 1sec time
extern clock_t myClkTime;
extern clock_t myOldClkTime;

extern int pollClock;				// current clock()
extern int lastPollClock;			// last pollClock

extern int superMode;						// system mode

extern TCB tcb[];

bool escape = FALSE;
bool ansi = FALSE;

// **********************************************************************
// **********************************************************************
// simulate asynchronous interrupts by polling events during idle loop
//
void pollInterrupts(void)
{
	// check for task monopoly
	pollClock = clock();
	assert("Timeout" && ((pollClock - lastPollClock) < MAX_CYCLES));
	lastPollClock = pollClock;

	// check for keyboard interrupt
	if ((inChar = GET_CHAR) > 0)
	{
	  keyboard_isr();
	}

	// timer interrupt
	timer_isr();

	return;
} // end pollInterrupts


// **********************************************************************
// keyboard interrupt service routine
//
static void keyboard_isr()
{
	// assert system mode
	assert("keyboard_isr Error" && superMode);
//    printf("---%d\n", (int)inChar);

	semSignal(charReady);					// SIGNAL(charReady) (No Swap)
	if (charFlag == 0)
	{
		switch (inChar)
		{
			case '\r':
			case '\n':
			{
				inBufIndx = 0;				// EOL, signal line ready
                cursor = 0;
				semSignal(inBufferReady);	// SIGNAL(inBufferReady)
				break;
			}

            case 0x12:                      // ^r
            {
                inBufIndx = 0;
                cursor = 0;
                inBuffer[0] = 0;
                sigSignal(-1, mySIGCONT);
                int i;
                for (i = 0; i < MAX_TASKS; ++i) {
                    tcb[i].signal &= ~mySIGTSTP;
                    tcb[i].signal &= ~mySIGSTOP;
                }
                break;
            }

            case 0x17:                      // ^w
            {
                inBufIndx = 0;
                cursor = 0;
                inBuffer[0] = 0;
                sigSignal(-1, mySIGTSTP);
                break;
            }

			case 0x18:						// ^x
			{
				inBufIndx = 0;
                cursor = 0;
				inBuffer[0] = 0;
				sigSignal(0, mySIGINT);		// interrupt task 0
				semSignal(inBufferReady);	// SEM_SIGNAL(inBufferReady)
				break;
			}

            case 127:
            case 0x08:                      // backspace
            {
                H_OFF;
                if (cursor > 0) {
                    printf("\b \b");
                    inBuffer[--cursor] = 0;
                }
                break;
            }

            case '\033':
            {
                escape = TRUE;
                break;
            }

            case '[':
            {
                if (escape) {
                    ansi = TRUE;
                    break;
                }
            }
            case 'A':
            {
                if (escape && ansi) {
                    // move queue up
                    historyUp();
                    escape = FALSE;
                    ansi = FALSE;
                    break;
                }
            }
            case 'B':
            {
                if (escape && ansi) {
                    // move queue down
                    historyDown();
                    escape = FALSE;
                    ansi = FALSE;
                    break;
                }
            }
            case 'C':
            {
                if (escape && ansi) {
                    if (cursor < inBufIndx) {
                        printf("%s", "\033[1C");
                        ++cursor;
                    }
                    // move cursor right
                    escape = FALSE;
                    ansi = FALSE;
                    break;
                }
            }
            case 'D':
            {
                if (escape && ansi) {
                    if (cursor > 0) {
                        printf("\b");
                        --cursor;
                    }
                    // move cursor left
                    escape = FALSE;
                    ansi = FALSE;
                    break;
                }
            }

			default:
			{
                H_OFF;
                if (cursor == inBufIndx)
                    ++inBufIndx;

				inBuffer[cursor++] = inChar;
				inBuffer[inBufIndx] = 0;
                printf("%c", inChar);
//				printf("\33[2K\r%s", inBuffer);		// echo character
//                printf("  char(%c)  \n", inChar);
			}
		}
	}
	else
	{
		// single character mode
		inBufIndx = 0;
		inBuffer[inBufIndx] = 0;
	}
	return;
} // end keyboard_isr

// **********************************************************************
// shell input helper functions
// clears anything typed from the screen and from the input buffer
void clearBuffer() {
    int i;
    for (i = 0; i < cursor; ++i) {
        printf("\b");
    }
    for (i = 0; i < inBufIndx; ++i) {
        printf(" ");
    }
    int max = inBufIndx;
    for (i = 0; i < max; ++i) {
        printf("\b");
        inBuffer[--inBufIndx] = 0;
    }
    cursor = 0;

}

// displays the previous typed command in the list
int historyUp() {
    if (cmdHistory->head < 0 || cmdHistory->tail < 0 || cmdHistory->current < 0)
        return;

    if (cmdHistory->head != cmdHistory->current || cmdHistory->head == cmdHistory->tail) {
        clearBuffer();
        if (cmdHistory->active && cmdHistory->head != cmdHistory->tail)
            cmdHistory->current = cmdHistory->current - 1 < 0 ? MAX_HISTORY - 1 : cmdHistory->current - 1;
        else
            H_ON;

        strcpy(inBuffer, cmdHistory->history[cmdHistory->current]);
        inBufIndx = strlen(cmdHistory->history[cmdHistory->current]);
        cursor = inBufIndx;
        printf("%s", inBuffer);
    }
}

// displays the next typed command in the list
int historyDown() {
    if (cmdHistory->tail != cmdHistory->current) {
        clearBuffer();
        cmdHistory->current = cmdHistory->current + 1 > MAX_HISTORY - 1 ? 0 : cmdHistory->current + 1;
        strcpy(inBuffer, cmdHistory->history[cmdHistory->current]);
        inBufIndx = strlen(cmdHistory->history[cmdHistory->current]);
        cursor = inBufIndx;
        printf("%s", inBuffer);
    }
    else if (cmdHistory->active) {
        clearBuffer();
        H_OFF;
    }
}

// **********************************************************************
// timer interrupt service routine
//
static void timer_isr()
{
	time_t currentTime;						// current time

	// assert system mode
	assert("timer_isr Error" && superMode);

	// capture current time
  	time(&currentTime);

  	// one second timer
  	if ((currentTime - oldTime1) >= 1)
  	{
		// signal 1 second
  	   semSignal(tics1sec);
		oldTime1 += 1;
  	}

	// sample fine clock
	myClkTime = clock();
	if ((myClkTime - myOldClkTime) >= ONE_TENTH_SEC)
	{
		myOldClkTime = myOldClkTime + ONE_TENTH_SEC;   // update old
		semSignal(tics10thsec);
	}

	// ?? add other timer sampling/signaling code here for project 2

	return;
} // end timer_isr
