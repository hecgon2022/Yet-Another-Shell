// Include neccessary headers
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>

// JOB STRUCT-----------------------------------------------------------------------------------------------------------------------------------------
typedef struct job_
{
    // pids and status
    pid_t pgid;
    pid_t cpid;

    int status;

    // Values for job control
    int fg, bg, jobs, background, needPipe;
    char *jobStatus;
    char *command;

    // Values from parser
    char **commandArray;
    char **leftCmd;
    char **rightCmd;
} job;

// jobNODE STRUCT AND FUNCTIONS ------------------------------------------------------------------------------------------------------------------------------
// Job Node Struct
typedef struct jobNode_
{
    // Basic jobNode vars
    pid_t pgid;
    int jobId;
    int doubleChild;
    int hasBeenReaped;
    int status;
    int background;
    char *statusString;
    char *command;

    // jobNode ptr to next (makes this a singly linked list)
    struct jobNode_ *nextJobNode;

} jobNode;

// Some necessary global vars
#define MAX_JOBS 20
volatile jobNode *root = NULL;
volatile int jobNum = 0;
volatile int jobIdTracker = 1;

// Add job function
void addJob(jobNode *newJobNode)
{

    // The stack is full
    if (jobNum >= 20)
    {
        printf("The job stack is full\n");
        return;
    }

    // The stack is empty, so just add at the root
    if (root == NULL)
    {
        root = newJobNode;
    }

    else
    {
        // Iterate until we are at the end of list (aka top of stack)
        volatile jobNode *currNode = root;
        while (currNode->nextJobNode != NULL)
        {
            currNode = currNode->nextJobNode;
        }

        // Add our newJobNode here
        currNode->nextJobNode = newJobNode;
    }

    // Increment our jobs
    jobNum++;
    newJobNode->jobId = jobIdTracker++;
}

void removeJob(int pgid, jobNode *currNode, jobNode *prevNode)
{

    // The stack is empty
    if (jobNum <= 0)
    {
        return;
    }

    // Iterate until we find the jobID
    while (currNode != NULL)
    {

        // Check for pgid
        if (currNode->pgid == pgid)
        {
            // The root is the node to delete
            if (prevNode == NULL)
            {
                // If there is an element after root, then move it to the root
                if (root->nextJobNode != NULL)
                {
                    root = root->nextJobNode;
                }
                else
                {
                    root = NULL;
                }
            }

            // The node to delete is not the root
            else
            {
                prevNode->nextJobNode = currNode->nextJobNode;
            }

            jobNum--;
            free(currNode);
        }

        // Increment up our list
        prevNode = currNode;
        currNode = currNode->nextJobNode;
    }
}

void printJobs()
{

    // If the stack is empty, just leave
    if (jobNum <= 0)
    {
        return;
    }

    // Iterate and print our job list
    volatile jobNode *currNode = root;
    int i = 1;
    while (currNode->nextJobNode != NULL)
    {
        printf("[%d] - %s\t%s\n", currNode->jobId, currNode->statusString, currNode->command);
        currNode = currNode->nextJobNode;
    }

    // Print our top of stack job
    printf("[%d] + %s\t%s\n", currNode->jobId, currNode->statusString, currNode->command);
}

// SIGHANDLER FUNCTION-----------------------------------------------------------------------------------------------------------------------------
void sigHandler(int signum)
{

    volatile jobNode *currNode = root;

    if (signum == SIGINT)
    {

        // TODO: Implement some checker if no fg job is running!
        if (currNode == NULL)
        {
            return;
        }

        // Find our most recent job (fg takes control of terminal so most recent is what we want)
        while (currNode->nextJobNode != NULL)
        {
            currNode = currNode->nextJobNode;
        }

        if (currNode->background != 0)
        {
            return;
        }

        // We send out the SIGINT signal to the process
        kill(-currNode->pgid, SIGINT);
    }

    else if (signum == SIGTSTP)
    {

        // TODO: Implement some checker if no fg job is running!
        if (currNode == NULL)
        {
            return;
        }

        // Find our most recent job (fg takes control of terminal so most recent is what we want)
        while (currNode->nextJobNode != NULL)
        {
            currNode = currNode->nextJobNode;
        }

        if (currNode->background != 0)
        {
            return;
        }

        // We send out the SIGTSTP
        int valid = kill(-currNode->pgid, SIGTSTP);
        currNode->statusString = "STOPPED";
    }

    else if (signum == SIGCHLD)
    {

        while (currNode != NULL)
        {
            if (currNode->background)
            {
                int changedPid = waitpid(-currNode->pgid, 0, WNOHANG);

                if (currNode->pgid == changedPid || currNode->pgid + 1 == changedPid)
                {

                    if (currNode->doubleChild == 1)
                    {
                        if (currNode->hasBeenReaped == 0)
                        {
                            currNode->hasBeenReaped = 1;
                        }
                        else
                        {
                            printf("\n[%d] + DONE\t%s\n", currNode->jobId, currNode->command);
                            removeJob(currNode->pgid, (jobNode *)root, NULL);
                        }
                    }
                    else
                    {
                        printf("\n[%d] + DONE\t%s\n", currNode->jobId, currNode->command);
                        removeJob(currNode->pgid, (jobNode *)root, NULL);
                    }
                }
            }
            currNode = currNode->nextJobNode;
        }
    }
}

// FLAG_FD FUNCTION--------------------------------------------------------------------------------------------------------------------------------
// Function that checks for fg, bg, jobs, &, and does file redirection (Updates commandArray respectively based on file redirection)
int flagAndFd_Checker(job *jobStruct, char **runCommand, int type)
{

    // Check if we are doing leftCmd, rightCmd, or commandArray
    char **checkArray;

    if (type == 0)
    {
        checkArray = jobStruct->commandArray;
    }
    else if (type == 1)
    {
        checkArray = jobStruct->leftCmd;
    }
    else if (type == 2)
    {
        checkArray = jobStruct->rightCmd;
    }

    int ofd;

    // Iterate over our commandArray
    int i = 0;
    while (checkArray[i])
    {
        // Check for < (stdin change)
        if (strcmp(checkArray[i], "<") == 0)
        {
            ofd = open(checkArray[i + 1], O_RDONLY);
            if (ofd == -1)
            {
                return -1;
            }
            dup2(ofd, STDIN_FILENO);
        }

        // Check for > (stdout change)
        else if (strcmp(checkArray[i], ">") == 0)
        {
            ofd = creat(checkArray[i + 1], 0644);
            dup2(ofd, STDOUT_FILENO);
        }

        // Check for 2> (stderr change)
        else if (strcmp(checkArray[i], "2>") == 0)
        {
            ofd = creat(checkArray[i + 1], 0644);
            dup2(ofd, STDERR_FILENO);
        }

        // Dont add that backgroud flag for successful command running
        else if (strcmp(checkArray[i], "&") == 0)
        {
            // Do Nothing;
        }

        // None of the above were found, so just normal command or args
        else
        {
            runCommand[i] = malloc(sizeof(char) * 30);
            runCommand[i] = checkArray[i];
        }

        i++;
    }

    free(checkArray);
    return 0;
}

// PARSER FUNCTION--------------------------------------------------------------------------------------------------------------------------------
// Parser Function that obtains a string and breaks it down into an array of strings ending with a null terminator
void parser(char *input, job *nextJob)
{

    // Copy over whole command from input to nextJob->command
    strcpy(nextJob->command, input);

    // Check for the fg, bg, and jobs command before trying to create our commandArray
    if (strcmp(nextJob->command, "fg") == 0)
    {
        nextJob->fg = 1;
        return;
    }

    if (strcmp(nextJob->command, "bg") == 0)
    {
        nextJob->bg = 1;
        return;
    }

    if (strcmp(nextJob->command, "jobs") == 0)
    {
        nextJob->jobs = 1;
        return;
    }

    // Declare some necessary vars for strtok_r and nextJob
    char *token, *refInput, *refInputFree, *savePtr;
    int i, pipeIndex;
    nextJob->needPipe = 0;
    refInput = refInputFree = strdup(input);

    // Use strtok to parse the input string into a white space delimmeted array of strings
    i = 0;
    while ((token = strtok_r(refInput, " ", &savePtr)) != NULL)
    {
        // Allocate to the size of 30 character for each potential command
        nextJob->commandArray[i] = malloc(sizeof(char) * 30);
        nextJob->commandArray[i] = token;

        // Check if that token is a pipe ( | )
        if (strcmp(token, "|") == 0)
        {
            nextJob->needPipe = 1;
            pipeIndex = i;
        }

        i++;
        refInput = NULL;
    }

    // Ensure we have it null terminated
    nextJob->commandArray[i] = NULL;

    // Check if it is a background command
    if (strcmp(nextJob->commandArray[i - 1], "&") == 0)
    {
        nextJob->background = 1;
    }

    // Check if needPipe has been set to 1
    if (nextJob->needPipe == 1)
    {

        // Iterate for our leftCmd
        int j = 0;
        while (j < pipeIndex)
        {
            nextJob->leftCmd[j] = malloc(sizeof(char) * 30);
            nextJob->leftCmd[j] = nextJob->commandArray[j];
            j++;
        }
        nextJob->leftCmd[j] = NULL;

        // Iterate for our rightCmd
        int r = 0;
        j++;
        while (nextJob->commandArray[j])
        {
            nextJob->rightCmd[r] = malloc(sizeof(char) * 30);
            nextJob->rightCmd[r] = nextJob->commandArray[j];
            r++, j++;
        }
        nextJob->rightCmd[r + 1] = NULL;
    }

    // Should we free our refInputFree here? (Fucks everything up if I free, why?)
    // free(refInputFree);
}

// MAIN FUNCTION-----------------------------------------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{

    // Register our signal handlers
    signal(SIGINT, sigHandler);
    signal(SIGTSTP, sigHandler);
    signal(SIGCHLD, sigHandler);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    // String that contains a potential command that has not been parsed
    char *nextInput;

    // 1. Grab and parse the input
    // Readline line prints (# ) and awaits for an enter to signify the end of typing from user
    while (nextInput = readline("# "))
    {

        // Create a new parsedInput struct for our new input line and allocate all memory for it
        job *nextJob;
        nextJob = malloc(sizeof(job));
        nextJob->command = malloc(sizeof(char) * 2000);
        nextJob->commandArray = malloc(sizeof(*nextJob->commandArray) * 2000);
        nextJob->leftCmd = malloc(sizeof(*nextJob->leftCmd) * 2000);
        nextJob->rightCmd = malloc(sizeof(*nextJob->rightCmd) * 2000);
        nextJob->background = 0;
        nextJob->fg = 0;
        nextJob->bg = 0;
        nextJob->jobs = 0;

        // Now we actually parse our command by breaking it down into a 2d array of null terminated strings
        if (strcmp(nextInput, "") == 0)
        {
            continue;
        }
        parser(nextInput, nextJob);

        // Run our fg command
        if (nextJob->fg)
        {

            if (root == NULL)
            {
                continue;
            }

            volatile jobNode *currNode = root;
            volatile jobNode *fgNode = NULL;

            // Grab our most recent node that is stopped
            while (currNode != NULL)
            {

                // If stopped, then save to bg
                if (strcmp(currNode->statusString, "STOPPED") == 0 || currNode->background == 1)
                {
                    fgNode = currNode;
                }

                currNode = currNode->nextJobNode;
            }

            // Send the kill command to start our stopped process in the back
            if (fgNode->background = 1 && strcmp(fgNode->statusString, "RUNNING") == 0)
            {
                kill(-(fgNode->pgid), SIGTSTP);
                kill(-(fgNode->pgid), SIGCONT);
            }

            else
            {
                kill(-(fgNode->pgid), SIGCONT);
            }

            fgNode->statusString = "RUNNING";
            fgNode->background = 0;

            // Print our job
            printf("[%d] + %s\t%s\n", fgNode->jobId, fgNode->statusString, fgNode->command);

            // Blocking call to wait for the foreground job to finish
            waitpid(-fgNode->pgid, (int *)&fgNode->status, WUNTRACED);

            if (WIFSIGNALED(fgNode->status) || WIFEXITED(fgNode->status))
            {
                removeJob(fgNode->pgid, (jobNode *)root, NULL);
            }

            continue;
        }

        // Run our bg command
        if (nextJob->bg)
        {
            if (root == NULL)
            {
                continue;
            }

            volatile jobNode *currNode = root;
            volatile jobNode *bgNode = NULL;

            // Grab our most recent node that is stopped
            while (currNode != NULL)
            {
                // If stopped, then save to bg
                if (strcmp(currNode->statusString, "STOPPED") == 0)
                {
                    bgNode = currNode;
                }

                currNode = currNode->nextJobNode;
            }

            // Send the kill command to start our stopped process in the back
            bgNode->background = 1;
            bgNode->statusString = "RUNNING";
            kill(-(bgNode->pgid), SIGCONT);

            // Print our job
            printf("[%d] + %s\t%s\n", bgNode->jobId, bgNode->statusString, bgNode->command);

            continue;
        }

        // Run our jobs command
        if (nextJob->jobs)
        {
            printJobs();
            continue;
        }

        // Double Child Exectuion
        if (nextJob->needPipe)
        {
            // Create our pipefd array
            int pipefd[2];
            pipe(pipefd);
            int cpid1, cpid2;

            // Allocate a new array of strings for our command (This is for the case of file redirection)
            char **runCommandLeft;
            runCommandLeft = malloc(sizeof(*runCommandLeft) * 2000);
            char **runCommandRight;
            runCommandRight = malloc(sizeof(*runCommandRight) * 2000);

            // Create our jobNode struct
            jobNode *newJob;
            newJob = malloc(sizeof(jobNode));
            newJob->command = nextJob->command;
            newJob->statusString = "RUNNING";
            newJob->status = 0;
            newJob->nextJobNode = NULL;
            newJob->background = nextJob->background;
            newJob->doubleChild = 1;
            newJob->hasBeenReaped = 0;

            // Add our jobNode to the linked list
            addJob(newJob);

            cpid1 = fork();
            newJob->pgid = cpid1;
            if (cpid1 == 0)
            {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);

                // Set our pgid
                setpgid(0, 0);

                // Check and run
                int valid = flagAndFd_Checker(nextJob, runCommandLeft, 1);

                if (valid != -1)
                {
                    execvp(runCommandLeft[0], runCommandLeft);
                }

                removeJob(newJob->pgid, (jobNode *)root, NULL);

                exit(0);
            }

            // usleep(10000);

            cpid2 = fork();
            if (cpid2 == 0)
            {
                close(pipefd[1]);
                dup2(pipefd[0], STDIN_FILENO);

                // Set our pgid for right child
                setpgid(0, cpid1);

                // Check and run
                int valid = flagAndFd_Checker(nextJob, runCommandRight, 2);

                if (valid != -1)
                {
                    execvp(runCommandRight[0], runCommandRight);
                }

                removeJob(newJob->pgid, (jobNode *)root, NULL);

                exit(0);
            }

            usleep(100000);

            // Close our pipe
            close(pipefd[0]);
            close(pipefd[1]);

            if (newJob->background)
            {
                waitpid(-newJob->pgid, &newJob->status, WNOHANG | WUNTRACED);
                waitpid(-newJob->pgid, &newJob->status, WNOHANG | WUNTRACED);
            }
            else
            {
                // Blocking call to wait for the foreground job to finish
                waitpid(-newJob->pgid, &newJob->status, WUNTRACED);
                newJob->hasBeenReaped = 1;
                waitpid(-newJob->pgid, &newJob->status, WUNTRACED);

                if (WIFSIGNALED(newJob->status) || WIFEXITED(newJob->status))
                {
                    removeJob(newJob->pgid, (jobNode *)root, NULL);
                }
            }

            free(runCommandLeft);
            free(runCommandRight);
            free(nextJob);

            continue;
        }

        // Single Child Execution
        else
        {

            // Allocate a new array of strings for our command (This is for the case of file redirection)
            char **runCommand;
            runCommand = malloc(sizeof(*runCommand) * 2000);

            // Create our jobNode struct
            jobNode *newJob;
            newJob = malloc(sizeof(jobNode));
            newJob->command = nextJob->command;
            // newJob->pgid = nextJob->pgid;
            newJob->statusString = "RUNNING";
            newJob->status;
            newJob->nextJobNode = NULL;
            newJob->background = nextJob->background;
            newJob->doubleChild = 0;
            newJob->hasBeenReaped = 0;

            // Add our jobNode to the linked list
            addJob(newJob);

            // Kick off our one child process
            nextJob->cpid = fork();
            newJob->pgid = nextJob->cpid;
            if (nextJob->cpid == 0)
            {

                // Set our pgid
                setpgid(0, 0);

                // Check and run
                int valid = flagAndFd_Checker(nextJob, runCommand, 0);
                if (valid != -1)
                {
                    execvp(runCommand[0], runCommand);
                }

                removeJob(newJob->pgid, (jobNode *)root, NULL);
                exit(0);
            }

            usleep(100000);

            // Wait protocol for foreground (Don't do any waiting for background)
            if (newJob->background)
            {
                waitpid(-newJob->pgid, &newJob->status, WNOHANG | WUNTRACED);
            }
            else
            {
                // Blocking call to wait for the foreground job to finish
                waitpid(-newJob->pgid, &newJob->status, WUNTRACED);
                if (WIFSIGNALED(newJob->status) || WIFEXITED(newJob->status))
                {
                    removeJob(newJob->pgid, (jobNode *)root, NULL);
                }
            }

            free(runCommand);
            free(nextJob);
        }
    }

    return 0;
}
