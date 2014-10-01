// os345p1.c - Command Line Processor
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
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include "os345.h"
#include "os345signals.h"

// The 'reset_context' comes from 'main' in os345.c.  Proper shut-down
// procedure is to long jump to the 'reset_context' passing in the
// power down code from 'os345.h' that indicates the desired behavior.

extern jmp_buf reset_context;
// -----


#define NUM_COMMANDS 52
typedef struct								// command struct
{
	char* command;
	char* shortcut;
	int (*func)(int, char**);
	char* description;
} Command;

typedef struct
{
    bool isTask;
    bool isValid;
    int argc;
    char** argv;
} Statement;

// ***********************************************************************
// project 1 variables
//
extern long swapCount;					// number of scheduler cycles
extern char inBuffer[];					// character input buffer
extern Semaphore* inBufferReady;		// input buffer ready semaphore
extern bool diskMounted;				// disk has been mounted
extern char dirPath[];					// directory path
Command** commands;						// shell commands


// ***********************************************************************
// project 1 prototypes
Statement* newStatement();
void deleteStatement(Statement* st);
Command** P1_init(void);
Command* newCommand(char*, char*, int (*func)(int, char**), char*);
CommandHistory *cmdHistory;

void mySigIntHandler()
{
    sigSignal(-1, mySIGTERM);
//	printf("Hellomynameisinigomontoyayoukilledmyfatherpreparetodie");
}

// ***********************************************************************
// myShell - command line interpreter
//
// Project 1 - implement a Shell (CLI) that:
//
// 1. Prompts the user for a command line.
// 2. WAIT's until a user line has been entered.
// 3. Parses the global char array inBuffer.
// 4. Creates new argc, argv variables using malloc.
// 5. Searches a command list for valid OS commands.
// 6. If found, perform a function variable call passing argc/argv variables.
// 7. Supports background execution of non-intrinsic commands.
//
int P1_shellTask(int argc, char* argv[])
{
	int i, j, found;					// # of arguments
	char** newArgv;							// pointers to arguments
    cmdHistory = malloc(sizeof(CommandHistory));
    cmdHistory->history = calloc(MAX_HISTORY, sizeof(char*));
    cmdHistory->head = -1;
    cmdHistory->tail = -1;
    cmdHistory->current = -1;
    cmdHistory->size = 0;
    cmdHistory->active = FALSE;

	// initialize shell commands
	commands = P1_init();					// init shell commands

	sigAction(mySigIntHandler, mySIGINT);

	while (1)
	{
		// output prompt
		if (diskMounted) printf("\n%s>>", dirPath);
		else printf("\n%ld>>", swapCount);

		SEM_WAIT(inBufferReady);			// wait for input buffer semaphore
        H_OFF;
		if (!inBuffer[0]) continue;		// ignore blank lines
		// printf("%s", inBuffer);

        addToHistory(inBuffer);

		SWAP										// do context switch

//        char** newArgv = malloc(MAX_ARGS * sizeof(char*));
        int stCount = 0;
        Statement** statements = malloc(MAX_ARGS * sizeof(Statement*));
        Statement* st = newStatement();
        statements[stCount++] = st;
		{
			// ?? >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
			// ?? parse command line into argc, argv[] variables
			// ?? must use malloc for argv storage!
			static char *sp;

			// init arguments
            printf("\n");
			sp = inBuffer;				// point to input string

			// parse input string
            int max_len = strlen(sp);
            j = 0;
            bool inQuote = FALSE;
            bool newArg = TRUE;
            bool ignore = FALSE;
            st->argv[st->argc] = calloc(max_len, sizeof(char));
            for (i=0; i<max_len; ++i) { // for all characters in the input string
                char c = sp[i];
                bool place = FALSE;
                if (!ignore || c == '&' || c == '|') {
                    if (c != '"' && inQuote)
                        place = TRUE;
                    else {
                        c = tolower(c);
                        switch (c) {
                            case '"':
                                inQuote = (inQuote + 1) % 2; // FALSE=>TRUE or TRUE=>FALSE
                                break;
                            case ' ':
                                // if there is something in the argument, we care about spaces
                                if (j != 0) {
                                    ++(st->argc);
                                    j = 0;
                                    newArg = TRUE;
                                }
                                break;
                            case '&':
                                // set task true and increment argument #
                                st->isTask = TRUE;
                            case '|':
                                ignore = FALSE;
                                ++(st->argc);

                                // create new statement
                                st = newStatement();
                                statements[stCount++] = st;

                                // start at first argument
                                j = 0;
                                newArg = TRUE;
                                break;
                            default:
                                place = TRUE;
                        }
                    }

                    // we can place the character in the argument
                    if (place) {
                        st->argv[st->argc][j] = c;
                        ++j;
                    }
                    else if (newArg) { // if we are allocating space for a new argument
                        if (st->argc >= MAX_ARGS) { // if we have too many arguments
                            printf("Invalid statement: too many arguments\n");
                            st->isValid = FALSE; // invalidate statement
                            ignore = TRUE; // ignore all args until new statement
                            inQuote = FALSE; // make sure this is unset, b/c we are ignoring everything
                        }
                        st->argv[st->argc] = calloc(max_len, sizeof(char));
                    }
                    newArg = FALSE;
                }
            }

            for (j = 0; j < stCount - 1; ++j) {
                Statement* stmt = statements[j];
                if (strlen(stmt->argv[stmt->argc - 1]) == 0)
                    free(stmt->argv[--(stmt->argc)]);
            }

            // Check if last statement is empty (happens on '&' at end of buffer)
            if (st->argc == 0 && strlen(st->argv[st->argc]) == 0) {
                deleteStatement(st); // Delete the empty statement
                statements[stCount--] = 0;
                st = statements[stCount];
            }
            else { // If it is not empty
                if (strlen(st->argv[st->argc]) == 0) // But the last argument is
                    free(st->argv[st->argc]);
                else
                    ++(st->argc); // Make sure argc is accurate
                if (inQuote) // Make sure quotations ended
                    st->isValid = FALSE;
            }

		}	// ?? >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

        // Print out arguments
//        for (j = 0; j < stCount; ++j) {
//            Statement* st = statements[j];
//            for (i=0; i<st->argc; ++i) {
//               printf("st: %d - args %d: %s\n", j, i, st->argv[i]);
//            }
//        }

		// look for command
        for (j = 0; j < stCount; ++j) {
            Statement* st = statements[j];
            if (st->isValid) {
                for (found = i = 0; i < NUM_COMMANDS; i++)
                {
//                    char *p;
//                    for (p = st->argv[0]; *p; ++p) *p = tolower(*p);

                    if (!strcmp(st->argv[0], commands[i]->command) ||
                         !strcmp(st->argv[0], commands[i]->shortcut))
                    {
                        // command found
                        int retValue = 0;
                        if (st->isTask)
                            createTask(st->argv[0], *commands[i]->func, HIGH_PRIORITY, st->argc, st->argv);
                        else {
                            retValue = (*commands[i]->func)(st->argc, st->argv);
                            if (retValue) printf("\nCommand Error %d", retValue);
                        }
                        found = TRUE;
                        break;
                    }
                }
            }
            if (!found || !st->isValid)	printf("\nInvalid command: %s", st->argv[0]);
        }

		// ?? free up any malloc'd argv parameters
        for (i = 0; i < stCount; ++i) {
            Statement* st = statements[i];
            deleteStatement(st);
        }
        free(statements);
            
        //free(newArgv);
		for (i=0; i<INBUF_SIZE; i++) inBuffer[i] = 0;
	}
	return 0;						// terminate task
} // end P1_shellTask


// ***********************************************************************
// ***********************************************************************
// P1 Project
//
int P1_project1(int argc, char* argv[])
{
	SWAP										// do context switch

	return 0;
} // end P1_project1



// ***********************************************************************
// ***********************************************************************
// history functions
//
int addToHistory(char* command) {
    if (cmdHistory->tail - cmdHistory->head >= 0 // if tail comes after head
            && cmdHistory->tail - cmdHistory->head < MAX_HISTORY - 1) { // and if we don't need to start rotating
        cmdHistory->history[++(cmdHistory->tail)] = calloc(strlen(command), sizeof(char));
        if (cmdHistory->head < 0)
            cmdHistory->head = 0;
    }
    else {
        cmdHistory->tail = (cmdHistory->tail + 1) % MAX_HISTORY;
        cmdHistory->head = (cmdHistory->head + 1) % MAX_HISTORY;

        // reallocate the memory in this location to new command size
        cmdHistory->history[cmdHistory->tail]
            = realloc(cmdHistory->history[cmdHistory->tail], strlen(command) * sizeof(char));

        // set the reallocated space to 0
        memset(cmdHistory->history[cmdHistory->tail], 0, strlen(command) * sizeof(char));
    }
    // copy command string into history
    strcpy(cmdHistory->history[cmdHistory->tail], command);
//    printf("\ntail: %d head: %d command: %s", cmdHistory->tail, cmdHistory->head, cmdHistory->history[cmdHistory->tail]);

    // set current to tail
    cmdHistory->current = cmdHistory->tail;
    return cmdHistory->current;
}

// Turns command history on
int historyOn() {
    cmdHistory->active = TRUE;
}

// Turns command history off and resets it
int historyOff() {
    cmdHistory->active = FALSE;
    cmdHistory->current = cmdHistory->tail;
}



// ***********************************************************************
// ***********************************************************************
// quit command
//
int P1_quit(int argc, char* argv[])
{
	int i;

	// free P1 commands
	for (i = 0; i < NUM_COMMANDS; i++)
	{
		free(commands[i]->command);
		free(commands[i]->shortcut);
		free(commands[i]->description);
	}
	free(commands);

    for (i = 0; i < MAX_HISTORY; ++i)
    {
        free(cmdHistory->history[i]);
    }
    free(cmdHistory->history);
    free(cmdHistory);

	// powerdown OS345
	longjmp(reset_context, POWER_DOWN_QUIT);
	return 0;
} // end P1_quit



// **************************************************************************
// **************************************************************************
// lc3 command
//
int P1_lc3(int argc, char* argv[])
{
	strcpy (argv[0], "0");
	return lc3Task(argc, argv);
} // end P1_lc3



// ***********************************************************************
// ***********************************************************************
// help command
//
int P1_help(int argc, char* argv[])
{
	int i;
    bool go = TRUE;
    if (argc > 1)
        go = FALSE;

	// list commands
	for (i = 0; i < NUM_COMMANDS; i++)
	{
		SWAP										// do context switch
        if (go ||
             !strcmp(argv[1], commands[i]->command) ||
             !strcmp(argv[1], commands[i]->shortcut))
        {
            if (strstr(commands[i]->description, ":")) printf("\n");
            printf("\n%4s: %s", commands[i]->shortcut, commands[i]->description);
        }
	}

	return 0;
} // end P1_help

// ***********************************************************************
// ***********************************************************************
// add command
//
int P1_add(int argc, char* argv[])
{
    int sum, i, a;
    char* format;
    sum = 0;
    printf("\n");
    for (i = 1; i < argc; ++i) {
        format = "%d";
        a = 0;
        char* str = argv[i];
        if (strlen(str) > 2) {
            if (str[0] == '0' && str[1] == 'x')
                format = "%x";
        }
        sscanf(str, format, &a);
        sum += a;
        printf("%d + ", a);
    }
    printf("\b\b= %d\n", sum);
    return 0;
}

// ***********************************************************************
// ***********************************************************************
// date/time command
//
int P1_date(int argc, char* argv[])
{
    time_t curtime;
    struct tm *info;
    (void)time(&curtime);
    info = localtime(&curtime);
    printf("Current local date/time is %s", asctime(info));

    return 0;
}

// ***********************************************************************
// ***********************************************************************
// args command
//
int P1_args(int argc, char* argv[])
{
    int i;
    for (i = 0; i < argc; ++i) {
        printf("\nargv[%d]: %s", i, argv[i]);
    }
    return 0;
}

// ***********************************************************************
// ***********************************************************************
// initialize statement with command/task arguments
//
Statement* newStatement() {
    Statement* statement = malloc(sizeof(Statement));

    statement->isTask = FALSE;

    statement->isValid = TRUE;

    statement->argc = 0;

    statement->argv = malloc(MAX_ARGS * sizeof(char*));

    return statement;
}

void deleteStatement(Statement* st) {
    int j;
    for (j = 0; j < st->argc; ++j) {
        free(st->argv[j]);
    }
    free(st->argv);
    free(st);
}

// ***********************************************************************
// ***********************************************************************
// initialize shell commands
//
Command* newCommand(char* command, char* shortcut, int (*func)(int, char**), char* description)
{
	Command* cmd = (Command*)malloc(sizeof(Command));

	// get long command
	cmd->command = (char*)malloc(strlen(command) + 1);
	strcpy(cmd->command, command);

	// get shortcut command
	cmd->shortcut = (char*)malloc(strlen(shortcut) + 1);
	strcpy(cmd->shortcut, shortcut);

	// get function pointer
	cmd->func = func;

	// get description
	cmd->description = (char*)malloc(strlen(description) + 1);
	strcpy(cmd->description, description);

	return cmd;
} // end newCommand


Command** P1_init()
{
	int i  = 0;
	Command** commands = (Command**)malloc(sizeof(Command*) * NUM_COMMANDS);

	// system
	commands[i++] = newCommand("quit", "q", P1_quit, "Quit");
	commands[i++] = newCommand("kill", "kt", P2_killTask, "Kill task");
	commands[i++] = newCommand("reset", "rs", P2_reset, "Reset system");

	// P1: Shell
	commands[i++] = newCommand("project1", "p1", P1_project1, "P1: Shell");
	commands[i++] = newCommand("help", "he", P1_help, "OS345 Help");
	commands[i++] = newCommand("lc3", "lc3", P1_lc3, "Execute LC3 program");
    commands[i++] = newCommand("add", "add", P1_add, "Add _10 or _16 numbers together");
    commands[i++] = newCommand("date/time", "dt", P1_date, "Output the current date and time");
    commands[i++] = newCommand("args", "ar", P1_args, "Display the input args");

	// P2: Tasking
	commands[i++] = newCommand("project2", "p2", P2_project2, "P2: Tasking");
	commands[i++] = newCommand("semaphores", "sem", P2_listSems, "List semaphores");
	commands[i++] = newCommand("tasks", "lt", P2_listTasks, "List tasks");
	commands[i++] = newCommand("signal1", "s1", P2_signal1, "Signal sem1 semaphore");
	commands[i++] = newCommand("signal2", "s2", P2_signal2, "Signal sem2 semaphore");

	// P3: Jurassic Park
	commands[i++] = newCommand("project3", "p3", P3_project3, "P3: Jurassic Park");
	commands[i++] = newCommand("deltaclock", "dc", P3_dc, "List deltaclock entries");

	// P4: Virtual Memory
	commands[i++] = newCommand("project4", "p4", P4_project4, "P4: Virtual Memory");
	commands[i++] = newCommand("frametable", "dft", P4_dumpFrameTable, "Dump bit frame table");
	commands[i++] = newCommand("initmemory", "im", P4_initMemory, "Initialize virtual memory");
	commands[i++] = newCommand("touch", "vma", P4_vmaccess, "Access LC-3 memory location");
	commands[i++] = newCommand("stats", "vms", P4_virtualMemStats, "Output virtual memory stats");
	commands[i++] = newCommand("crawler", "cra", P4_crawler, "Execute crawler.hex");
	commands[i++] = newCommand("memtest", "mem", P4_memtest, "Execute memtest.hex");

	commands[i++] = newCommand("frame", "dfm", P4_dumpFrame, "Dump LC-3 memory frame");
	commands[i++] = newCommand("memory", "dm", P4_dumpLC3Mem, "Dump LC-3 memory");
	commands[i++] = newCommand("page", "dp", P4_dumpPageMemory, "Dump swap page");
	commands[i++] = newCommand("virtual", "dvm", P4_dumpVirtualMem, "Dump virtual memory page");
	commands[i++] = newCommand("root", "rpt", P4_rootPageTable, "Display root page table");
	commands[i++] = newCommand("user", "upt", P4_userPageTable, "Display user page table");

	// P5: Scheduling
	commands[i++] = newCommand("project5", "p5", P5_project5, "P5: Scheduling");
//	commands[i++] = newCommand("stress1", "t1", P5_stress1, "ATM stress test1");
//	commands[i++] = newCommand("stress2", "t2", P5_stress2, "ATM stress test2");

	// P6: FAT
	commands[i++] = newCommand("project6", "p6", P6_project6, "P6: FAT");
	commands[i++] = newCommand("change", "cd", P6_cd, "Change directory");
	commands[i++] = newCommand("copy", "cf", P6_copy, "Copy file");
	commands[i++] = newCommand("define", "df", P6_define, "Define file");
	commands[i++] = newCommand("delete", "dl", P6_del, "Delete file");
	commands[i++] = newCommand("directory", "dir", P6_dir, "List current directory");
	commands[i++] = newCommand("mount", "md", P6_mount, "Mount disk");
	commands[i++] = newCommand("mkdir", "mk", P6_mkdir, "Create directory");
	commands[i++] = newCommand("run", "run", P6_run, "Execute LC-3 program");
	commands[i++] = newCommand("space", "sp", P6_space, "Space on disk");
	commands[i++] = newCommand("type", "ty", P6_type, "Type file");
	commands[i++] = newCommand("unmount", "um", P6_unmount, "Unmount disk");

	commands[i++] = newCommand("fat", "ft", P6_dfat, "Display fat table");
	commands[i++] = newCommand("fileslots", "fs", P6_fileSlots, "Display current open slots");
	commands[i++] = newCommand("sector", "ds", P6_dumpSector, "Display disk sector");
	commands[i++] = newCommand("chkdsk", "ck", P6_chkdsk, "Check disk");
	commands[i++] = newCommand("final", "ft", P6_finalTest, "Execute file test");

	commands[i++] = newCommand("open", "op", P6_open, "Open file test");
	commands[i++] = newCommand("read", "rd", P6_read, "Read file test");
	commands[i++] = newCommand("write", "wr", P6_write, "Write file test");
	commands[i++] = newCommand("seek", "sk", P6_seek, "Seek file test");
	commands[i++] = newCommand("close", "cl", P6_close, "Close file test");

	assert(i == NUM_COMMANDS);

	return commands;

} // end P1_init
