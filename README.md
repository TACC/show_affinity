# show_affinity
show_affinity is a tool under Linux to show the core binding affinity of running processes/threads of current user. 


To compile,<br> 
`gcc -Wall -o show_affinity show_affinity.c`

To run the command, <br>
`./show_affinity [all]`

Without any parameter, show_affinity will only show the results of processes/threads of current user that have running statuses.

With "all" as a parameter, show_affinity show the results of all processes/threads of current user. 
<br>
<br>
<br>
Example output, <br>
![](https://github.com/TACC/show_affinity/blob/master/affinity.png)

