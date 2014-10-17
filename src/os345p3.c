// os345p3.c - Jurassic Park
// ***********************************************************************
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
#include <time.h>
#include <assert.h>
#include "os345.h"
#include "os345park.h"

// ***********************************************************************
// project 3 variables

// Jurassic Park

extern TCB tcb[];
extern JPARK myPark;
extern Semaphore* parkMutex;						// protect park access
extern Semaphore* fillSeat[NUM_CARS];			// (signal) seat ready to fill
extern Semaphore* seatFilled[NUM_CARS];		// (wait) passenger seated
extern Semaphore* rideOver[NUM_CARS];			// (signal) ride over

extern DeltaClock* deltaClockList;          // delta clock list
extern Semaphore* deltaClockMutex;          // mutex for delta clock access
extern Semaphore* tics10thsec;

Semaphore* dcChange;
int timeTaskID;
Semaphore* visitorRideDoneSem;
Semaphore* driverRideDoneSem;
int carNeedingDriver;

Semaphore* passengerNeeded;
Semaphore* passengerFound;
Semaphore* getDriver;
Semaphore* driverFound;

Semaphore* parkLine;
Semaphore* ticketLine;
Semaphore* tickets;
Semaphore* museumLine;
Semaphore* driverNeeded;
Semaphore* giftShopLine;

// ***********************************************************************
// project 3 functions and tasks
void LC3_project3(int, char**);
void LC3_dc(int, char**);

int carTask(int, char**);
int visitorTask(int, char**);
int driverTask(int, char**);

bool insertDeltaClock(int, Semaphore*);
int dcMonitorTask(int, char**);
int timeTask(int, char**);
void printDeltaClock(void);

// ***********************************************************************
// ***********************************************************************
// project3 command
int P3_project3(int argc, char* argv[])
{
	char buf[32];
	char* newArgv[2];
    char* tickArgv[0];
    carNeedingDriver = 0;

    getDriver = createSemaphore("getDriver", COUNTING, 0);
    passengerNeeded = createSemaphore("passengerNeeded", BINARY, 0);
    passengerFound = createSemaphore("passengerFound", BINARY, 0);
    driverNeeded = createSemaphore("driverNeeded", BINARY, 0);
    driverFound = createSemaphore("driverFound", BINARY, 0);
    parkLine = createSemaphore("parkLine", COUNTING, MAX_IN_PARK);
    tickets = createSemaphore("tickets", COUNTING, MAX_TICKETS);
    ticketLine = createSemaphore("ticketLine", COUNTING, 0);
    museumLine = createSemaphore("museumLine", COUNTING, MAX_IN_MUSEUM);
    giftShopLine = createSemaphore("giftShopLine", COUNTING, MAX_IN_GIFTSHOP);

    // start delta clock
    createTask( "tickDelta",
            tickDelta,
            HIGH_PRIORITY,
            1,
            0,
            tickArgv);

	// start park
	sprintf(buf, "jurassicPark");
	newArgv[0] = buf;
	createTask( buf,				// task name
		jurassicTask,				// task
		MED_PRIORITY,				// task priority
        1,
		1,						    // task count
		newArgv);					// task argument

    int i;
    char* taskArgv[1];
    taskArgv[0] = calloc(32, sizeof(char));

    char argbuf[32];
    // start car tasks
    for (i = 0; i < NUM_CARS; ++i)
    {
        memset(argbuf, '\0', sizeof(argbuf));
        sprintf(argbuf, "%d", i);
        strcpy(taskArgv[0], argbuf);
        sprintf(buf, "carTask[%d]", i);
        createTask( buf,
                carTask,
                MED_PRIORITY,
                1,
                1,
                taskArgv);
    }

    // start visitor tasks
    char* visitorArgv[1];
    for (i = 0; i < NUM_VISITORS; ++i)
    {
        memset(argbuf, '\0', sizeof(argbuf));
        sprintf(argbuf, "%d", i);
        strcpy(taskArgv[0], argbuf);
        sprintf(buf, "visitorTask[%d]", i);
        createTask( buf,
                visitorTask,
                MED_PRIORITY,
                1,
                1,
                taskArgv);
    }

    // start driver tasks
    char* driverArgv[1];
    for (i = 0; i < NUM_DRIVERS; ++i)
    {
        memset(argbuf, '\0', sizeof(argbuf));
        sprintf(argbuf, "%d", i);
        strcpy(taskArgv[0], argbuf);
        sprintf(buf, "driverTask[%d]", i);
        createTask( buf,
                driverTask,
                MED_PRIORITY,
                1,
                1,
                taskArgv);
    }

    free(taskArgv[0]);

	// wait for park to get initialized...
	while (!parkMutex) SWAP;
	printf("\nStart Jurassic Park...");

	//?? create car, driver, and visitor tasks here

	return 0;
} // end project3

// ***********************************************************************
// ***********************************************************************
// car task
int carTask(int argc, char* argv[])
{
    int carId = atoi(argv[0]);                          SWAP;
    Semaphore* rideDone[3];
    Semaphore* driverDone;
    while (1)
    {
        if (fillSeat[carId])
        {
            int i;
            for (i = 0; i < NUM_SEATS; ++i)
            {
                semWait(fillSeat[carId]);               SWAP;
                semSignal(passengerNeeded);             SWAP;
                semWait(passengerFound);                SWAP;
                rideDone[i] = visitorRideDoneSem;       SWAP;

                if (i == NUM_SEATS - 1)
                {
                    semSignal(driverNeeded);            SWAP;
                    carNeedingDriver = carId + 1;       SWAP;
                    semSignal(getDriver);               SWAP;
                    semWait(driverFound);               SWAP;
                    driverDone = driverRideDoneSem;     SWAP;
                }

                semSignal(seatFilled[carId]);           SWAP;
            }

            semWait(rideOver[carId]);                   SWAP;
            semSignal(driverDone);                      SWAP;
            for (i = 0; i < NUM_SEATS; ++i)
            {
                semSignal(rideDone[i]);                 SWAP;
            }
        }
        else
        {
            SWAP;
        }
    }
}


// ***********************************************************************
// ***********************************************************************
// visitor task
int visitorTask(int argc, char* argv[])
{
    int visitorId = atoi(argv[0]);                      SWAP;
    char buf[32];
    sprintf(buf, "timer_visitorTimerSem[%d]", visitorId);   SWAP;
//    printf("\nsem: %s", buf);
    Semaphore* timeSem = createSemaphore(buf, BINARY, 0); SWAP;

    // wait random time (0-10 sec) to enter park
    int enterTime = rand() % 100;                   SWAP;
    sprintf(buf, "timer_enteringPark[%d]", visitorId);  SWAP;
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(enterTime, timeSem);           SWAP;
    semWait(timeSem);                               SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numOutsidePark++;                        SWAP;
    semSignal(parkMutex);                           SWAP;

    semWait(parkLine);                              SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numOutsidePark--;                        SWAP;
    myPark.numInPark++;                             SWAP;
    myPark.numInTicketLine++;                       SWAP;
    semSignal(parkMutex);                           SWAP;

    // wait 0-3 sec to get in ticket line
    int waitTime = rand() % 30;                     SWAP;
    sprintf(buf, "timer_gettingInTicketLine[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    // enter ticket line
    semSignal(getDriver);                           SWAP;
    semWait(ticketLine);                            SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numTicketsAvailable--;                   SWAP;
    myPark.numInTicketLine--;                       SWAP;
    myPark.numInMuseumLine++;                       SWAP;
    semSignal(parkMutex);                           SWAP;

    // wait 0-3 sec to get in museum line
    waitTime = rand() % 30;                         SWAP;
    sprintf(buf, "timer_gettingInMuseumLine[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    // enter museum line
    semWait(museumLine);                            SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numInMuseumLine--;                       SWAP;
    myPark.numInMuseum++;                           SWAP;
    semSignal(parkMutex);                           SWAP;

    // wait 0-3 sec in museum
    waitTime = rand() % 30;                         SWAP;
    sprintf(buf, "timer_inMuseum[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numInMuseum--;                           SWAP;
    myPark.numInCarLine++;                          SWAP;
    semSignal(parkMutex);                           SWAP;

    semSignal(museumLine);                          SWAP;

    // wait 0-3 set to get in tour car line
    waitTime = rand() % 30;                         SWAP;
    sprintf(buf, "timer_gettingInCarLine[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    // enter tour car line
    semWait(passengerNeeded);                       SWAP;
    visitorRideDoneSem = timeSem;                   SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numInCarLine--;                          SWAP;
    myPark.numInCars++;                             SWAP;
    semSignal(parkMutex);                           SWAP;

    semSignal(passengerFound);                      SWAP;

    // wait until rideDone
    sprintf(buf, "timer_rideDone[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    semWait(timeSem);                               SWAP;

    // release ticket
    semSignal(tickets);                             SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numTicketsAvailable++;
    myPark.numInCars--;                             SWAP;
    myPark.numInGiftLine++;                         SWAP;
    semSignal(parkMutex);                           SWAP;

    // wait 0-3 sec to get in gift shop line
    waitTime = rand() % 30;                         SWAP;
    sprintf(buf, "timer_gettingInGiftLine[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    // enter gift shop line
    semWait(giftShopLine);                          SWAP;

    semWait(parkMutex);                             SWAP;
    myPark.numInGiftLine--;                         SWAP;
    myPark.numInGiftShop++;                         SWAP;
    semSignal(parkMutex);                           SWAP;

    // wait 0-3 sec in gift shop
    waitTime = rand() % 30;                         SWAP;
    sprintf(buf, "timer_inGiftShop[%d]", visitorId);
//    printf("\n%s", buf);
    strcpy(timeSem->name, buf);
    insertDeltaClock(waitTime, timeSem);            SWAP;
    semWait(timeSem);                               SWAP;

    // leave gift shop and park
    semWait(parkMutex);                             SWAP;
    myPark.numInGiftShop--;                         SWAP;
    myPark.numInPark--;                             SWAP;
    myPark.numExitedPark++;                         SWAP;
    semSignal(parkMutex);                           SWAP;

    semSignal(giftShopLine);

    semSignal(parkLine);                            SWAP;
    
    return 0;
}


// ***********************************************************************
// ***********************************************************************
// driver task
int driverTask(int argc, char* argv[])
{
    int driverId = atoi(argv[0]);
    char buf[32];
    sprintf(buf, "driverDone[%d]", driverId);
    Semaphore* driverDone = createSemaphore(buf, BINARY, 0);  SWAP;

    while (1)
    {
        // wait to be awoken
        semWait(parkMutex);                         SWAP;
        myPark.drivers[driverId] = 0;               SWAP;
        semSignal(parkMutex);                       SWAP;
        semWait(getDriver);                         SWAP;

        bool done = FALSE;                          SWAP;
        while (!done)
        {
            if (semTryLock(tickets))
            {
                semWait(tickets);                       SWAP;
                semWait(parkMutex);                     SWAP;
                myPark.drivers[driverId] = -1;          SWAP;
                semSignal(parkMutex);                   SWAP;
                semSignal(ticketLine);                  SWAP;
                done = TRUE;                            SWAP;
            }
            else if (semTryLock(driverNeeded))
            {
                semWait(driverNeeded);              SWAP;
                semWait(parkMutex);                 SWAP;
                int car = carNeedingDriver;         SWAP;
                myPark.drivers[driverId] = car;     SWAP;
                semSignal(parkMutex);               SWAP;

                driverRideDoneSem = driverDone;     SWAP;
                semSignal(driverFound);             SWAP;
                semWait(driverDone);                SWAP;
                done = TRUE;                        SWAP;
            }
            else
            {
                SWAP;
            }
        }
    }
}


// ***********************************************************************
// ***********************************************************************
// delta clock command
int P3_dc(int argc, char* argv[])
{
	printf("\nDelta Clock");
    printDeltaClock();
	// ?? Implement a routine to display the current delta clock contents

	return 0;
} // end CL3_dc


// ***********************************************************************
// ***********************************************************************
// insert new delta clock event
bool insertDeltaClock(int time, Semaphore* sem)
{
    semWait(deltaClockMutex);

    DeltaClock* newClock = 0;
    if (time >= 0 && sem && sem->name && sem->q) // if the parameters are valid
    {
        newClock = malloc(sizeof(DeltaClock)); // allocate new memory
        newClock->sem = sem;
        newClock->clockLink = 0;
    }
    else // otherwise fail
    {
        printf("\nInvalid delta clock parameters");
        semSignal(deltaClockMutex);
        return FALSE;
    }
    
    if (!deltaClockList) // if the head has not been allocated
    {
        deltaClockList = newClock; // do it
        deltaClockList->time = time;
        semSignal(deltaClockMutex);
        return TRUE;
    }

    DeltaClock* dc = deltaClockList;
    int newTime = time;
    if (time < dc->time) // if the new clock goes before the head
    {
        DeltaClock* old = deltaClockList; // put it there
        old->time = old->time - time;
        newClock->time = time;
        deltaClockList = newClock;
        deltaClockList->clockLink = old;
        semSignal(deltaClockMutex);
        return TRUE;
    }
    newTime = newTime - dc->time;

    // loop through dc events
    while (dc) 
    {
        if (dc->clockLink) // if there is a next event
        {
            DeltaClock* next = dc->clockLink; // get it
            int tempTime = newTime - next->time; // find possible value for new time
            if (tempTime < 0) // if the new clock should be inserted before next
            {
                dc->clockLink = newClock; // stick it in
                newClock->clockLink = next;
                newClock->time = newTime;
                next->time = tempTime * -1; // set next time
                semSignal(deltaClockMutex);
                return TRUE;
            }
            else // if the new clock should not be inserted here
            {
                newTime = tempTime; // keep going
                dc = next;
            }
        }
        else // if there is no next event
        {
            dc->clockLink = newClock; // stick the new clock in here
            newClock->time = newTime;
            semSignal(deltaClockMutex);
            return TRUE;
        }
    }

    // I don't know how we got here...
    semSignal(deltaClockMutex);
    return FALSE;
}


// ***********************************************************************
// ***********************************************************************
// tick delta clock
int tickDelta(int argc, char* argv[])
{
    while (1)
    {
        semWait(tics10thsec);
        semWait(deltaClockMutex);
        if (!deltaClockList)
        {
            semSignal(deltaClockMutex);
            continue;
        }

//        printDeltaClock();
        // decrement clock 1 tick
        deltaClockList->time = deltaClockList->time - 1;
        bool change = FALSE;
        while (deltaClockList && deltaClockList->time <= 0) // sem signal all 0 clock events and free them
        {
            semSignal(deltaClockList->sem);
            DeltaClock* old = deltaClockList;
            deltaClockList = old->clockLink;
            old->clockLink = 0;
            old->sem = 0;
            free(old);

            change = TRUE;
        }
        if (dcChange)
        {
            semSignal(dcChange);
        }
        semSignal(deltaClockMutex);
    }
    return 0;
}


// ***********************************************************************
// test delta clock
int P3_tdc(int argc, char* argv[])
{
    createTask( "tickDelta",
        tickDelta,
        HIGH_PRIORITY,
        1,
        argc,
        argv);

	createTask( "DC Test",			// task name
		dcMonitorTask,		// task
		10,					// task priority
        1,
		argc,					// task arguments
		argv);

	timeTaskID = createTask( "Time",		// task name
		timeTask,	// task
		10,			// task priority
        1,
		argc,			// task arguments
		argv);
	return 0;
} // end P3_tdc


// ***********************************************************************
// monitor the delta clock task
int dcMonitorTask(int argc, char* argv[])
{
    dcChange = createSemaphore("dcChange", BINARY, 0);
	int i, flg;
	char buf[32];
	// create some test times for event[0-9]
    Semaphore* event[10];
	int ttime[10] = {
		90, 300, 50, 170, 340, 300, 50, 300, 40, 110	};

	for (i=0; i<10; i++)
	{
		sprintf(buf, "event[%d]", i);
		event[i] = createSemaphore(buf, BINARY, 0);
		insertDeltaClock(ttime[i], event[i]);
	}
	printDeltaClock();

    int numDeltaClock = 10;
	while (numDeltaClock > 0)
	{
		SEM_WAIT(dcChange)
		flg = 0;
		for (i=0; i<10; i++)
		{
			if (event[i]->state ==1)			{
					printf("\n  event[%d] signaled", i);
					event[i]->state = 0;
					flg = 1;
                    --numDeltaClock;
				}
		}
		if (flg) printDeltaClock();
	}
	printf("\nNo more events in Delta Clock");

	// kill dcMonitorTask
	tcb[timeTaskID].state = S_EXIT;
    deleteSemaphore(&dcChange);
	return 0;
} // end dcMonitorTask


extern Semaphore* tics1sec;

// ********************************************************************************************
// display time every tics1sec
int timeTask(int argc, char* argv[])
{
	char svtime[64];						// ascii current time
	while (1)
	{
		SEM_WAIT(tics1sec);
        SWAP;
		printf("\nTime = %s", myTime(svtime));
	}
	return 0;
} // end timeTask


// ***********************************************************************
// display all pending events in the delta clock list
void printDeltaClock(void)
{
    int count = 0;
    DeltaClock* dc = deltaClockList;
    while (dc)
    {
        printf("\n%4d%4d  %-20s", count++, dc->time, dc->sem->name);
        dc = dc->clockLink;
    }
    printf("\ndone");
	//	printf("\n%4d%4d  %-20s", i, deltaClock[i].time, deltaClock[i].sem->name);
	return;
}


/*
// ***********************************************************************
// ***********************************************************************
// ***********************************************************************
// ***********************************************************************
// ***********************************************************************
// ***********************************************************************
// delta clock command
int P3_dc(int argc, char* argv[])
{
	printf("\nDelta Clock");
	// ?? Implement a routine to display the current delta clock contents
	//printf("\nTo Be Implemented!");
	int i;
	for (i=0; i<numDeltaClock; i++)
	{
		printf("\n%4d%4d  %-20s", i, deltaClock[i].time, deltaClock[i].sem->name);
	}
	return 0;
} // end CL3_dc







*/

