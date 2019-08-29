/*************************************************************************
--------------------------------------------------------------------------
--  show_affinity License
--------------------------------------------------------------------------
--
--  show_affinity is licensed under the terms of the MIT license reproduced
--  below. This means that Lmod is free software and can be used for both
--  academic and commercial purposes at absolutely no cost.
--
--  ----------------------------------------------------------------------
--
--  Copyright (C) 2017-2019 Lei Huang
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

#define MAX_REC	(2048)
#define MAX_CORE	(2048)

void Enumerate_All_PID(void);
int Is_Thread_Running(char szName[]);
int Get_Position_of_Next_Line(char szBuff[], int iPos, int nBuffSize);
void Read_Proc_Stat(void);
void Query_Task_Set(int tid, int *ShowName, char szMsg[]);
void Extract_Exec_Name(int pid, char szExeName[]);
char *cpulist_create(char *str, size_t len, cpu_set_t *set, size_t setsize);

#define LEN_AFFINITY_BUFF	(1024*100)
char szBuff_Affinity[LEN_AFFINITY_BUFF];

int my_uid, my_pid;
char szHostName[256];
int nThreads=0;	// The total number of threads (logic cores) on the node 
int Show_All=0;

int main(int argc, char argv[])
{
	if(argc == 2)	Show_All = 1;
	else	Show_All = 0;

	my_pid = getpid();
	my_uid = getuid();
	gethostname(szHostName, 255);

	Read_Proc_Stat();


	Enumerate_All_PID();
	

	return 0;
}

void Enumerate_All_PID(void)	// exhaustively enumerate all PIDs
{
	DIR *dp, *dp_task;
	struct dirent *ep, *ep_task;
	char szPath[512], szPath_Child[512], szExeName[512], szMsg[256], c;
	struct stat file_stat;
	int pid, tid, thread_count;
	int IsThreadRunning, ShowName;

	printf("pid     Exe_Name             tid     Affinity\n", pid, szExeName);
	
	dp = opendir("/proc");
	if (dp != NULL)	{
		while (ep = readdir (dp))	{
			sprintf(szPath, "/proc/%s", ep->d_name);
			c = ep->d_name[0];
			if( (c < '0') || (c > '9') )	continue;	// not starting with a number
			
			pid = atoi(ep->d_name);
			
			if(stat(szPath, &file_stat) == -1)	continue;	// error
			if(pid == my_pid)	continue;	// skip checking my tools itself
			
			if(file_stat.st_uid == my_uid)	{	// build the list of my jobs
				thread_count = 0;
				ShowName = 1;
				sprintf(szPath, "/proc/%d/task", pid);
				dp_task = opendir(szPath);
				if (dp_task != NULL)	{
					while( ep_task = readdir (dp_task) )	{
						c = ep_task->d_name[0];
						if( (c < '0') || (c > '9') )	continue;	// not starting with a number

						if(thread_count == 0)	{
							Extract_Exec_Name(pid, szExeName);
							sprintf(szMsg, "%-6d  %-15s     ", pid, szExeName);
						}						

						tid = atoi(ep_task->d_name);
						sprintf(szPath_Child, "/proc/%s/stat", ep_task->d_name);
						IsThreadRunning = Is_Thread_Running(szPath_Child);

						if(Show_All)	{
							Query_Task_Set(tid, &ShowName, szMsg);
						}
						else	{
							if(IsThreadRunning)	Query_Task_Set(tid, &ShowName, szMsg);
						}
						thread_count++;
					}
					closedir(dp_task);
				}
				else
					perror ("Couldn't open the directory");
			}
		}
		closedir(dp);
	}
	else
		perror ("Couldn't open the directory");
}


void Query_Task_Set(int tid, int *ShowName, char szMsg[])
{
	cpu_set_t my_set;
	CPU_ZERO(&my_set);

	if( sched_getaffinity(tid, sizeof(my_set), &my_set) == -1 )	{
		printf("Failed to get tid %d's affinity. Error no. is %x\nQuit\n", tid, errno);
		exit(1);
	}
	cpulist_create(szBuff_Affinity, LEN_AFFINITY_BUFF, &my_set, sizeof(my_set));
	if( *ShowName == 1 )	{
		*ShowName = 0;
		printf("%s %-6d  %-43s\n", szMsg, tid, szBuff_Affinity);
	}
	else	printf("                             %-6d  %-43s\n", tid, szBuff_Affinity);;
}


#define SIZE_STAT	(256)
int Is_Thread_Running(char szName[])
{
	int fd;
	int num_read, ReadItems, pid, ppid;
	char szBuff[SIZE_STAT];
	char RunningStatus[64], szExeName[128];
	
	fd = open(szName, O_RDONLY, 0);	// open, read the file take 2.4 milliseconds. KNL is 3 times slower than haswell. 
	if(fd == -1)	return 0;
	num_read = read(fd, szBuff, SIZE_STAT);
	close(fd);
	
	ReadItems = sscanf(szBuff, "%d%s%s%d", &pid, szExeName, RunningStatus, &ppid);
	if(RunningStatus[0] == 'R')	return 1;
	else	return 0;
}


#define cpuset_nbits(setsize)	(8 * (setsize))

char *cpulist_create(char *str, size_t len, cpu_set_t *set, size_t setsize)
{
	size_t i;
	char *ptr = str;
	int entry_made = 0;
	size_t max = cpuset_nbits(setsize);

	for (i = 0; i < max; i++) {
		if (CPU_ISSET_S(i, setsize, set)) {
			int rlen;
			size_t j, run = 0;
			entry_made = 1;
			for (j = i + 1; j < max; j++) {
				if (CPU_ISSET_S(j, setsize, set))
					run++;
				else
					break;
			}
			if (!run)
				rlen = snprintf(ptr, len, "%zu,", i);
			else if (run == 1) {
				rlen = snprintf(ptr, len, "%zu,%zu,", i, i + 1);
				i++;
			} else {
				rlen = snprintf(ptr, len, "%zu-%zu,", i, i + run);
				i += run;
			}
			if (rlen < 0 || (size_t) rlen >= len)
				return NULL;
			ptr += rlen;
			len -= rlen;
		}
	}
	ptr -= entry_made;
	*ptr = '\0';

	return str;
}

#define BUFF_SIZE	(131072)	// 128 KB

void Read_Proc_Stat(void)	// 4.2 ms in total
{
	int fd;
	char szThreadIdx[256], szBuff[BUFF_SIZE];
	int i, num_read, iPos;

	fd = open("/proc/stat", O_RDONLY, 0);	// open, read the file take 2.4 milliseconds. KNL is 3 times slower than haswell. 
	if(fd == -1)	return;
	
	num_read = read(fd, szBuff, BUFF_SIZE);
	close(fd);

	iPos = Get_Position_of_Next_Line(szBuff, 24, num_read);	// find the position of the second line
	
	nThreads = 0;
	while(iPos >= 0)	{
		if( strncmp(szBuff+iPos, "cpu", 3) == 0 )	{
			nThreads++;

			if(nThreads > MAX_CORE)	{
				printf("nThreads > MAX_CORE\n");
				exit(1);
			}
		}
		else	{	// Already found all CPUs available on the node
			break;
		}
		iPos = Get_Position_of_Next_Line(szBuff, iPos + 24, num_read);	// find the next line
	}

//	printf("There are %d threads on %s\n", nThreads, szHostName);
}

int Get_Position_of_Next_Line(char szBuff[], int iPos, int nBuffSize)
{
	int i=iPos;

	while(i < nBuffSize)	{
		if(szBuff[i] == 0xA)	{	// A new line
			return (i+1);	// pointing to the beginning of next line
		}
		else	{
			i++;
		}
	}
	return (-1);
}

void Extract_Exec_Name(int pid, char szExeName[])
{
	char szPath[512];
	int fd, nLen=196, i=0, count=0;
	int num_read;
	char szBuff[SIZE_STAT];
	
	sprintf(szPath, "/proc/%d/stat", pid);
	fd = open(szPath, O_RDONLY, 0);	// open, read the file take 2.4 milliseconds. KNL is 3 times slower than haswell. 
	if(fd == -1)	return;

	num_read = read(fd, szBuff, nLen);
	close(fd);

	szExeName[0] = 0;

	while(i < nLen)	{
		if(szBuff[i] == '(')	{
			break;
		}
		i++;
	}
	if(i >= nLen)	return;

	i++;
	while(i < nLen)	{
		if(szBuff[i] == ')')	{
			break;
		}
		else	{
			szExeName[count] = szBuff[i];
			count++;
		}
		i++;
	}
	szExeName[count] = 0;
}


