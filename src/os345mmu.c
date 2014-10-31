// os345mmu.c - LC-3 Memory Management Unit
// **************************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the CS345 projects.          **
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
#include <assert.h>
#include "os345.h"
#include "os345lc3.h"
#include "os345signals.h"

// ***********************************************************************
// mmu variables
extern TCB tcb[];
extern int curTask;

// LC-3 memory
unsigned short int memory[LC3_MAX_MEMORY];

// statistics
int memAccess;						// memory accesses
int memHits;						// memory hits
int memPageFaults;					// memory faults
int nextPage;						// swap page size
int pageReads;						// page reads
int pageWrites;						// page writes

int getFrame(int);
int getAvailableFrame(void);
int runClock(int);
bool tableIsEmpty(int);

// clock replacement data
int startFrame;
int endFrame;
int clockAdr = 0x23fe;

int getFrame(int notme)
{
	int frame;
	frame = getAvailableFrame();
	if (frame >=0)
    { 
//        printf("\nReturning frame %d - no clock", frame);
        return frame;
    }

    // run clock
    frame = runClock(notme);

//    printf("\nReturning frame %d - clock", frame);
	return frame;
}

int runClock(int notme)
{
    int frame = -1;
    int swappta, swappte1, swappte2, swapframe;

    int i = 0;
    while (TRUE)
    {
        clockAdr = clockAdr + 0x02 >= LC3_RPT_END ? LC3_RPT : clockAdr + 0x02;
        if (clockAdr == LC3_RPT && i < 10)
        {
            i += 1;
//            printf("\nFull loop %d", i);
        }
        if (i == 10)
        {
//            printf("\nSignaling");
            sigSignal(0, mySIGINT);
            break;
        }

        int rpta = clockAdr;
        int rpte1 = memory[rpta];
        int rpte2 = memory[rpta + 1];
        int rptframe = FRAME(rpte1);
//        printf("\n%d %d %x %x", notme, rptframe, clockAdr, LC3_RPT_END);

        // Ignore passed in frame
        if (rptframe == notme)
        {
            continue;
        }

        // We want to try and swap out a defined rpt frame
//        printf("\n Not Defined %x %d %x", rpta, rptframe, clockAdr);
        if (DEFINED(rpte1))
        {
  //          printf("\nDefined %d", rptframe);
            // Dereference referenced frames, they should stay for now
            if (REFERENCED(rpte1))
            {
    //            printf("\nReferenced %d", rptframe);
                rpte1 = CLEAR_REF(rpte1);
            }
            else
            {
      //          printf("\nNot Referenced %d", rptframe);
                // If the rpte is defined, not referenced, and not pinned
                //      Swap what it points to
                if (!PINNED(rpte1))
                {
        //            printf("\nNot Pinned %d", rptframe);
                    return swapFrame(rpta, rpte1, rpte2, rptframe);
                }
                else // If rpte is pinned
                {
          //          printf("\nPinned %d", rptframe);
                    // We need to see if we should unpin it
                    if (tableIsEmpty(rptframe<<6))
                    {
            //            printf("\nEmpty Table %d", rptframe);
                        // Because then we can swap the frame
                        rpte1 = CLEAR_PINNED(rpte1);
                        return swapFrame(rpta, rpte1, rpte2, rptframe);
                    }
                    else
                    {
              //          printf("\nNot Empty Table %d", rptframe);
                        int j;
                        for (j = 0; j < 0x40; j += 2)
                        {
                            int upta = (FRAME(rpte1)<<6) + j;
                            int upte1 = memory[upta];
                            int upte2 = memory[upta + 1];
                            int uptframe = FRAME(upte1);

                            if (uptframe == notme)
                            {
                                continue;
                            }

                            if (DEFINED(upte1))
                            {
                                if (REFERENCED(upte1))
                                {
                                    upte1 = CLEAR_REF(upte1);
                                }
                                else
                                {
                                    if (!PAGED(upte2)) rpte1 = SET_DIRTY(rpte1);
                                    memory[rpta] = rpte1;
                                    return swapFrame(upta, upte1, upte2, uptframe);
                                }
                            }
                            memory[upta] = upte1;
                            memory[upta + 1] = upte2;
                        }
                    }
                }

            }

        }
        // Not defined in this context means the frame wasn't
        // initialized in the first place, so ignore it

        memory[rpta] = rpte1;
        memory[rpta + 1] = rpte2;
    }
}

bool tableIsEmpty(int upta)
{
    int i;
    for (i = 0; i < 0x40; i += 2)
    {
        int upte1 = memory[upta + i];
        if (DEFINED(upte1))
        {
            return FALSE;
        }
    }
    return TRUE;
}

bool swapFrame(int pta, int pte1, int pte2, int frame)
{
    // If pte has been paged
    int pnum;
    if (PAGED(pte2))
    {
        // If it is now dirty
//        printf("\nDirty? %s pta: %x", DIRTY(pte1) ? "TRUE" : "FALSE", pta);
        if (DIRTY(pte1))
        {
            // Swap it out again
            pnum = accessPage(SWAPPAGE(pte2), frame, PAGE_OLD_WRITE);
            // Clear the dirty bit
            pte1 = CLEAR_DIRTY(pte1);
        }
    }
    else // Otherwise it needs to be paged
    {
        pnum = accessPage(0, frame, PAGE_NEW_WRITE);
        // Set the new page number
        pte2 |= pnum;
        pte2 = SET_PAGED(pte2);
    }



    // Empty the frame
    memset(&memory[(frame<<6)], 0, 128);
    // Clear defined
    pte1 = CLEAR_DEFINED(pte1);
    memory[pta] = pte1;
    memory[pta + 1] = pte2;

    return frame;
}

// **************************************************************************
// **************************************************************************
// LC3 Memory Management Unit
// Virtual Memory Process
// **************************************************************************
//           ___________________________________Frame defined
//          / __________________________________Dirty frame
//         / / _________________________________Referenced frame
//        / / / ________________________________Pinned in memory
//       / / / /     ___________________________
//      / / / /     /                 __________frame # (0-1023) (2^10)
//     / / / /     /                 / _________page defined
//    / / / /     /                 / /       __page # (0-4096) (2^12)
//   / / / /     /                 / /       /
//  / / / /     / 	             / /       /
// F D R P - - f f|f f f f f f f f|S - - - p p p p|p p p p p p p p

#define MMU_ENABLE	1

unsigned short int *getMemAdr(int va, int rwFlg)
{
	unsigned short int pa;
	int rpta, rpte1, rpte2;
	int upta, upte1, upte2;
	int rptFrame, uptFrame;

	rpta = tcb[curTask].RPT + RPTI(va);
	rpte1 = memory[rpta];
	rpte2 = memory[rpta+1];

    memAccess += 2;
	// turn off virtual addressing for system RAM
	if (va < 0x3000) return &memory[va];
#if MMU_ENABLE
	if (DEFINED(rpte1))
	{
        memHits++;
		// defined
	}
	else
	{
        memPageFaults++;
		// fault
		rptFrame = getFrame(-1);
//        printf("\nROOT %d %x", rptFrame, rptFrame);
		rpte1 = SET_DEFINED(rptFrame);
//        rpte1 = SET_PINNED(rpte1);
		if (PAGED(rpte2))
		{
			accessPage(SWAPPAGE(rpte2), rptFrame, PAGE_READ);
		}
		else
		{
			memset(&memory[(rptFrame<<6)], 0, 128);
		}
	}

    rpte1 = SET_PINNED(rpte1);
	memory[rpta] = rpte1 = SET_REF(rpte1);
	memory[rpta+1] = rpte2;

	upta = (FRAME(rpte1)<<6) + UPTI(va);
	upte1 = memory[upta];
	upte2 = memory[upta+1];

	if (DEFINED(upte1))
	{
        memHits++;
		// defined
	}
	else
	{
        memPageFaults++;
		// fault
		uptFrame = getFrame(FRAME(memory[rpta]));
//        printf("\nUSER %d %x", uptFrame, uptFrame);
		upte1 = SET_DEFINED(uptFrame);
		if (PAGED(upte2))
		{
			accessPage(SWAPPAGE(upte2), uptFrame, PAGE_READ);
		}
		else
		{
			memset(&memory[(uptFrame<<6)], 0, 128);
		}
	}

    if (rwFlg) upte1 = SET_DIRTY(upte1);
	memory[upta] = SET_REF(upte1);
	memory[upta+1] = upte2;


	return &memory[(FRAME(upte1)<<6) + FRAMEOFFSET(va)];
#else
	return &memory[va];
#endif
} // end getMemAdr


// **************************************************************************
// **************************************************************************
// set frames available from sf to ef
//    flg = 0 -> clear all others
//        = 1 -> just add bits
//
void setFrameTableBits(int flg, int sf, int ef)
{	int i, data;
	int adr = LC3_FBT-1;             // index to frame bit table
	int fmask = 0x0001;              // bit mask

	// 1024 frames in LC-3 memory
	for (i=0; i<LC3_FRAMES; i++)
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;
			adr++;
			data = (flg)?MEMWORD(adr):0;
		}
		else fmask = fmask >> 1;
		// allocate frame if in range
//        printf("\n%x %x %x", adr, fmask, data); 
		if ( (i >= sf) && (i < ef)) { data = data | fmask; }
		MEMWORD(adr) = data;
	}
    startFrame = sf;
    endFrame = ef;
	return;
} // end setFrameTableBits


// **************************************************************************
// get frame from frame bit table (else return -1)
int getAvailableFrame()
{
	int i, data;
	int adr = LC3_FBT - 1;				// index to frame bit table
	int fmask = 0x0001;					// bit mask

	for (i=0; i<LC3_FRAMES; i++)		// look thru all frames
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;				// move to next work
			adr++;
			data = MEMWORD(adr);
		}
		else fmask = fmask >> 1;		// next frame
		// deallocate frame and return frame #
		if (data & fmask)
		{  MEMWORD(adr) = data & ~fmask;
			return i;
		}
	}
	return -1;
} // end getAvailableFrame



// **************************************************************************
// read/write to swap space
int accessPage(int pnum, int frame, int rwnFlg)
{
   static unsigned short int swapMemory[LC3_MAX_SWAP_MEMORY];

   if ((nextPage >= LC3_MAX_PAGE) || (pnum >= LC3_MAX_PAGE))
   {
      printf("\nVirtual Memory Space Exceeded!  (%d)", LC3_MAX_PAGE);
      exit(-4);
   }
   switch(rwnFlg)
   {
      case PAGE_INIT:                    		// init paging
         nextPage = 0;
         return 0;

      case PAGE_GET_ADR:                    	// return page address
         return (int)(&swapMemory[pnum<<6]);

      case PAGE_NEW_WRITE:                   // new write (Drops thru to write old)
         pnum = nextPage++;
//         printf("\n%d pnum", pnum);

      case PAGE_OLD_WRITE:                   // write
         //printf("\n    (%d) Write frame %d (memory[%04x]) to page %d", p.PID, frame, frame<<6, pnum);
         memcpy(&swapMemory[pnum<<6], &memory[frame<<6], 1<<7);
         pageWrites++;
         return pnum;

      case PAGE_READ:                    // read
         //printf("\n    (%d) Read page %d into frame %d (memory[%04x])", p.PID, pnum, frame, frame<<6);
         memcpy(&memory[frame<<6], &swapMemory[pnum<<6], 1<<7);
         pageReads++;
         return pnum;

      case PAGE_FREE:                   // free page
         break;
   }
   return pnum;
} // end accessPage
