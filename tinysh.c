/****************************************************************************************************
 * Author: Keenan Taggart
 * Program: tinysh
 * Date: 2018
 * Description: A tiny shell. Takes commands, some of which are built in. The rest are exec()ed.
 *   Handles simple input/output redirection, PID expansion, and background processes. Handles/ignores
 *   some signals.
 ***************************************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BGPID_ARR_SIZE 20                     /* Size of array of background process IDs. 20 is more than ample for primitive use but an array is bad implementation */
                                              /* I.e. I would use a linked list if I wanted to make this more robust. An array works fine for the exercise though */
#define NUM_ARGS 513
#define PATH_STR_SZ 100


/* Despite being taught that global variables aren't a good idea, I can't think of a better way to implement 
 * flags like these other than putting them in a struct and passing them to nearly ever function, which just
 * seems silly. pidString is really just kind of laziness on my part, although it works just fine. */

char pidString[50];                           /* Used for expanding $$ */
int background = 0;                           /* Flag indicating if a process is supposed to run in the background. Flips if user enters & as final argument */
int disallow_background = 0;                  /* Flips when SIGTSTP is sent to process */
int background_inform = 0;                    /* Flips when SIGSTP is sent to process, unflips when user has been informed about background mode availability */



  /* * * * * * * * * * * * * * * * * * * * * * * * *
   *              Function definitions             *
   * * * * * * * * * * * * * * * * * * * * * * * * */  


/* Input: A signal number
 * Output: None
 * Description: Executes when a SIGTSTP is sent to the process. Flips the disallow_background flag
 *   and preps the background_inform flag */
void catchSIGTSTP(int signo) {
  if (disallow_background) {
    disallow_background = 0;
  } 
  else {
    disallow_background = 1;
  }
  background_inform = 1;
}

/* Input: None
 * Output: None
 * Description: If the background_inform flag is flipped, then print info to user based on condition 
 *   of disallow_background flag */
void backgroundInform() {
  if (background_inform && disallow_background) {
    printf("Entering foreground-only mode (& is now ignored)\n");
  }
  else if (background_inform && !disallow_background) {
    printf("Exiting foreground-only mode\n");
  }
  fflush(stdout);
  background_inform = 0;
}

/* Input: Array of PIDs for each extant background process
 * Output: None
 * Description: Iterates through array of background PIDs. If any are complete, print their exit
 *   or termination status as well as their PID, and then remove them from the PID array */
void resolveBGPID(pid_t pids[BGPID_ARR_SIZE]) {
  int i, stat;
  for (i = 0; i < BGPID_ARR_SIZE; i++) {                                                                /* Go through array of background PIDs and check each one for completion */
    if (pids[i]) {                            
      if (waitpid(pids[i], &stat, WNOHANG)) {                                                           /* waitpid returns pid of child process if complete, 0 otherwise */
        printf("background pid %d is done: ", pids[i]);                                                 /* print PID of finished background process */
        if (WIFEXITED(stat)) {
          printf("exit value %d\n", WEXITSTATUS(stat));                                                 /* If process exited normally, print exit value */
        }
        else {
          printf("terminated by signal %d\n", WTERMSIG(stat));                                          /* If process was terminated by a signal, print num of signal */
        }
        fflush(stdout);
        pids[i] = 0;                                                                                    /* Since process is finished, remove it from array of background processes */
      }
    } 
  }
}

/* Input: Pointer to input string, array of background process PIDs
 * Output: None
 * Description: Prints any necessary messages to the user, and then prompts for input */
void getCommand(char **userInput, pid_t backgroundPIDs[BGPID_ARR_SIZE]) {
  int numChars = -5;
  size_t buf = 0;

  while (1) {
    backgroundInform();
    resolveBGPID(backgroundPIDs);
    printf(": ");
    fflush(stdout);
    numChars = getline(userInput, &buf, stdin);
    if (numChars == -1) {
      clearerr(stdin);
    } 
    else if ((*userInput)[0] != '#' && numChars > 1) {
      break;
    }
  }
  (*userInput)[strlen(*userInput) - 1] = '\0';
}

/* Input: Array of background process PIDs, PID to be added to array
 * Output: None
 * Description: Iterates through array looking for an empty spot. Passed PID is placed in
 *   that spot */
void placePID(pid_t bgPIDs[BGPID_ARR_SIZE], pid_t addPID) {
  int i, placed = 0;
  for (i = 0; i < BGPID_ARR_SIZE; i++) {
    if (bgPIDs[i] == 0) {                                                                               /* pid 0 is used by a kernel process, so here it's a flag for an empty spot in the array */
      bgPIDs[i] = addPID; 
      placed = 1;
      break;
    }
  }
  if (!placed) {                                                                                        /* The array can only hold 20 background PIDs. Should use linked list */
    perror("Couldn't track background process.");                                                       /* If more capacity is needed, bump up BGPID_ARR_SIZE */
    exit(1);                                                                                            /* If not enough room, the PID is never logged and so is never checked (wait()ed) in resolveBGPID -- ergo, zombie */
  }                                                                                                     
}

/* Input: Array of background process PIDs
 * Output: None
 * Description: Assigns each value in the array to 0 -- the flag indicating that
 *   no PID is stored at that location */
void scrubPIDArray(pid_t pids[BGPID_ARR_SIZE]) {
  int i;
  for (i = 0; i < BGPID_ARR_SIZE; i++) {
    pids[i] = 0;
  }
}

/* Input: Array of background process PIDs
 * Output: None
 * Description: Iterates through array and sends a SIGKILL to each process it finds.
 *   The background processes ignore SIGTERM 
 * I could not pass up an opportunity to write a function as morbidly named as killChildren */
void killChildren(pid_t pids[BGPID_ARR_SIZE]) {
  int i;
  for (i = 0; i < BGPID_ARR_SIZE; i++) {                                                                /* Check all extant background processes */
    if (pids[i]) {                                                                                      /* If one exists then kill it */
      kill(pids[i], SIGKILL);
      pids[i] = 0;                                                                                      /* Scrubbing the array just to be safe */
    }
  }
}

/* Input: Input string, array of arguments, array of two strings to hold redirection arguments
 * Output: an integer representing the number of arguments
 * Description: Uses strtok to parse through the input string, storing each individual word
 *   (delimited by a space) in the argument array. Also performs PID expansion and stores
 *   any input/output arguments in the associated array */
int parseCommand(char *userInput, char *args[513], char *redir[2]) {
  int argCount = 0;
  char* token = strtok(userInput, " ");

  while (token) {
    if (strstr(token, "$$")) {                                                                          
      
      /* Only supports one expansion, big flaw. A version 1.1 would allocate a large string for input,
       * and then when expansion was needed, copy the remainder further in memory to make room for
       * $$ to be replaced by the pid */

      *(strstr(token, "$$")) = '\0';                                                                    
      sprintf(pidString, "%s%ld", token, (long) getpid());                                              
      args[argCount] = pidString;
    }
    else if (!strcmp(token, "<")) {                                                                     /* Check for input redirection */
      token = strtok(NULL, " ");                                                                        /* Next token is input source, put in redir array */
      redir[0] = token;
      argCount--;
    }
    else if (!strcmp(token, ">")) {                                                                     /* Check for output redirection */
      token = strtok(NULL, " ");                                                                        /* Next token is output destination, put in redir array */
      redir[1] = token;
      argCount--;
    }
    else {
      args[argCount] = token;
    }
    argCount++; 
    token = strtok(NULL, " ");
  }
  args[argCount] = NULL;

  if (!strcmp(args[argCount - 1], "&")) {                                                               /* If last argument is background command */
    if (!disallow_background) {                                                                         /* Check if background commands are allowed */
      background = 1;                                                                                   /* If they are, set background flag */
    }
    args[argCount - 1] = NULL;                                                                          /* Eliminate background command from array of arguments */
  }

  return argCount;
}

/* Input: None
 * Output: None
 * Description: Sets up the signal handler for SIGTSTP */
void prepSigHandler() {
  struct sigaction SIGTSTP_action = {0};
  SIGTSTP_action.sa_handler = catchSIGTSTP; 
  SIGTSTP_action.sa_flags = SA_RESTART;
  sigfillset(&SIGTSTP_action.sa_mask);
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
  signal(SIGINT, SIG_IGN);
}

/* Input: Array of background process PIDs, input string
 * Output: None
 * Description: Kills all the background process children, frees the input string. General
 *   cleanup before exiting the shell */
void exitShell(pid_t bgPIDs[BGPID_ARR_SIZE], char* userInput) {
  killChildren(bgPIDs);
  free(userInput);
  userInput = NULL;
  background = 0;                                                                                     
}

/* Input: integer value containing info/flags from waitpid()
 * Output: None
 * Description: Checks the passed integer for exit status or termination status and
 *   prints the associated information to the screen */
void statusShell(int lastStat) {
  if (WIFEXITED(lastStat)) {
    printf("exit value %d\n", WEXITSTATUS(lastStat));
  }
  else {
    printf("terminated by signal %d\n", WTERMSIG(lastStat));
  }
  fflush(stdout);
  background = 0;
}

/* Input: array of arguments, integer representing number of arguments
 * Output: None
 * Description: If number of arguments is 1, then changes directory to HOME
 *   directory (as indicated by the environment variable). Otherwise, uses
 *   the second argument as a destination
 *   NOTE: All other arguments are ignored. It would probably be better to
 *   issue an error in that scenario, good idea for future version */
void cdShell(char *args[NUM_ARGS], int argCount) {
  char path[PATH_STR_SZ];
  size_t pathSize = PATH_STR_SZ; 

  if (argCount == 1) {
    strcpy(path, getenv("HOME"));
  }
  else {
    strcpy(path, args[1]);
  }
  chdir(path);
  background = 0;
}

/* Input: Array with redirection arguments, array of arguments, pointer to file descriptor
 *   for input file, pointer to file descriptor for output file
 * Output: None
 * Description: Performs input redirection with dup2() if necessary, and then calls execvp()
 *   on the given arguments. Exits if exec fails */
void execChild(char *redir[2], char *args[NUM_ARGS], int *inputFile, int *outputFile) {
  if (redir[0]) {                                                                                       /* If input redirection is provided */
    *inputFile = open(redir[0], O_RDONLY);                                                              /* Open the provided argument */
    if (*inputFile == -1) {
      perror(redir[0]);
      exit(1);
    }
    dup2(*inputFile, 0);                                                                                /* Switch stdin to provided input file */
  }
  else if (background) {                                                                                /* If no input redirection was provided AND proc is running in background, open /dev/null for input */
    *inputFile = open("/dev/null", O_RDWR);
    dup2(*inputFile, 0);
  }

  if (redir[1]) {                                                                                       /* If output redirection is provided */
    *outputFile = open(redir[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);                                   /* Open the provided argument */
    if (*outputFile == -1) {
      perror(redir[1]);
      exit(1);
    }
    dup2(*outputFile, 1);                                                                               /* Switch stdout to provided output file */
  }
  else if (background) {                                                                                /* If no output redirection provided AND proc in background, open /dev/null for output */
    if (!redir[0]) {
      *outputFile = *inputFile;                                                                         /* If already using /dev/null for input, just use same file descriptor */
    }
    else {
      *outputFile = open("/dev/null", O_WRONLY);
    }
    dup2(*outputFile, 1);
  }

  if (!background) {                                                                                    /* If foreground proc, ensure it has default behavior for interrupt signal */
    signal(SIGINT, SIG_DFL);
  }
  if (execvp(args[0], args) < 0) {                                                                      /* Execute */
    perror(args[0]);
    exit(1);
  }
}

/* Input: PID of child process, array of background process PIDs, pointer to integer
 *   to hold details provided by waitpid()
 * Output: None
 * Description: If the child process is not running in the background, then parent
 *   waits for exit/termination. If the child process is running in the background
 *   then the parent adds it to the array of background process PIDs and continues
 *   running without wait()ing */
void execParent(pid_t spawnpid, pid_t backgroundPIDs[BGPID_ARR_SIZE], int *lastStat) {
  if (!background) {                                                                                    /* If it's a foreground proc, wait() for completion */
    waitpid(spawnpid, lastStat, 0);
    if (WIFSIGNALED(*lastStat)) {                                                                       /* If child was terminated, provide details */
      printf("terminated by signal %d\n", WTERMSIG(*lastStat));
      fflush(stdout);
    }
  }
  else {
    placePID(backgroundPIDs, spawnpid);
    printf("background pid is %d\n", spawnpid);
    fflush(stdout);
  }
}

/* Input: array of I/O redirection arguments, pointer to file descriptor for input file,
 *   pointer to file descriptor for output file
 * Output: None
 * Description: Sets I/O redirection arguments to NULL, closes file descriptors if they
 *   have been opened (and then resets them to flag indicating no file opened) */
void scrubIO(char *redir[2], int *inputFile, int *outputFile) {
  redir[0] = NULL;
  redir[1] = NULL;
  if (*inputFile != -5) {                                                                               /* -5 indicates no file has been opened */
    close(*inputFile);
    *inputFile = -5;
  }
  if (*outputFile != -5) {
    close(*outputFile);
    *outputFile = -5;
  }
}

/* Input: Pointer to input, array of arguments, number of arguments
 * Output: None
 * Description: Resets background flag, deallocates user input, clears array of
 *   arguments, resets string used for PID expansion */
void cleanup(char **userInput, char *args[NUM_ARGS], int argCount) {
  int i;

  background = 0;
  free(*userInput);
  *userInput = NULL;
  for (i = 0; i < argCount; i++) {
    args[i] = NULL;
  }
  memset(pidString, '\0', 50 * sizeof(char)); 
}


  /* * * * * * * * * * * * * * * * * * * * * * * * * * 
   *               Main() starts here                * 
   * * * * * * * * * * * * * * * * * * * * * * * * * */ 


int main(void) {

  char *args[NUM_ARGS];
  char *redir[2];
  int argCount = 0;
  int lastStat = -5;                                                                                    /* -5 means lastStat hasn't been used */ 
                                                                                                        /* Never tests for this, but could in future version of stat command */
  
  int outputFile = -5;
  int inputFile = -5;

  char* userInput = NULL;

  pid_t spawnpid = -5;
  pid_t backgroundPIDs[BGPID_ARR_SIZE];

  prepSigHandler();
  scrubPIDArray(backgroundPIDs);
  
  while (1) {
    scrubIO(redir, &inputFile, &outputFile);

    getCommand(&userInput, backgroundPIDs);
    argCount = parseCommand(userInput, args, redir); 

    if (!strcmp(args[0], "exit")) {                                                                     /* Built-in 'exit' */
      exitShell(backgroundPIDs, userInput);
      break;
    }
    else if (!strcmp(args[0], "status")) {                                                              /* Built-in 'status' */
      statusShell(lastStat);
    }
    else if (!strcmp(args[0], "cd")) {                                                                  /* Built-in 'cd' */
      cdShell(args, argCount);
    }
    else {                                                                                              /* Commands which are not built-in */
      spawnpid = fork();
      switch (spawnpid) {
        case -1:                                                                                        /* If fork fails */
          perror("Fork() problem!");
          exit(1);
          break;
        case 0:                                                                                         /* Child */
          execChild(redir, args, &inputFile, &outputFile);
          break;
        default:                                                                                        /* Parent */
          execParent(spawnpid, backgroundPIDs, &lastStat);
          break;
      } 
    }
    cleanup(&userInput, args, argCount);
  }

  return 0;
}
