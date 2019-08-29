show_affinity is a tool to show the core binding affinity of running processes/threads of current user. 

To compile, 
gcc -O2 -o show_affinity show_affinity.c 

To run the command, 
./show_affinity [all]

Without any parameter, show_affinity will only show the results of processes/threads of current user that keep cpu busy.

With "all" as a parameter, show_affinity show the results of all processes/threads of current user. 

