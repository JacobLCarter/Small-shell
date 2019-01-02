/*******************************************************************************
* Program Name: smallsh 
* Author: Jacob Carter
* Date Modified: 11/15/2018
* Course: CS344
* Description: A small shell written in C, that performs similarly to bash. 
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

//define global constant variables
#define ALLOWEDLENGTH 2048
#define AMOUNTARGUMENTS 512

//global path declaration for the home folder
const char *PATH = "HOME";
//global for checking background-only mode
int restrictedBG = 0;
//global to hold the status for reporting with "status" command
int status = 0;

//a struct to hold all background processes
struct LiveBGProcesses 
{
    int numProcesses;
    pid_t processList[256];
};

//struct to hold the parsed out information taken from the user's input
struct UserInput
{
    char command[ALLOWEDLENGTH];
    int includedArguments;
    char *arguments[AMOUNTARGUMENTS];
    char *inFile;
    char *outFile;
    int isBackgrounded;
};

/*******************************************************************************
* Function Name: initBGProcessList 
* Description: fills the process list with "marked" (garbage) values befor use 
* Arguments: pointer to a struct 
* Return: none 
*******************************************************************************/
void initBGProcessList(struct LiveBGProcesses *live)
{
    live->numProcesses = 0;

    for (int i = 0; i < 256; i++)
    {
	    live->processList[i] = -5;
    }
}

/*******************************************************************************
* Function Name: addBGProcess 
* Description: adds a process to the list of currently running processes 
* Arguments: pointer to a struct, the process id to add 
* Return: none 
*******************************************************************************/
void addBGProcess(struct LiveBGProcesses *live, pid_t pid)
{
    live->processList[live->numProcesses] = pid;
    live->numProcesses++;
}

/*******************************************************************************
* Function Name: removeBGPid 
* Description: removes a process from the list of currently running processes 
* Arguments: pointer to a struct, integer 
* Return: none 
*******************************************************************************/
void removeBGPid(struct LiveBGProcesses *live, int index)
{
    int i;

    //reset the process at the specified index
    live->processList[index] = -5;
    live->numProcesses--;

    //move each following process to fill the space from the removed process
    for (i = index; i < live->numProcesses; i++)
    {
        live->processList[i] = live->processList[i + 1];
    }
}

/*******************************************************************************
* Function Name: killAllBGProcesses 
* Description: kills all currently running background processes 
* Arguments: pointer to a struct
* Return: none 
*******************************************************************************/
void killAllBGProcesses(struct LiveBGProcesses *live)
{
    for (int i = 0; i < live->numProcesses - 1; i++)
    {
	    kill(live->processList[i], SIGINT);
    }
}

/*******************************************************************************
* Function Name: wipeInputObj 
* Description: clears all data from a struct UserInput object 
* Arguments: pointer to a struct
* Return: none 
*******************************************************************************/
void wipeInputObj(struct UserInput *toWipe)
{
    memset(toWipe->command, '\0', sizeof(toWipe->command));
    toWipe->inFile = "";
    toWipe->outFile = "";
    memset(toWipe->arguments, '\0', sizeof(toWipe->arguments));
    toWipe->includedArguments = 0;
    toWipe->isBackgrounded = 0;
}

/*******************************************************************************
* Function Name: printPrompt 
* Description: prints the shell prompt to the screen 
* Arguments: none 
* Return: none 
*******************************************************************************/
void printPrompt()
{
    printf(": ");
    
    //flush the buffers to ensure text reaches the screen
    fflush(stdin);
    fflush(stdout);
}

/*******************************************************************************
* Function Name: catchSIGINT 
* Description: handles the signal sent by ^C 
* Arguments: integer 
* Return: none 
*******************************************************************************/
void catchSIGINT(int sig)
{
    char* message = "Caught SIGINT\n";

    //print the message to the screen
    write(STDOUT_FILENO, message, 15);
}

/*******************************************************************************
* Function Name: catchSIGINT 
* Description: handles the signal sent by ^C 
* Arguments: integer 
* Return: none 
*******************************************************************************/
void catchSIGSTOP(int sig)
{
    char* message;

    //if already in foreground-only mode, switch to normal mode
    if (restrictedBG == 1)
    {
        message = "Caught SIGSTOP, Stopping foreground-only mode\n";
        restrictedBG = 0;
    }
    //if in normal mode, switch to foreground-only mode
    else
    {
        message = "Caught SIGSTOP, Starting foreground-only mode\n";
        restrictedBG = 1;
    }

    //print the respective message to the screen
    write(STDOUT_FILENO, message, 47);
}

/*******************************************************************************
* Function Name: chechBackground 
* Description: helper that checks if foreground-only mode is set 
* Arguments: none 
* Return: none 
*******************************************************************************/
void checkBackground()
{
    //if switch signal is sent and foreground-only mode active, switch to normal
    if (WTERMSIG(status) == 11 && restrictedBG == 1)
    {
        printf("Exiting foreground-only mode\n");
        restrictedBG = 0;
    }
    //if switch signal is sent and normal mode active, switch to foreground-only
    else if (WTERMSIG(status) == 11 && restrictedBG == 0)
    {
        printf("Entering foreground-only mode\n");
        restrictedBG = 1;
    }
}

/*******************************************************************************
* Function Name: changeDirectory 
* Description: Changes the present working directory to the passed parameter 
* Arguments: string indicating new directory 
* Return: none 
*******************************************************************************/
int changeDirectory(char* whereTo)
{
    //the path to the home directory
    char* path = getenv(PATH);
    int error;

    //if no directory specified, go to home directory 
    if (whereTo == NULL)
    {
        error = chdir(path); 
    }
    else
    {
        error = chdir(whereTo);
    }

    //print an error if changing to the new directory failed 
    if (error < 0)
    {
        printf("Cannot find specified directory: %s\n", whereTo);
        return 1;
    }

    return 0;
}

/*******************************************************************************
* Function Name: checkComment 
* Description: helper function that identifies if user input is a comment 
* Arguments: string
* Return: none 
*******************************************************************************/
int checkComment(char *stringToCheck)
{
    int comment = 0;

    //execute if the input is not empty
    if (stringToCheck != NULL)
    {
        //if the input start with a pound, it is a comment
        if (stringToCheck[0] == '#')
        {
            comment = 1;
        }
    }

    return comment;
}

/*******************************************************************************
* Function Name: parseInput 
* Description: splits a string of user input into useable chunks 
* Arguments: struct object, string
* Return: none 
*******************************************************************************/
void parseInput(struct UserInput *obj, char *input)
{
    //buffer to hold pieces of the chopped up input
    char *buffer;
    //buffer to hold the entirety of the user input
    char inBuffer[ALLOWEDLENGTH];

    //remove the new line included with the user's input
    input[strlen(input) - 1] = '\0';

    //copy the input into the temporary buffer to be cut up
    strcpy(inBuffer, input); 

    //cut everything before the first space into the buffer
    buffer = strtok(inBuffer, " ");
    //add this value to the objects command member
    strcpy(obj->command, buffer);

    //cut everything until the next space, starting where left off
    buffer = strtok(NULL, " ");
   
    //continue to parse the string until there is nothing left
    while (buffer != NULL) 
    {
        //identifies and fills the name of an input file
        if (strcmp(buffer, "<") == 0)
        {
            buffer = strtok(NULL, " ");
            obj->inFile = buffer;
        }
        //identifies and fills the name of an output file
        else if (strcmp(buffer, ">") == 0)
        {
            buffer = strtok(NULL, " ");
            obj->outFile = buffer;
        }
        //identifies if a process should be started in the background
        else if (strcmp(buffer, "&") == 0)
        {
            obj->isBackgrounded = 1;
        }
        //adds anything not caught by the above categories as an argument
        else
        {
            obj->arguments[obj->includedArguments] = buffer;
            obj->includedArguments++;
        }

        //cut everything until the next space, starting where left off
        buffer = strtok(NULL, " ");
    }
}

/*******************************************************************************
* Function Name: redirect 
* Description: creates file redirects for a new process, if needed 
* Arguments: struct object
* Return: none 
*******************************************************************************/
void redirect(struct UserInput *input)
{
    //success indicators
    int sourceFD, targetFD, result;

    //execute if an input file is specified
    if (strcmp(input->inFile, "") != 0)
    {
        //attempt to open the specified file
        sourceFD = open(input->inFile, O_RDONLY);
       
        if (sourceFD == -1)
        {
            perror("Source open()");
            exit(1);
        }
        else
        {
            //duplicate the file descriptor
            result = dup2(sourceFD, 0);

            if (result == -1)
            {
                perror("Source dup2()\n");
                exit(2);
            }
        }
    }

    if (strcmp(input->outFile, "") != 0)
    {
        targetFD = open(input->outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
       
        if (targetFD == -1)
        {
            perror("Target open()");
            exit(1);
        }
        else
        {
            result = dup2(targetFD, 1);
            
            if (result == -1)
            {
                perror("Source dup2()\n");
                exit(2);
            }
        }
    }
}

/*******************************************************************************
* Function Name: setExecArguments 
* Description: creates an argument list that will be passed into an exec() call 
* Arguments: struct object, pointer to a string
* Return: none 
*******************************************************************************/
void setExecArguments(struct UserInput *input, char **arguments)
{
    int index = 0;
    int i;

    //set the first argument to the command to run
    arguments[index] = input->command;
    index++;
    
    for (i = 0; i < input->includedArguments; i++)
    {
        if (getenv(input->arguments[i]) != NULL)
        {
            arguments[index] = getenv(input->arguments[i]);
            index++;
        }
        //expand to the process id of the shell itself if given "$$"
        else if (strcmp(input->arguments[i], "$$") == 0)
        {
            sprintf(arguments[index], "%d", getpid());
            index++;
        }
        else
        {
            arguments[index] = input->arguments[i];
            index++;
        }
    }

    arguments[index] = NULL;
}

/*******************************************************************************
* Function Name: forkChild 
* Description: creates a child process and executes the command given
* Arguments: struct object, struct object 
* Return: none 
*******************************************************************************/
void forkChild(struct UserInput *input, struct LiveBGProcesses *bgList)
{
    //fork a new process
    pid_t pid = fork();
    //hold the arguments that will be passed into exec()
    char *argumentsIn[AMOUNTARGUMENTS];
   

    switch(pid)
    {
        //indicates that fork failed; exit with a message
        case -5:
            printf("Unable to fork\n");
            exit(1);
            break;
        //indicates that fork succeeded; create arguments list and execute command
        case 0:
            //setup redirects
            redirect(input);
            //setup arguments for exec()
            setExecArguments(input, argumentsIn);
            //execute the new command
            execvp(argumentsIn[0], argumentsIn);
            //will only reach this line if the above execution fails
            printf("Command not found\n");
            exit(1);
            break;
        //indicates parent process
        default:
            if (input->isBackgrounded == 1 && restrictedBG != 1) 
            {
                //add a background process to the list and print it to the screen
                addBGProcess(bgList, pid);
                printf("Background PID: %d\n", pid); 
            }
            else 
            {
                //wait for the foreground process to finish
                waitpid(pid, &status, 0);
            }
            break;
    }
}

/*******************************************************************************
* Function Name: startShell 
* Description: runs the shell itself 
* Arguments: none 
* Return: none 
*******************************************************************************/
void startShell()
{
    //will hold the user's input
    char buffer[ALLOWEDLENGTH];
    
    //create an object to hold the parsed input and a pointer to reference it
    struct UserInput obj;
    struct UserInput *inputObj = &obj;

    //create an object to hold the list of live background processes and a 
    //pointer to reference it
    struct LiveBGProcesses bg;
    struct LiveBGProcesses *background = &bg;

    //clear the object that will hold the user input
    wipeInputObj(inputObj);

    //clear and set-up the process list
    initBGProcessList(background);

    //initialize sigaction structs to be empty
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};
   
    //set the function to call if ^C is received
    SIGINT_action.sa_handler = catchSIGINT;
    //block all other arriving signals
    sigfillset(&SIGINT_action.sa_mask);
    //do not provide any additional flags
    SIGINT_action.sa_flags = 0;

    SIGTSTP_action.sa_handler = catchSIGSTOP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;

    do
    {
        sigaction(SIGINT, &SIGINT_action, NULL);
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

        //check if foreground-only mode is set
        checkBackground();

        //print the main shell prompt
        printPrompt();

        memset(buffer, '\0', sizeof(buffer));

        //write the user input into the buffer
        fgets(buffer, sizeof(buffer), stdin);

        //re-prompt if empty or user entered a comment
        if (checkComment(buffer) == 1 || buffer[0] == '\n')
        {
            continue;
        }
        else
        {
            //break the string into a UserInput object
            parseInput(inputObj, buffer);

            //change directory if the command "cd" is matched
            if (strcmp(inputObj->command, "cd") == 0)
            {
                changeDirectory(inputObj->arguments[0]);
            }
            //give the current status if the command is matched
            else if (strcmp(inputObj->command, "status") == 0)
            {
                //executes if an exit status has been set
                if (WEXITSTATUS(status))
                {
                    status = WEXITSTATUS(status);
                    printf("exit value %d\n", status);
                }
                //executes if a termination signal has been given
                else
                {
                    printf("terminated by signal %d\n", status);
                }
            }
            //exit the shell if the command is matched
            else if (strcmp(inputObj->command, "exit") == 0)
            {
                killAllBGProcesses(background);
                exit(0);
            }
            //create a child process and try to execute the given command
            else
            {
                forkChild(inputObj, background);
            }
        }
   
        //clear the UserInput object for use on the next pass
        wipeInputObj(inputObj);
        
    }
    while (1);
}

int main()
{
    startShell();

    return 0;
}
