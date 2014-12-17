// os345fat.c - file management system
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
//
//		11/19/2011	moved getNextDirEntry to P6
//
// ***********************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include "os345.h"
#include "os345fat.h"

// ***********************************************************************
// ***********************************************************************
//	functions to implement in Project 6
//
int fmsCloseFile(int);
int fmsDefineFile(char*, int);
int fmsDeleteFile(char*);
int fmsOpenFile(char*, int);
int fmsReadFile(int, char*, int);
int fmsSeekFile(int, int);
int fmsWriteFile(int, char*, int);

// ***********************************************************************
// ***********************************************************************
//  helper functions
//
int validEntry(short);
void setFatEntries(int, unsigned short);
int getEmptyCluster();
int clearChain(unsigned short);
int convertToUpper(char*);

// ***********************************************************************
// ***********************************************************************
//	Support functions available in os345p6.c
//
extern int fmsGetDirEntry(char* fileName, DirEntry* dirEntry);
extern int fmsGetNextDirEntry(int *dirNum, char* mask, DirEntry* dirEntry, int dir);

extern int fmsMount(char* fileName, void* ramDisk);

extern void setFatEntry(int FATindex, unsigned short FAT12ClusEntryVal, unsigned char* FAT);
extern unsigned short getFatEntry(int FATindex, unsigned char* FATtable);

extern int fmsMask(char* mask, char* name, char* ext);
extern void setDirTimeDate(DirEntry* dir);
extern int isValidFileName(char* fileName);
extern void printDirectoryEntry(DirEntry*);
extern void fmsError(int);

extern int fmsReadSector(void* buffer, int sectorNumber);
extern int fmsWriteSector(void* buffer, int sectorNumber);

// ***********************************************************************
// ***********************************************************************
// fms variables
//
// RAM disk
unsigned char RAMDisk[SECTORS_PER_DISK * BYTES_PER_SECTOR];

// File Allocation Tables (FAT1 & FAT2)
unsigned char FAT1[NUM_FAT_SECTORS * BYTES_PER_SECTOR];
unsigned char FAT2[NUM_FAT_SECTORS * BYTES_PER_SECTOR];

char dirPath[128];							// current directory path
FDEntry OFTable[NFILES];					// open file table

extern bool diskMounted;					// disk has been mounted
extern TCB tcb[];							// task control block
extern int curTask;							// current task #


// ***********************************************************************
// ***********************************************************************
// This function closes the open file specified by fileDescriptor.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
//	Return 0 for success, otherwise, return the error number.
//
int fmsCloseFile(int fileDescriptor)
{
    int error, i, bufferIndex;
    int dirNum = 0;
    int entrySize = sizeof(DirEntry);
    FDEntry* fdEntry = &OFTable[fileDescriptor];
	char buffer[BYTES_PER_SECTOR];
    unsigned short dirCluster, dirSector;

    if (fdEntry->name[0] == 0) return ERR63;
    if (fdEntry->mode != OPEN_READ)
    {
        //printf("\nBuffer altered = %s", fdEntry->flags & BUFFER_ALTERED ? "True" : "False");
        if (fdEntry->flags & BUFFER_ALTERED)
        {
            //printf("\nWriting %s for %d to cluster %d", fdEntry->buffer, fileDescriptor, fdEntry->currentCluster);
            if((error = fmsWriteSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
            fdEntry->flags &= ~BUFFER_ALTERED;
        }

        dirCluster = fdEntry->directoryCluster;
        DirEntry dirEntry;
        while (dirCluster != FAT_EOC)
        {
            if (dirCluster)
            {
                dirSector = C_2_S(dirCluster);
            }
            else
            {	// root directory
                dirSector = (dirNum / ENTRIES_PER_SECTOR) + BEG_ROOT_SECTOR;
                if (dirSector >= BEG_DATA_SECTOR) return ERR67;
            }
            if ((error = fmsReadSector(&buffer, dirSector)) < 0) return error;
            for (bufferIndex = 0; bufferIndex < BYTES_PER_SECTOR; bufferIndex += entrySize)
            {
                memcpy(&dirEntry, &buffer[bufferIndex], entrySize);
                if (dirEntry.name[0] == 0xe5) continue;
                if (!strncmp(fdEntry->name, dirEntry.name, 8))
                {
                    if (!strncmp(fdEntry->extension, dirEntry.extension, 3))
                    {
                        dirEntry.fileSize = fdEntry->fileSize;
                        dirEntry.startCluster = fdEntry->startCluster;
                        dirEntry.attributes = fdEntry->attributes;
                        setDirTimeDate(&dirEntry);
                        memcpy(&buffer[bufferIndex], &dirEntry, entrySize);
                        if ((error = fmsWriteSector(&buffer, dirSector)) < 0) return error;
                        fdEntry->name[0] = 0;
                        return 0;
                    }
                }
                ++dirNum;
            }
            if (dirCluster)
            {
                dirCluster = getFatEntry(dirCluster, FAT1);
            }
        }
    }

    fdEntry->name[0] = 0;
    return 0;
} // end fmsCloseFile



// ***********************************************************************
// ***********************************************************************
// If attribute=DIRECTORY, this function creates a new directory
// file directoryName in the current directory.
// The directory entries "." and ".." are also defined.
// It is an error to try and create a directory that already exists.
//
// else, this function creates a new file fileName in the current directory.
// It is an error to try and create a file that already exists.
// The start cluster field should be initialized to cluster 0.  In FAT-12,
// files of size 0 should point to cluster 0 (otherwise chkdsk should report an error).
// Remember to change the start cluster field from 0 to a free cluster when writing to the
// file.
//
// Return 0 for success, otherwise, return the error number.
//
int fmsDefineFile(char* fileName, int attribute)
{
    // Find empty dir entry in CDIR
    // If no empty dir entry
    //      If in root, error
    //      Otherwise, add new sector to CDIR
    //      Find empty dir entry in CDIR
    // Set dir entry name
    // If file
    //      Set start cluster to 0
    // Otherwise if directory
    //      Set start cluster to new empty cluster
    //      Read new cluster into buffer
    //      Add '.' entry -> to start cluster
    //      Add '..' entry -> to CDIR
    int error, i, bufferIndex;
    int dirNum = 0;
    bool found = FALSE;
    int entrySize = sizeof(DirEntry);
	char buffer[BYTES_PER_SECTOR];
    unsigned short dirCluster, dirSector;

    convertToUpper(fileName);

    dirCluster = CDIR;
    DirEntry dirEntry;
    DirEntry emptyEntry;
    while (!found)
    {
        if (dirCluster)
        {
            dirSector = C_2_S(dirCluster);
        }
        else
        {	// root directory
            dirSector = (dirNum / ENTRIES_PER_SECTOR) + BEG_ROOT_SECTOR;
            //printf("\n%d %d", dirSector, BEG_DATA_SECTOR);
            if (dirSector >= BEG_DATA_SECTOR) return ERR67;
        }
        if ((error = fmsReadSector(&buffer, dirSector)) < 0) return error;
        for (bufferIndex = 0; bufferIndex < BYTES_PER_SECTOR; bufferIndex += entrySize)
        {
            memcpy(&dirEntry, &buffer[bufferIndex], entrySize);
            if (dirEntry.name[0] == 0xe5 || dirEntry.name[0] == 0)
            {
                emptyEntry = dirEntry;
                found = TRUE;
                break;
            }
            ++dirNum;
        }
        if (dirCluster)
        {
            int nextCluster, emptyCluster;
            nextCluster = getFatEntry(dirCluster, FAT1);
            if (nextCluster == FAT_EOC)
            {
                if ((emptyCluster = getEmptyCluster()) < 0) return emptyCluster;
                if ((error = validEntry(emptyCluster)) < 0) return error;
                setFatEntries(dirCluster, emptyCluster);
                setFatEntries(emptyCluster, FAT_EOC);
                nextCluster = emptyCluster;
            }
            dirCluster = nextCluster;
        }
    }

    char delim[1] = ".";
    int nameLength = strcspn(fileName, delim);
    if (nameLength > 8) return ERR50;

    // Get file name
    char* filename = strtok(fileName, delim);
    if (filename == NULL || strlen(filename) > 8) return ERR50;
    // Get file extension
    char* extension = strtok(NULL, delim);
    if (extension != NULL && strlen(extension) > 3) return ERR50;
    // See if file continues (invalid)
    char* token = strtok(NULL, delim);
    if (token != NULL) return ERR50;

    setDirTimeDate(&emptyEntry);
    sprintf(emptyEntry.name, "%-8s", filename);
    if (attribute & ARCHIVE)
    {
        // Files must have extensions
        if (extension == NULL) return ERR50;
        sprintf(emptyEntry.extension, "%-3s", extension);
        emptyEntry.startCluster = 0;
        emptyEntry.fileSize = 0;
        emptyEntry.attributes = ARCHIVE;
    }
    else if (attribute & DIRECTORY)
    {
        // Directories cannot have extensions
        if (extension != NULL) return ERR50;
        strcpy(emptyEntry.extension, "   ");
        int cluster, sector;
        if ((cluster = getEmptyCluster()) < 0) return cluster;
        if ((error = validEntry(cluster)) < 0) return error;
        emptyEntry.startCluster = cluster;
        setFatEntries(cluster, FAT_EOC);
        emptyEntry.attributes = DIRECTORY;

        // Read out new sector
        sector = C_2_S(cluster);
        char dirBuffer[BYTES_PER_SECTOR];
        fmsReadSector(&dirBuffer, sector);

        // Set "." entry
        DirEntry startEntry;
        memcpy(&startEntry, &dirBuffer[0], entrySize);
        sprintf(startEntry.name, "%-8s", ".");
        strcpy(startEntry.extension, "   ");
        startEntry.attributes = DIRECTORY;
        startEntry.startCluster = cluster;
        memcpy(&dirBuffer[0], &startEntry, entrySize);

        // Set ".." entry
        memcpy(&startEntry, &dirBuffer[entrySize], entrySize);
        sprintf(startEntry.name, "%-8s", "..");
        strcpy(startEntry.extension, "   ");
        startEntry.attributes = DIRECTORY;
        startEntry.startCluster = CDIR;
        memcpy(&dirBuffer[entrySize], &startEntry, entrySize);

        // Write in new sector entries
        fmsWriteSector(&dirBuffer, sector);
    }
    else return ERR51;

    memcpy(&buffer[bufferIndex], &emptyEntry, entrySize);
    fmsWriteSector(&buffer, dirSector);

    return 0;
} // end fmsDefineFile



// ***********************************************************************
// ***********************************************************************
// This function deletes the file fileName from the current directory.
// The file name should be marked with an "E5" as the first character and the chained
// clusters in FAT 1 reallocated (cleared to 0).
// Return 0 for success; otherwise, return the error number.
//
int fmsDeleteFile(char* fileName)
{
    int error, i, bufferIndex;
    int dirNum = 0;
    int entrySize = sizeof(DirEntry);
	char buffer[BYTES_PER_SECTOR];
    unsigned short dirCluster, dirSector;

    dirCluster = CDIR;
    DirEntry dirEntry;
    while (dirCluster != FAT_EOC)
    {
        if (dirCluster)
        {
            dirSector = C_2_S(dirCluster);
        }
        else
        {	// root directory
            dirSector = (dirNum / ENTRIES_PER_SECTOR) + BEG_ROOT_SECTOR;
            if (dirSector >= BEG_DATA_SECTOR) return 0;
        }
        if ((error = fmsReadSector(&buffer, dirSector)) < 0) return error;
        for (bufferIndex = 0; bufferIndex < BYTES_PER_SECTOR; bufferIndex += entrySize)
        {
            memcpy(&dirEntry, &buffer[bufferIndex], entrySize);
            ++dirNum;
            if (dirEntry.name[0] == 0xe5) continue;
            //if (dirEntry.attributes & DIRECTORY) continue;
            if (fmsMask(fileName, dirEntry.name, dirEntry.extension))
            {
                dirEntry.name[0] = 0xe5;
                memcpy(&buffer[bufferIndex], &dirEntry, entrySize);
                clearChain(dirEntry.startCluster);
                if ((error = fmsWriteSector(&buffer, dirSector)) < 0) return error;
            }
        }
        if (dirCluster)
        {
            dirCluster = getFatEntry(dirCluster, FAT1);
        }
    }
    return 0;

} // end fmsDeleteFile



// ***********************************************************************
// ***********************************************************************
// This function opens the file fileName for access as specified by rwMode.
// It is an error to try to open a file that does not exist.
// The open mode rwMode is defined as follows:
//    0 - Read access only.
//       The file pointer is initialized to the beginning of the file.
//       Writing to this file is not allowed.
//    1 - Write access only.
//       The file pointer is initialized to the beginning of the file.
//       Reading from this file is not allowed.
//    2 - Append access.
//       The file pointer is moved to the end of the file.
//       Reading from this file is not allowed.
//    3 - Read/Write access.
//       The file pointer is initialized to the beginning of the file.
//       Both read and writing to the file is allowed.
// A maximum of 32 files may be open at any one time.
// If successful, return a file descriptor that is used in calling subsequent file
// handling functions; otherwise, return the error number.
//
int fmsOpenFile(char* fileName, int rwMode)
{
    int error;
    DirEntry dirEntry;

    if ((error = fmsGetDirEntry(fileName, &dirEntry)) < 0) return error;
    printDirectoryEntry(&dirEntry);
    if (dirEntry.attributes & DIRECTORY) return ERR50;

    int i, descriptor = -1;
    for (i = 0; i < NFILES; ++i)
    {
        FDEntry* fd = &OFTable[i];
        if (fd->name[0] != 0)
        {
            if (!strncmp(fd->name, dirEntry.name, 8))
            {
                if (!strncmp(fd->extension, dirEntry.extension, 3))
                {
                    return ERR62;
                }
            }
        }
        else if (descriptor < 0)
        {
            descriptor = i;
        }
    }

    if (descriptor < 0) return ERR70;

    FDEntry* fdEntry = &OFTable[descriptor];

    strcpy(fdEntry->name, dirEntry.name);
    strcpy(fdEntry->extension, dirEntry.extension);
    fdEntry->attributes = dirEntry.attributes;
    fdEntry->directoryCluster = CDIR;
    fdEntry->startCluster = dirEntry.startCluster;
    fdEntry->currentCluster = dirEntry.startCluster;
    fdEntry->fileSize = dirEntry.fileSize;
    fdEntry->pid = curTask;
    fdEntry->mode = rwMode;
    fdEntry->flags = 0x00;
    fdEntry->fileIndex = 0;
//    if((error = fmsReadSector(fdEntry->buffer, C_2_S(fdEntry->startCluster)))) return error;

    short entryCode = fdEntry->startCluster;
    while (entryCode && entryCode != FAT_EOC)
    {
        short oldCode = entryCode;
        entryCode = getFatEntry(entryCode, FAT1);
        if ((error = validEntry(entryCode)) < 0) return error;
        if (rwMode == OPEN_WRITE) setFatEntries(oldCode, 0); // unset fat entries for file write
        if (rwMode == OPEN_APPEND && entryCode != FAT_EOC) fdEntry->currentCluster = entryCode; // move current cluster for append
    }
    if (rwMode == OPEN_WRITE) setFatEntries(fdEntry->startCluster, FAT_EOC); // mark next entry as EOC
    if (rwMode == OPEN_APPEND) fdEntry->fileIndex = fdEntry->fileSize; // set file index to end
    // TODO what happens if file size is multiple of BYTES_PER_SECTOR?

    return descriptor;
} // end fmsOpenFile



// ***********************************************************************
// ***********************************************************************
// This function reads nBytes bytes from the open file specified by fileDescriptor into
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// After each read, the file pointer is advanced.
// Return the number of bytes successfully read (if > 0) or return an error number.
// (If you are already at the end of the file, return EOF error.  ie. you should never
// return a 0.)
//
int fmsReadFile(int fileDescriptor, char* buffer, int nBytes)
{
    int error, nextCluster;
    FDEntry* fdEntry;
    int numBytesRead = 0;
    unsigned int bytesLeft, bufferIndex;
//    memset(buffer, 0, strlen(buffer));
    fdEntry = &OFTable[fileDescriptor];
    //printf("\n  File size - %d, Index - %d", fdEntry->fileSize, fdEntry->fileIndex);
    if(fdEntry->name[0] == 0) return ERR63;
    if((fdEntry->mode == OPEN_WRITE) || (fdEntry->mode == OPEN_APPEND)) return ERR85;
    while(nBytes > 0)
    {
        if(fdEntry->fileSize == fdEntry->fileIndex) return(numBytesRead ? numBytesRead: ERR66);
        bufferIndex = fdEntry->fileIndex % BYTES_PER_SECTOR;
        if((bufferIndex == 0) && (fdEntry->fileIndex || !fdEntry->currentCluster))
        {
            if(fdEntry->currentCluster == 0)
            {
                if(fdEntry->startCluster == 0) return ERR66;
                nextCluster = fdEntry->startCluster;
                fdEntry->fileIndex = 0;
            }
            else
            {
                nextCluster = getFatEntry(fdEntry->currentCluster, FAT1);
                if(nextCluster == FAT_EOC) return numBytesRead;
            }
            if(fdEntry->flags & BUFFER_ALTERED)
            {
                if((error = fmsWriteSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
                fdEntry->flags &= ~BUFFER_ALTERED;
            }
            fdEntry->currentCluster = nextCluster;
        }
        if((error = fmsReadSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
        bytesLeft = BYTES_PER_SECTOR - bufferIndex;
        if(bytesLeft > nBytes) bytesLeft = nBytes;
        if(bytesLeft > (fdEntry->fileSize - fdEntry->fileIndex))
        {
            bytesLeft = fdEntry->fileSize - fdEntry->fileIndex;
        }
        memcpy(buffer, &fdEntry->buffer[bufferIndex], bytesLeft);
        fdEntry->flags |= BUFFER_ALTERED;

        fdEntry->fileIndex += bytesLeft;
        numBytesRead += bytesLeft;
        buffer += bytesLeft;
        nBytes -= bytesLeft;
    }
    //printf("\nRead %i bytes", numBytesRead);
    return numBytesRead;
} // end fmsReadFile



// ***********************************************************************
// ***********************************************************************
// This function changes the current file pointer of the open file specified by
// fileDescriptor to the new file position specified by index.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// The file position may not be positioned beyond the end of the file.
// Return the new position in the file if successful; otherwise, return the error number.
//
int fmsSeekFile(int fileDescriptor, int index)
{
    int error, i, levels;
    unsigned short code;
    FDEntry* fdEntry;
    fdEntry = &OFTable[fileDescriptor];
    if(fdEntry->name[0] == 0) return ERR63;
    if(fdEntry->mode == OPEN_WRITE || fdEntry->mode == OPEN_APPEND) return ERR85;
    if(fdEntry->flags & BUFFER_ALTERED)
    {
        //printf("\nWriting file buffer to sector - %d, %s", C_2_S(fdEntry->currentCluster), fdEntry->buffer);
        if((error = fmsWriteSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
        fdEntry->flags &= ~BUFFER_ALTERED;
    }
    fdEntry->fileIndex = 0;
    fdEntry->currentCluster = fdEntry->startCluster;

//    printf("\nIndex - %d, fileSize - %d", index, fdEntry->fileSize);
    //printf("\nStart at entry %d", fdEntry->currentCluster);
    if (index > fdEntry->fileSize) return ERR80;
    levels = index / BYTES_PER_SECTOR;
    if (index % BYTES_PER_SECTOR == 0) --levels;
    for (i = 0; i < levels; ++i)
    {
        code = getFatEntry(fdEntry->currentCluster, FAT1);
        if ((error = validEntry(code)) < 0) return error;
        fdEntry->currentCluster = code;
    }
    //printf("\nSeeked to entry %d", fdEntry->currentCluster);
    fdEntry->fileIndex = index;
    return fdEntry->fileIndex; 
} // end fmsSeekFile



// ***********************************************************************
// ***********************************************************************
// This function writes nBytes bytes to the open file specified by fileDescriptor from
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// Writing is always "overwriting" not "inserting" in the file and always writes forward
// from the current file pointer position.
// Return the number of bytes successfully written; otherwise, return the error number.
//
int fmsWriteFile(int fileDescriptor, char* buffer, int nBytes)
{
    int error, cluster, nextCluster;
    FDEntry* fdEntry;
    int numBytesWritten = 0;
    unsigned int bytesLeft, bufferIndex;
    fdEntry = &OFTable[fileDescriptor];
    if(fdEntry->name[0] == 0) return ERR63;
    if(fdEntry->mode == OPEN_READ) return ERR85;
//    printf("\nMode - %d, Index - %d", fdEntry->mode, fdEntry->fileIndex);
    while (nBytes > 0)
    {
        bufferIndex = fdEntry->fileIndex % BYTES_PER_SECTOR;
        if((bufferIndex == 0) && (fdEntry->fileIndex || !fdEntry->currentCluster))
        {
            if(fdEntry->currentCluster == 0)
            {
                if(fdEntry->startCluster == 0)
                {
                    if ((cluster = getEmptyCluster()) < 0) return cluster;
                    fdEntry->startCluster = cluster;
                    //printf("\nAssigning cluster %d to new file %d", cluster, fileDescriptor);
                    setFatEntries(fdEntry->startCluster, FAT_EOC);
                }
                nextCluster = fdEntry->startCluster;
                fdEntry->fileIndex = 0;
            }
            else
            {
                nextCluster = getFatEntry(fdEntry->currentCluster, FAT1);
                if(nextCluster == FAT_EOC)
                {
                    if ((cluster = getEmptyCluster()) < 0) return cluster;
                    nextCluster = cluster;
                    //printf("\nAssigning cluster %d to end of file %d", cluster, fileDescriptor);
                    setFatEntries(fdEntry->currentCluster, nextCluster);
                    setFatEntries(nextCluster, FAT_EOC);
                }
            }
            if(fdEntry->flags & BUFFER_ALTERED)
            {
                //printf("\nWriting file buffer to sector - %d, %s", C_2_S(fdEntry->currentCluster), fdEntry->buffer);
                if((error = fmsWriteSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
                fdEntry->flags &= ~BUFFER_ALTERED;
            }
            fdEntry->currentCluster = nextCluster;
        }
        if((error = fmsReadSector(fdEntry->buffer, C_2_S(fdEntry->currentCluster)))) return error;
//        printf("\nBuffer - %s", fdEntry->buffer);
        bytesLeft = BYTES_PER_SECTOR - bufferIndex;
        if(nBytes < bytesLeft) bytesLeft = nBytes;
        //printf("\nWriting %d bytes of %s to %d of %s for %d", bytesLeft, buffer, bufferIndex, fdEntry->buffer, fileDescriptor);

        memcpy(&fdEntry->buffer[bufferIndex], buffer, bytesLeft);
        //printf("\nPostBuffer - %s", fdEntry->buffer);
        fdEntry->fileIndex += bytesLeft;
        numBytesWritten += bytesLeft;
        buffer += bytesLeft;
        nBytes -= bytesLeft;
        fdEntry->flags |= BUFFER_ALTERED;
        //printf("\nBuffer altered = %s", fdEntry->flags & BUFFER_ALTERED ? "True" : "False");

        if (fdEntry->mode == OPEN_WRITE || fdEntry->fileIndex > fdEntry->fileSize)
        {
            fdEntry->fileSize = fdEntry->fileIndex;
        }
    }

    //printf("\nPostBuffer - %s", fdEntry->buffer);
    return numBytesWritten;
} // end fmsWriteFile


int validEntry(short entryCode)
{
    if (entryCode == FAT_BAD) return ERR54;
    if (entryCode < 2) return ERR54;
    return 0;
}


void setFatEntries(int FATindex, unsigned short FAT12ClusEntryVal)
{
    setFatEntry(FATindex, FAT12ClusEntryVal, FAT1);
    setFatEntry(FATindex, FAT12ClusEntryVal, FAT2);
    return;
}


int getEmptyCluster()
{
    int i, error, sector;
    char buffer[BYTES_PER_SECTOR];
	for (i = 2; i < CLUSTERS_PER_DISK; i++)
    {
        if (!getFatEntry(i, FAT1)) // If FAT entry is 0x000, we can use it
        {
            sector = C_2_S(i);
            memset(buffer, 0, BYTES_PER_SECTOR * sizeof(char));
            if ((error = fmsWriteSector(buffer, sector)) < 0) return error;
            return i;
        }
    }
    return ERR65; // No empty entries found, so file space is full
}


int clearChain(unsigned short entryStart)
{
    int error;
    unsigned short entry = entryStart;
    while (entry != FAT_EOC)
    {
        unsigned short next = getFatEntry(entry, FAT1);
        setFatEntries(entry, 0);
        if ((error = validEntry(next)) < 0) return error;
        entry = next;
    }
    return 0;
}


int convertToUpper(char* s)
{
    int i;
    for (i = 0; i < strlen(s); ++i)
    {
        s[i] = toupper(s[i]);
    }
    return i;
}
