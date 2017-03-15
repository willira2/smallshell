# smallshell
This is a simplistic bash-like shell written in C. 

It supports three built in commands - exit, cd, and status - as well as comments, which are lines beginning with the # character. It executes other commands through fork()/execvp(). Standard input and standard output can be redirected, and it also supports both foreground and background processes (controllable by the command line and by receiving signals).

