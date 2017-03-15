/*********************************************************************
 ** Program Filename: smallsh.c
 ** Author: Rachel Williams
 ** Date: 3 - 5 - 17
 ** Description: smallsh assignment
 *********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

char* getUserCommand();
int testUserCommand(char*);
int bufferNotBlank(char*);
int commandType(char*);
void changeDirectory(char*);
int isBackground(char*);

void parseCommand(char*, char**);
int needsExpansion(char*);
void expHandler(char**);
void expandArray(char**);
int pidPosition(char*);

int testRedirection(char*);
void getNewPath(int, int, char**, char**, char**);
void addBgPid(int, int[]);
void checkBgKids(int[]);
void catchSIGINT(int);
void catchSIGTSTP(int);

//global flags for SIGTSTP function and background process usage
static int _bgFlag = 0; //initially set to false/off
static int _fgChild = 0; //will hold pid of fg child 


void main()
{

	pid_t spawnid = -5;
	int childExitMethod = -5, cType = -5, backgroundBool = -5, redirectFlag = -5;
	int sourceFD, targetFD, result;

	int backgroundProcesses[50] = {0};

	char *command = NULL;
	char *inPath; //stdin redirection
	char *outPath; //stdout redirection

	char** execArgs;	//malloc'd array to hold command arguments

	//SIGINT handler setup
	struct sigaction SIGINT_action = {0};

	SIGINT_action.sa_handler = catchSIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = SA_RESTART;

	sigaction(SIGINT, &SIGINT_action, NULL);

	//SIGSTP handler setup
	struct sigaction SIGTSTP_action = {0};

	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;

	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	do{
		//check waitpid with NOHANG
		checkBgKids(backgroundProcesses);

		//get user command
		command = getUserCommand();

		//determine if built-in or not built-in
		cType = commandType(command);

		//if not built in, remove any instance of &
		if(cType != 0)
		{
			if(strcspn(command, "&") != strlen(command))
			{
				command[(strcspn(command, "&"))-1] = 0;
			}
		}

		//allocate enough space for arguments, depending on numArgs return value
		execArgs = (char**)calloc((numArgs(command))+2, sizeof(char*));
		
		switch (cType){
			case 0: //not built-in command
				
				//test for redirection operators and what kind
				redirectFlag = testRedirection(command);

				//test if background command
				backgroundBool = isBackground(command);

				//if redirectFlag > 0, call getNewPath
				if(redirectFlag > 0)
				{
					getNewPath(backgroundBool, redirectFlag, &command, &inPath, &outPath);
				}

				//put command into array, removing & if necessary
				parseCommand(command, execArgs);

				//expand any instance of $$ in command array
				expandArray(execArgs);

				//fork
				spawnid = fork();
				
				switch(spawnid)
				{
					//beep boop fork error
					case -1:
						perror("Hull breach!");
						exit(1);
					break;

					//child
					case 0:

						//if background process and background processes are allowed
						if(backgroundBool == 1 && _bgFlag == 0)
						{
							//redirect BOTH stdin and stdout depending on redirectFlag
							if(redirectFlag == 1) //redirect stdin to inPath, stdout to /dev/null
							{
								sourceFD = open(inPath, O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								targetFD = open("/dev/null", O_WRONLY);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1); 
								} 

							}

							else if(redirectFlag == 2) //redirect stdin to /dev/null, stdout to outPath
							{
								sourceFD = open("/dev/null", O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								targetFD = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1);
								} 
							}

							else if(redirectFlag == 3) //redirect stdin to inPath, stdout to outPath
							{
								sourceFD = open(inPath, O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								targetFD = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1);
								} 
							}

							else //redirect stdin to /dev/null, stdout to /dev/null
							{
								sourceFD = open("/dev/null", O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1); 
								}
							}

							// redirect streams
							result = dup2(sourceFD, 0);	
							if (result == -1) { perror("source dup2()"); exit(2); } 

							result = dup2(targetFD, 1);
							if (result == -1) { perror("target dup2()"); exit(2); }

							//exec
							if (execvp(*execArgs, execArgs) < 0)
		   					{
				    			perror("Exec failure!");
									exit(1);
								}
						} 

						//if background is not specified OR bg flag has been raised
						//this will catch foreground processes AND background processes that have been blocked by SIGSTP
						else if((backgroundBool == 0) | (_bgFlag == 1)) 
						{

							//save fg child pid
							_fgChild = spawnid;

							//redirect if redirectFlag != 0
							if(redirectFlag == 1) //redirect stdin to inPath
							{
								sourceFD = open(inPath, O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								result = dup2(sourceFD, 0);	
								if (result == -1) { perror("source dup2()"); exit(2); }
							}

							else if(redirectFlag == 2) //redirect stdout to outPath
							{
								targetFD = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1);
								} 

								result = dup2(targetFD, 1);
								if (result == -1) { perror("target dup2()"); exit(2); }
							}

							else if(redirectFlag == 3) //redirect stdin to inPath, stdout to outPath
							{
								sourceFD = open(inPath, O_RDONLY);
								if (sourceFD == -1) 
								{ 
									perror("source open()"); 
									exit(1); 
								} 

								targetFD = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (targetFD == -1) 
								{ 
									perror("target open()"); 
									exit(1);
								} 

								// redirect streams
								result = dup2(sourceFD, 0);	
								if (result == -1) 
									{ 
										perror("source dup2()"); 
									  exit(2); 
									} 

								result = dup2(targetFD, 1);
								if (result == -1) 
									{ 
										perror("target dup2()"); 
										exit(2); 
									}
							}

							//exec 
							if (execvp(*execArgs, execArgs) < 0)
		   					{
				    			perror("Exec failure!");
									exit(1);
								}
						}
					
					break;

					//parent
					default:
						//reset stdin and stdout depending on redirectFlag
						//if bg, print bg pid and add to background array
						if(backgroundBool == 1 && _bgFlag == 0)
						{
							//print background pid
							printf("background pid is %d\n", spawnid);
							fflush(stdout);

							// add bg pid to bgPid[]
							addBgPid(spawnid, backgroundProcesses);
						}
						
						//if fg, waitpid
						else 
						{
							waitpid(spawnid, &childExitMethod, 0);
							_fgChild = 0; //reset _fgChild

							if (WIFSIGNALED(childExitMethod)) 
				    		{
				       			printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
				       			fflush(stdout);
				   			}
						}
					break;
				}
		

			break;

			case 1: //exit command
				//free malloc'd memory
				if(command)
				{
					free(command);
				}

				if(execArgs)
				{
					int y = 0;
					for(y; execArgs[y]; y++)
					{
						free(execArgs[y]);
					}

					free(execArgs);	
				}
				//kill process and all children with SIGTERM
				kill(0, SIGTERM);

			break;

			case 2: //status command
				if (WIFEXITED(childExitMethod))
				{
					printf("exit value %d\n", WEXITSTATUS(childExitMethod));
					fflush(stdout);
				} 
				else if (WIFSIGNALED(childExitMethod))
				{
					printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
				  fflush(stdout);
				}

			break;

			case 3: //cd command

				changeDirectory(command);

			break;
		}

	//free malloc'd memory
	if(command)
	{
		free(command);
	}

	if(execArgs)
	{
		int y = 0;
		for(y; execArgs[y]; y++)
		{
			free(execArgs[y]);
		}

		free(execArgs);	
	}
		
	}while(1); 

}
 
 /*********************************************************************
 ** Function: catchSIGTSTP
 ** Description: SIGTSTP handler function. the first time it catches SIGTSTP, 
 it changes the global var _bgFlag to 1 and prints a message that user has 
 entered foreground-only mode. The next time SIGTSTP is caught, _bgFlag is set
 back to 0, and a message is printed to user that foreground-only mode has been 
 exited
 ** Parameters: int signo, the number of the signal that was caught
 ** Pre-Conditions: a SIGTSTP signal has been caught
 ** Post-Conditions: _bgFlag is set to 1 or 0 depending on it's pre-function
 call state, and an informative message about this change is printed to stdout
 *********************************************************************/
void catchSIGTSTP(int signo)
{
	//if fg is off, turn it on 
	if(_bgFlag == 1)
	{
		char* message = "\nExiting foreground-only mode\n:";
		write(STDOUT_FILENO, message, 31);
		_bgFlag = 0;
	}
	
	//else fg is on, turn it off
	else
	{
		char* message = "\nEntering foreground-only mode (& is now ignored)\n:";
		write(STDOUT_FILENO, message, 51);
		_bgFlag = 1;
	}
}


 /*********************************************************************
 ** Function: catchSIGINT
 ** Description: SIGINT handler function. if the foreground child global
 variable _fgChild is not equal to 0, ie there is currently a fg child running, 
 this function kills that child using pid stored in _fgChild.
 ** Parameters: int signo, the number of the signal captured by the sigaction
 function 
 ** Pre-Conditions: a SIGINT signal has been caught
 ** Post-Conditions: the foreground child, if one exists, has been killed
 *********************************************************************/
void catchSIGINT(int signo)
{
	if(_fgChild != 0)
	{
		kill(_fgChild, 15);
	}	
}

 /*********************************************************************
 ** Function: getUserCommand
 ** Description: prompts user for input with ':', continues prompting until 
 user enters a non blank line or one not beginning with '#'. allocates space 
 for the entered command and returns pointer to it
 ** Parameters: none
 ** Pre-Conditions: none
 ** Post-Conditions: pointer to a string is returned once user enters a 
 line that is not blank and does not begin with '#'
 *********************************************************************/
char* getUserCommand()
{
	int bytes_read = -5, length = 0;
	size_t nbytes = 0;
	char* buffer = NULL;
	char* returnString = NULL;

	do
	  {
	  	fflush(stdout);
	  	printf(":");

	  	bytes_read = getline(&buffer, &nbytes, stdin);

	  	if(bytes_read == -1)
	   	 {
	      perror("getline error");
	   	 }

	  }while((testUserCommand(buffer) != 0) | (bytes_read == -1));

	  length = strlen(buffer) + 1;
	  returnString = (char*)malloc(length * sizeof(char));
	  strcpy(returnString, buffer);

	  if(buffer)
	  {
	  	free(buffer);
	  }

	 return returnString;
 
}

/*********************************************************************
 ** Function: testUserCommand
 ** Description: checks if user command is a comment line or blank line, 
 returning 1 if so. Else, returns 0.
 ** Parameters: char* buffer - received via stdin in getUserCommand function
 ** Pre-Conditions: buffer is a valid char* 
 ** Post-Conditions: command has been tested, and return value indicates
 correct classification of command
 *********************************************************************/
 int testUserCommand(char* buffer)
 {
 	int returnValue = 0;
 	char currChar = buffer[0];

 	if((currChar == '#') | (bufferNotBlank(buffer) == 0))
 	{
 		returnValue = 1;
 	}

 	return returnValue;

 }

 /*********************************************************************
 ** Function: bufferNotBlank
 ** Description: checks if user's command input is just a blank line. returns
 0 if it is blank, 1 if not
 ** Parameters: char* buffer - user's input string
 ** Pre-Conditions: buffer is a valid string
 ** Post-Conditions: 0 is returned if string is empty, 1 elsewise
 *********************************************************************/
 int bufferNotBlank(char* buffer)
 {
 	char* currChar = buffer;

 	while(*currChar)
 	{
 		if((*currChar != ' ') && (*currChar != '\n'))
 		{
 			return 1;
 		}

 		else
 		{
 			currChar++;
 		}
 	}

 	return 0;

 }

 /*********************************************************************
 ** Function: commandType
 ** Description: tests argument to see if command is built-in (and if so, 
 what kind of built-in), not built-in, or blank. Returns 0 if not built-in,
 1 if exit command, 2 if status command, 3 if cd command, or 4 if blank
 ** Parameters: char* buffer, a char* string containing user's full 
 input string
 ** Pre-Conditions: buffer is a valid char*
 ** Post-Conditions: 0, 1, 2, 3, or 4 is returned
 *********************************************************************/
 
 int commandType(char* buffer)
 {
 	char* exitCommand = "exit\n";
 	char* statusCommand = "status";
 	char* cdCommandArg = "cd ";
 	char* cdCommandNoArg = "cd\n";

 	if(strstr(buffer, exitCommand) != NULL && strlen(buffer) == strlen(exitCommand))
 	{
 		return 1;
 	}

 	else if(strstr(buffer, statusCommand) != NULL)
 	{
  		return 2;
 	}

 	else if((strstr(buffer, cdCommandArg) != NULL) | (strstr(buffer, cdCommandNoArg) != NULL))
 	{
 		if(buffer[0] != 'c')
 		{
 			return 0;
 		}
 		return 3;
 	}

 	else if(bufferNotBlank(buffer) == 0)
 	{
 		return 4;
 	}

 	else
 	{
 		return 0;
 	}

 }

 /*********************************************************************
 ** Function: isBackground
 ** Description: returns 1 if input string contains " &" substring, 
 signalling that input string is a background command. else returns 0
 ** Parameters: char* buffer
 ** Pre-Conditions: buffer is a valid string
 ** Post-Conditions: 0 or 1 is returned
 *********************************************************************/
 int isBackground(char* buffer)
 {
 	char ampersand = '&';

 	if(buffer[strlen(buffer)-1] == ampersand)
 	{
 		return 1;
 	}

 	else
	{
		return 0;
	}

 }

 /*********************************************************************
 ** Function: numArgs
 ** Description: counts the number of space chars in the buffer argument. 
 This count is equal to one less than the number of substrings in the buffer
 ** Parameters: char* buffer, a string containing the user's command and 
 arguments
 ** Pre-Conditions: buffer is a valid char*
 ** Post-Conditions: count is returning, containing one fewer than the 
 number of words in the user command
 *********************************************************************/
 int numArgs(char* buffer)
 {
 	int d = 0, count = 0;
 	char* command = buffer;

 	//get rid of trailing newline
 	command[strcspn(command, "\n")] = 0;

 	for (d; command[d]; d++)
	{
		count += (command[d] == ' ');
	}

	return count;

 }

 /*********************************************************************
 ** Function: parseCommand
 ** Description: takes a char* string and uses strtok to split it into 
 smaller strings with delimiters of the space char and ampersand char, 
 saving each resultant substring in the argArr array. The '&' is used as 
 a delimeter because this will strip the unneeded'&' off the string if it exists.
 ** Parameters: char* buffer - a string containing the user's shell command
 and arguments, separated by spaces
 ** Pre-Conditions: buffer is a valid string, argArr has been allocated
 enough memory to hold every substring in the command string
 ** Post-Conditions: argArr holds every substring in the buffer argument
 *********************************************************************/
 void parseCommand(char* buffer, char** argArr)
 {
 	int i = 0;
 	char* token;
 	char* delim = " &";
 	char* command = buffer;

 	token = strtok(command, delim);

 	while(i < 512 && token != NULL)
 	{
 		argArr[i] = (char*)malloc((strlen(token)+1)*(sizeof(char)));
 		strcpy(argArr[i], token);
 		token = strtok(NULL, delim);
 		i++;
 	}

 }

  /*********************************************************************
 ** Function: changeDirectory
 ** Description: changes cwd directory to a new directory, depending on
 the arguments in the parameter string. full path names are created for
 absolute and relative paths if needed
 ** Parameters: char* buffer, a command from the user to change directories.
 may or may not include an argmument after the initial "cd" command
 ** Pre-Conditions: buffer is a valid string
 ** Post-Conditions: cwd is changed to user's desired cwd, if the path
 requested is valid. else, error text is printed to stdout
 *********************************************************************/

 void changeDirectory(char* buffer)
 {
 	char* cdCommand = "cd";
 	char* absPath = " /";
 	char absPathBeginning = '/';

 	char* path = NULL;
 	char d = 'd';
 	int chdirValue = -5;

 	//no argument
 	if(strcmp(buffer, cdCommand) == 0)
 	{
 		chdirValue = chdir(getenv("HOME"));

 		if(chdirValue == -1)
 		{
 			printf("chdir error in $HOME path\n");
 			fflush(stdout);
 		}
 	}

 	//absolute path
 	else if(strstr(buffer, absPath) != NULL)
 	{
   		path = strchr(buffer, absPathBeginning);
   		path[strcspn(path, "\n")] = 0;

   		if(needsExpansion(path) == 1)
			{
				expHandler(&path);
			}

   		chdirValue = chdir(path);

   		if(chdirValue == -1)
	 		{
	 			printf("chdir error in absolute path\n");
	 			fflush(stdout);
	 		}
 	}

 	//relative path
 	else
 	{
 		path = strchr(buffer, d);
   		path[strcspn(path, "\n")] = 0;
   		path += 2; //move two spaces to move past d and blank space

   		if(needsExpansion(path) == 1)
			{
				expHandler(&path);
			}

   		chdirValue = chdir(path);

   		if(chdirValue == -1)
	 		{
	 			printf("chdir error in relative path\n");
	 			fflush(stdout);
	 		}
 	}
 }

/*********************************************************************
** Function: needsExpansion
** Description: if function argument contains $$, 1 is returned. else, 
0
** Parameters: char* command
** Pre-Conditions: command is a valid string
** Post-Conditions: 0 or 1 is returned
*********************************************************************/
int needsExpansion(char* command)
{
	char* dolla = "$$";

	if(strstr(command, dolla) != NULL)
	{
		return 1;
	}

	return 0;
}

/*********************************************************************
** Function: pidPosition
** Description: returns 0, 1, or 2 depending on where in the argument
string there is an instance of $
** Parameters: char* buffer - a string containing $$
** Pre-Conditions: buffer is a valid string containing $$
** Post-Conditions: 0, 1, or 2 is returned
*********************************************************************/
int pidPosition(char* buffer)
{
	if(buffer[0] == '$')
	{
		return 0;
	}

	else if(buffer[strlen(buffer)-1] == '$')
	{
		return 1;
	}

	else
	{
		return 2;
	}
}

/*********************************************************************
** Function: expHandler
** Description: finds the position of $$ in the string pointer passed
to thef function and then replaces that instance with the pid of the 
current process. This function is called before fork, and so the pid is
the shell pid.
** Parameters: char** buffer - pointer to a command argument
** Pre-Conditions: buffer points to a not-null string
** Post-Conditions: any instance of $$ in the function argument is 
replaced with the pid of the current process
*********************************************************************/
void expHandler(char** buffer)
{
	int procId = getpid();
	char pidString[10];
	char fullString[50];
	char* brokenString[2];

	int pidPos = pidPosition(*buffer);

 	char* token;
 	char* delim = "$$";
 	char* command = *buffer;

 	snprintf(pidString, 9, "%d", procId);

 	token = strtok(command, delim);

 	if(token == NULL)
 	{
 		//*buffer = (char*)malloc((strlen(pidString)*sizeof(char)));
 	 	strcpy(*buffer, pidString);
 	}

 	else
 	{
 		brokenString[0] = (char*)malloc((strlen(token)*sizeof(char)));
 	 	strcpy(brokenString[0], token);
 	
 	 	token = strtok(NULL, delim);
 	 	
 	
 	 	if(token != NULL)	
 	 	{
 			brokenString[1] = (char*)malloc((strlen(token)*sizeof(char)));
 	 		strcpy(brokenString[1], token);
 	 	}	
 	
 	 switch (pidPos)
 	 {
 	 	case 0:
 	 		strcpy(fullString, pidString);
 	
 	 		strcat(fullString, brokenString[0]);

 	 		free(brokenString[0]);
 	
 	 		//*buffer = (char*)malloc((strlen(fullString)*sizeof(char)));
 	 		strcpy(*buffer, fullString);
 	 	break;
 	
 	 	case 1:
 	 		strcpy(fullString, brokenString[0]);

 	 		free(brokenString[0]);
 	
 	 		strcat(fullString, pidString);
 	
 	 		//*buffer = (char*)malloc((strlen(fullString)*sizeof(char)));
 	 		strcpy(*buffer, fullString);
 	 	break;
 	
 	 	case 2:
 	 		strcpy(fullString, brokenString[0]);

 	 		free(brokenString[0]);
 	
 	 		strcat(fullString, pidString);
 	
 	 		strcat(fullString, brokenString[1]);

 	 		free(brokenString[1]);
 	
 	 		//*buffer = (char*)malloc((strlen(fullString)*sizeof(char)));
 	 		strcpy(*buffer, fullString);
 	 	break;
 	 }
 	}
}

/*********************************************************************
** Function: expandArray
** Description: examines every element in the execArgs array, calling
needsExpansion on each. If needsExpansion returns 1, that element is 
passed by reference to expHandler
** Parameters: char* execArgs[], an array of char strings holding the 
arguments for the command
** Pre-Conditions: execArgs is a valid array
** Post-Conditions: any array element containing an instance of $$ is
expanded
*********************************************************************/
void expandArray(char* execArgs[])
{
	int i = 0;
	while(execArgs[i] != NULL)
	{
		if(needsExpansion(execArgs[i]) == 1)
		{
			expHandler(&(execArgs[i]));
		}

		i++;
	}
}

/*********************************************************************
** Function: testRedirection
** Description: searches string for occurrences of < and >. returns 0, 1,
2, or 3 depending on result of search. 0 if not found, 1 if only < is found,
2 if only > is found, and 3 if both are found.
** Parameters: char* buffer, the full string received from user as a command
** Pre-Conditions: char* buffer is a valid string
** Post-Conditions: 0, 1, 2, or 3 is returned
*********************************************************************/
int testRedirection(char* buffer)
{
	char* outOp = " > ";
	char* inOp = " < ";

	//contains both
	if(strstr(buffer, outOp) != NULL && strstr(buffer, inOp) != NULL)
	{
		return 3;
	}
	
	//contains only >
	else if(strstr(buffer, outOp) != NULL && strstr(buffer, inOp) == NULL)
	{
		return 2;
	}

	//contains only <
	else if(strstr(buffer, outOp) == NULL && strstr(buffer, inOp) != NULL)
	{
		return 1;
	}

	//contains no redirection operators
	else
	{
		return 0;
	}
}

/*********************************************************************
** Function: getNewPath
** Description: splits redirection paths off parameter "buffer" and 
stores those paths in the appropriate path string, iPath or oPath.
** Parameters: int bgBool - either 1 or 0. if 1, function will remove the 
trailing & char from the end of the string. int rFlag - contains 1, 2, or 3. 
if 1, contains only stdin redirection operator, if 2, only stdout redirection
operator, and if 3, contains both. char** buffer - pointer to full string 
received from user. char** iPath - pointer to string where stdin redirection
path will be store. char** oPath - same as iPath, but for stdout
** Pre-Conditions: bgBool is valid and correct, rFlag is valid and correct,
char** buffer has a valid string containing <, >, or both.
** Post-Conditions: iPath and/or oPath point to strings used for redirection
in main
*********************************************************************/
void getNewPath(int bgBool, int rFlag, char** buffer, char** iPath, char** oPath)
{	
	char* tempString;
 	char* inDelim = "<";
 	char* outDelim = ">";
 	char* command = *buffer;

	switch(rFlag)
	{
		// < inPath
		case 1:
			//split redirection off buffer and store in iPath
			*iPath = strstr(command, inDelim);
			*iPath += 2; //move past "< "
			(*iPath)[strcspn(*(iPath), "\n")] = 0;

				//if background, remove trailing &
				if(bgBool == 1)
				{
					(*iPath)[(strcspn(*(iPath), "&"))-1] = 0;
				}

			//reduce buffer to just the meat
 			*buffer = strtok(command, inDelim);
 			(*buffer)[strcspn(*(buffer), " ")] = 0; //get rid of trailing space
		break;

		// > outPath
		case 2:
			//split redirection off buffer and store in oPath
			*oPath = strstr(command, outDelim);
			*oPath += 2; //move past "< "
			(*oPath)[strcspn(*(oPath), "\n")] = 0; //get rid of trailing newline

				//if background, remove trailing &
				if(bgBool == 1)
				{
					(*oPath)[(strcspn(*(oPath), "&"))-1] = 0;
				}

			//reduce buffer to just the meat
 			*buffer = strtok(command, outDelim);
 			(*buffer)[strcspn(*(buffer), " ")] = 0; //get rid of trailing space
		break;

		// > and <
		case 3:
			tempString = strstr(command, inDelim);

			//tempString contains only inDelim, meaning inDelim is at the end
			//of the string. iPath can be safely set to tempString
			if(strstr(tempString, outDelim) == NULL)
			{
				//set iPath
				*iPath = tempString;
				*iPath += 2; //move past "< "
				(*iPath)[strcspn(*(iPath), "\n")] = 0; //could maybe put this as an else after next if

					//if background, remove trailing &
					if(bgBool == 1)
					{
						(*iPath)[(strcspn(*(iPath), "&"))-1] = 0;
					}

				//cut inDelim path off command
				*buffer = strtok(command, inDelim);

				//get oPath
				*oPath = strstr(*buffer, outDelim);
				*oPath += 2; //move past "> "
				(*oPath)[strcspn(*(oPath), " ")] = 0; //get rid of trailing space

				//cut outDelim path off command
				*buffer = strtok(command, outDelim);
				(*buffer)[strcspn(*(buffer), " ")] = 0; //get rid of trailing space
			}

			//else, tempString contains both redirection paths and outDelim is at end
			else
			{
				//save oPath from tempString
				*oPath = strstr(tempString, outDelim);
				*oPath += 2; //move past "> "
				(*oPath)[strcspn(*(oPath), "\n")] = 0; //get rid of trailing newline

				//if background, remove trailing &
					if(bgBool == 1)
					{
						(*oPath)[(strcspn(*(oPath), "&"))-1] = 0;
					}

				//cut oPath off tempString and save iPath
				*iPath = strtok(tempString, outDelim);
				*iPath += 2; //move past "< "
				(*iPath)[strcspn(*(iPath), " ")] = 0; //get rid of trailing space

				//cut everything after < off and set remainder to buffer
				*buffer = strtok(command, "<");
				(*buffer)[strcspn(*(buffer), " ")] = 0; //get rid of trailing space

			}
		break;

		default:
			//fatal error
		break;
	}
}

/*********************************************************************
** Function: addBgPid
** Description: searches for the first element in bgArr[] set to 0, which
designates that that element is empty. Adds the value in pid to that element
** Parameters: int pid, the pid of a background child process. int bgArr[],
int array holding pids of currently running background children.
** Pre-Conditions: pid is a valid pid. bgArr[] has room to store pid
** Post-Conditions: pid has been added to bgArr[] if there is room. if not,
it is not added. **bgArr will need to be changed to a dynamic array or 
linked list if it is anticipated that more than 50 background processes will
be concurrently run.
*********************************************************************/

void addBgPid(int pid, int bgArr[])
{
	int i = 0, flag = 0;
	
	// add pid to firs empty value of bgArr
	while(i < 50 && flag == 0)
	{
		if(bgArr[i] == 0)
		{
			bgArr[i] = pid;
			flag = 1;
		}
	}
}

/*********************************************************************
** Function: checkBgKids
** Description: loops through elements in bgArr which are background
child pids. Calls waitpid on each element with NOHANG. if child has 
finished, outputs exit status information and then resets that element
value to 0
** Parameters: int bgArr[], an int array holding the pids of background
children. 
** Pre-Conditions: pids in bgArr[] are valid bg children
** Post-Conditions: if any background children have finished execution, 
an informative message is sent to stdout along with their exit value or 
termination signal.
*********************************************************************/

void checkBgKids(int bgArr[])
{
	int i, status, wpid;
	
	for(i = 0; i < 50; i++)
	{
		if(bgArr[i] != 0)
		{
			wpid = waitpid(bgArr[i], &status, WNOHANG);	

			if(wpid != 0)
			{	
				if (WIFEXITED(status)) 
				{
	        		printf("background pid %d is done: exit value %d\n", bgArr[i], WEXITSTATUS(status));
	        		fflush(stdout);
	   		 	}
	    		
	    		else if (WIFSIGNALED(status)) 
	    		{
	       			printf("background pid %d is done: terminated by signal %d\n", bgArr[i], WTERMSIG(status));
	       			fflush(stdout);
	   			}
	
	   			bgArr[i] = 0;
	   		}
		}
	}
}


