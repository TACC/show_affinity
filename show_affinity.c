/*************************************************************************
--------------------------------------------------------------------------
--  show_affinity License
--------------------------------------------------------------------------
--
--  show_affinity is licensed under the terms of the MIT license reproduced
--  below. This means that show_affinity is free software and can be used 
--  for both academic and commercial purposes at absolutely no cost.
--
--  ----------------------------------------------------------------------
--
--  Copyright (C) 2017-2019 Lei Huang (huang@tacc.utexas.edu)
--
--  Permission is hereby granted, free of charge, to any person obtaining
--  a copy of this software and associated documentation files (the
--  "Software"), to deal in the Software without restriction, including
--  without limitation the rights to use, copy, modify, merge, publish,
--  distribute, sublicense, and/or sell copies of the Software, and to
--  permit persons to whom the Software is furnished to do so, subject
--  to the following conditions:
--
--  The above copyright notice and this permission notice shall be
--  included in all copies or substantial portions of the Software.
--
--  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
--  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
--  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
--  NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
--  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
--  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
--  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
--  THE SOFTWARE.
--
--------------------------------------------------------------------------
*************************************************************************/

/*
About: This is the source code of show_affinity. It is a tool to query and print 
the binding affinity of the processes of current user on current node. It helps 
users to check whether processes/threads are bound on cores correctly. If not, 
one might observe performance issue due to improper binding. 

To compile,
gcc -Wall -o show_affinity show_affinity.c

To run the command, 
./show_affinity [all]

Without any parameter, show_affinity will only show the results of the processes/
threads of current user that keep cpu busy. With "all" as a parameter, show_affinity 
show the results of all processes/threads of current user.

You can run show_affinity with watch if you want to constantly monitor the binding 
affinities of running processes. 

Example: watch -n 2 show_affinity all

*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define __USE_GNU
#include <sched.h>
#include <errno.h>

#define MAX_CORE			(2048)	// The maximum number of cores on current computer.
#define LEN_AFFINITY_BUFF	(MAX_CORE*64)	// The maximum length of the buffer to hold affinity information
#define BUFF_SIZE	(MAX_CORE * 256)	// 256 bytes should be more than enough for one line record. BUFF_SIZE defines the size of the buffer for thread binding affinity info 
#define SIZE_STAT			(256)	// The number of bytes we read from "/proc/%tid/stat"

void Enumerate_All_Processes(void);	// Enumerate_All_Processes() exhaustively enumerates all processes
int Is_Thread_Running(const char szName[]);	// Is_Thread_Running() queries whether a thread is running or not with file "/proc/%tid/stat". 
int Get_Offset_of_Next_Line(const char szBuff[], const int iPosStart, const int nBuffSize);	// Get_Offset_of_Next_Line() determines where the next new line starts in a given buffer. 
int Determine_Number_of_Cores(void);	// Determine_Number_of_Cores() determines how many logical cores on current computer from file /proc/stat. 
void Query_Task_CPUSet(const int tid, int *bMainThread, const char szMsg[]);	// Query_Task_CPUSet() queries and prints the binding affinity for a given thread. 
void Extract_Exec_Name(const int pid, char szExeName[], const int nMaxStrLen);	// Extract_Exec_Name() extracts the executable file name for a given process from "/proc/%pid/stat". 
char *cpulist_create(char *str, const size_t len, const cpu_set_t *set, const size_t setsize);	// cpulist_create() generates a human readable string from an array of bits of cpu binding affinity. 

// Global variables in this file. Since this code is simple enough, it does not hurt to define these four global variables. 
static int my_uid; // my_uid - uid of current user. 
static int my_pid;	// my_pid - the process id of show_affinity itself. 
static int nCores = 0;	// The total number of logical cores on the node 
static int bShow_All = 0; // A flag for showing the results for all running processes/threads or only those 
//char szHostName[256]; // the name of current computer

int main(int argc, char *argv[])	{
	bShow_All = 0;

	if (argc == 2)	{
		if ( ( strncmp( argv[1], "all", 4) == 0 ) || (( strncmp(argv[1], "ALL", 4) == 0 )) )	{
			bShow_All = 1;
		}
	}

	my_pid = getpid();
	my_uid = getuid();
//	gethostname(szHostName, 255);

	nCores = Determine_Number_of_Cores();	// Get the number of logical cores. 
	Enumerate_All_Processes();				// Enumerate allprocesses and print binding affinities. 

	return 0;
}

/*
Enumerate_All_Processes() exhaustively enumerates all processes.
*/
void Enumerate_All_Processes(void)	{
	DIR *pDirRoot, *pDirProc;
	struct dirent *pProcEntry, *pThreadEntry;
	char szPath[512], szPathChild[512], szExeName[512], szMsg[576];
	struct stat file_stat;
	int pid, tid, thread_count;
	int IsThreadRunning, bMainThread;

	printf("pid     Exe_Name             tid     Affinity\n");
	
	pDirRoot = opendir("/proc");	// the root dir of all running processes
	if ( pDirRoot != NULL )	{
		while ( 1 )	{
			pProcEntry = readdir( pDirRoot );
			if ( pProcEntry == NULL )	{
				break;
			}
			
			snprintf( szPath, sizeof(szPath), "/proc/%s", pProcEntry->d_name );	// enumerates all processes under /proc
			if ( (pProcEntry->d_name[0] < '0') || (pProcEntry->d_name[0] > '9') )	continue;	// not starting with a number
			
			pid = atoi( pProcEntry->d_name );
			
			if ( stat( szPath, &file_stat ) == -1 )	continue;	// error to query the stat of directory szPath
			if ( pid == my_pid )	continue;	// skip checking show_affinity itself
			
			if ( file_stat.st_uid == my_uid )	{	// build the list of the jobs of current user
				thread_count = 0;
				bMainThread = 1;	// tid = pid for the first thread (main thread) in a process
				snprintf( szPath, sizeof(szPath), "/proc/%d/task", pid );	// enumerates all threads under current process
				pDirProc = opendir( szPath );
				if ( pDirProc != NULL )	{
					while ( 1 )	{
						pThreadEntry = readdir( pDirProc );
						if ( pThreadEntry == NULL )	{
							break;
						}
						if ( (pThreadEntry->d_name[0] < '0') || (pThreadEntry->d_name[0] > '9') )	continue;	// not starting with a number

						if ( thread_count == 0 )	{	// Need to query executable file name for the main thread of each process
							Extract_Exec_Name( pid, szExeName, sizeof(szExeName) );
							if (szExeName[0] == 0 )	{	// not a valid name
								continue;
							}

							snprintf( szMsg, sizeof(szMsg), "%-6d  %-15s     ", pid, szExeName );
						}						

						tid = atoi( pThreadEntry->d_name );
						snprintf( szPathChild, sizeof(szPathChild), "/proc/%s/stat", pThreadEntry->d_name );
						IsThreadRunning = Is_Thread_Running( szPathChild );

						if ( bShow_All )	{
							Query_Task_CPUSet( tid, &bMainThread, szMsg );
						}
						else	{
							if ( IsThreadRunning )	Query_Task_CPUSet( tid, &bMainThread, szMsg );
						}
						thread_count++;
					}
					closedir( pDirProc );
				}
				else	{
					printf( "Couldn't open the directory %s\n", szPath );
				}
			}
		}
		closedir( pDirRoot );
	}
	else
		perror( "Couldn't open the directory /proc.\n" );
}

/*
Query_Task_CPUSet() queries and prints the binding affinity for a given thread. 
Input parameters:
     tid - the thread id we need to query binding affinity.
     bMainThread - the flag whether current thread is the main thread of current process or not.
     szMsg - This buffer contains the thread id (of main thread) and executable file name.  
Output parameters:
     bMainThread - *bMainThread == 1 means current thread is the main thread of a process. *bMainThread = 0 if (*bMainThread == 1). 
*/

void Query_Task_CPUSet( const int tid, int *bMainThread, const char szMsg[] )	{
	char szBuff_Affinity[LEN_AFFINITY_BUFF];
	cpu_set_t my_set;

	CPU_ZERO( &my_set );

	if ( sched_getaffinity( tid, sizeof(my_set), &my_set ) == -1 )	{
		printf( "Failed to get tid %d's affinity. Error no. is %x\nQuit\n", tid, errno );
		exit( 1 );
	}
	cpulist_create( szBuff_Affinity, LEN_AFFINITY_BUFF, &my_set, sizeof(my_set) );
	if ( *bMainThread )	{	// The first thread is the main thread. The followings are not. 
		printf( "%s %-6d  %-43s\n", szMsg, tid, szBuff_Affinity );
		*bMainThread = 0;
	}
	else	printf( "                             %-6d  %-43s\n", tid, szBuff_Affinity );
}

/*
Is_Thread_Running() Query whether a thread is running or not with file "/proc/%tid/stat". 
Input parameters:
     szName - The full path of "/proc/%tid/stat". 
 Returned parameter:
     0 - not running; 1 - running
*/

int Is_Thread_Running(const char szName[])	{
	int pid, ppid, nItemsRead;
	char szBuff[SIZE_STAT];
	char RunningStatus[512], szExeName[512];
	FILE *fIn;
	
	fIn = fopen( szName, "r" );
	if ( fIn == NULL )	{
		printf( "Warning: Error to open file %s\n", szName );
		return 0;
	}
	fread( szBuff, 1, SIZE_STAT, fIn );
	fclose( fIn );
	
	nItemsRead = sscanf( szBuff, "%d%s%s%d", &pid, szExeName, RunningStatus, &ppid );
	if( nItemsRead != 4 )	{
		printf( "Warning: Unexpected content. %s\nnItemsRead = %d\n", szBuff, nItemsRead );
	}

	if( RunningStatus[0] == 'R' )	return 1;
	else	return 0;
}

/*
cpulist_create() generates a human readable string from an array of bits of cpu binding affinity. 
Input parameters:
     str - The buffer to hold the human readable string. 
     len - The max length of str[].
	 set - cpu masks from sched_getaffinity(). 
	 setsize - the number of bytes set occupies. 
Returned parameter:
     str - The buffer to hold the human readable string. 
*/

char *cpulist_create(char *str, const size_t len, const cpu_set_t *set, const size_t setsize)	{
	size_t i, j, BlockSize, nBytesLeft=len, nBytesWritten;
	size_t MaxLenSet = 8 * (setsize);	// Each byte has 8 bits. 
	char *ptr = str;
	int EntryMade = 0;

	for ( i = 0; i < MaxLenSet; i++ ) {
		if ( CPU_ISSET_S(i, setsize, set) ) {
			BlockSize = 0;
			EntryMade = 1;
			for ( j = i + 1; j < MaxLenSet; j++ ) {	// To determine the size of blocks of cores allow current thread to run. Just for a better presentation of results. 
				if ( CPU_ISSET_S(j, setsize, set) )
					BlockSize++;
				else
					break;
			}
			if ( !BlockSize )
				nBytesWritten = snprintf( ptr, nBytesLeft, "%zu,", i );
			else if ( BlockSize == 1 ) {
				nBytesWritten = snprintf( ptr, nBytesLeft, "%zu,%zu,", i, i + 1 );
				i++;
			}
			else {
				nBytesWritten = snprintf( ptr, nBytesLeft, "%zu-%zu,", i, i + BlockSize );
				i += BlockSize;
			}
			if ( ( nBytesWritten < 0 ) || ( (size_t) nBytesWritten >= nBytesLeft ) )	{
				printf( "Error: str[] is not long enough to hold binding affinity info.\n" );
				return NULL;
			}

			ptr += nBytesWritten;
			nBytesLeft -= nBytesWritten;
		}
	}
	ptr -= EntryMade;
	*ptr = '\0';

	return str;
}

#define MIN_LEN_PER_LINE	(24)	// minimal length per line in file /proc/stat

// Determine_Number_of_Cores() determines how many logical cores on current computer from file /proc/stat. 
// Return value:
//     The number of logical cores.  

int Determine_Number_of_Cores(void)	{
	FILE *fIn;
	char szBuff[BUFF_SIZE];
	size_t nBytesRead;
	int iPos, nCoreLocal;

	fIn = fopen( "/proc/stat", "r" );
	if ( fIn == NULL )	{
		printf("Fatal error: Fail to open file /proc/stat.\n");
		exit(1);
	}
	
	nBytesRead = fread( szBuff, 1, BUFF_SIZE, fIn );
	fclose( fIn );
/*
Example head of /proc/stat, 

cpu  334573387 201030304 287161779 119936862297 143550565 0 8066790 0 0 0
cpu0 11810689 13567949 5778954 2122299612 3007485 0 1259027 0 0 0
cpu1 15361040 30313968 6503408 2103388061 1671157 0 200641 0 0 0
...
*/
	iPos = Get_Offset_of_Next_Line( szBuff, MIN_LEN_PER_LINE, nBytesRead );	// find the position of the second line
	
	nCoreLocal = 0;
	while ( iPos >= 0 )	{
		if( strncmp( szBuff+iPos, "cpu", 3 ) == 0 )	{
			nCoreLocal++;

			if(nCoreLocal > MAX_CORE)	{
				printf( "nCores > MAX_CORE\n" );
				exit( 1 );
			}
		}
		else	{	// Already found all CPUs available on the node
			break;
		}
		iPos = Get_Offset_of_Next_Line( szBuff, iPos + MIN_LEN_PER_LINE, nBytesRead );	// find the next line
	}

//	printf("There are %d cores on %s\n", nCoreLocal, szHostName);

	return nCoreLocal;
}
#undef MIN_LEN_PER_LINE

// Get_Offset_of_Next_Line() determines where the next new line starts in a given buffer. 
// Input parameters:
//     szBuff - The buffer to check
//     iPosStart - Starting offset
//     nBuffSize - The total length of the buffer
// Return value:
//     If positive, the offset of the next new line. (-1) means no new line is found. 

int Get_Offset_of_Next_Line(const char szBuff[], const int iPosStart, const int nBuffSize)	{
	int i=iPosStart;

	while ( i < nBuffSize )	{
		if ( szBuff[i] == 0xA )	{	// A new line
			return (i+1);	// pointing to the beginning of next line
		}
		else	{
			i++;
		}
	}
	return (-1);
}

// Extract_Exec_Name() extracts the executable file name for a given process from "/proc/%pid/stat". 
// Input parameters:
//     pid - the process id. 
//     nMaxStrLen - The maximum allowed length of buffer szExeName.
// Output parameter:
//     szExeName[] - The buffer that holds the executable file name.  

void Extract_Exec_Name(const int pid, char szExeName[], const int nMaxStrLen)	{
	char szPath[512];
	int i, count=0, nBytesRead, nMaxStrLenAllowed = nMaxStrLen-1;
	char szBuff[SIZE_STAT];
	FILE *fIn=NULL;
	
	szExeName[0] = 0;

	snprintf( szPath, sizeof(szPath), "/proc/%d/stat", pid );
	fIn = fopen( szPath, "r" );
	if( fIn == NULL )	return;

	nBytesRead = fread( szBuff, 1, nMaxStrLen, fIn );
	fclose( fIn );

	// Example of szBuff, "120097 (sshd) "

	i = 0;
	while ( i < nBytesRead )	{
		if( szBuff[i] == '(' )	{	// starting with '('
			break;
		}
		i++;
	}
	if( i >= nBytesRead )	return;

	i++;
	while( i < nBytesRead )	{
		if( szBuff[i] == ')' )	{	// ending with ')'
			break;
		}
		else	{
			szExeName[count] = szBuff[i];
			count++;
			if( count >= nMaxStrLenAllowed )	{	// The buffer is full. 
				break;
			}
		}
		i++;
	}
	szExeName[count] = 0;
}
