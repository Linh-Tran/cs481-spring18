/* 
 * tsh - A tiny shell program with job control
 * 
 * <Linh Tran litran117>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
	/* Idea on blocking signals to avoid race conditions */
	/* https://lasr.cs.ucla.edu/vahab/resources/signals.html */
	char *argv[MAXARGS];
	int background_job;
	sigset_t signal_set;
	pid_t c_pid;

	//Parse the cmdline and build the argv
	background_job = parseline(cmdline, argv);

	//Ignore empty arguements
	if(argv[0]==NULL){
		return;
	}

	//Setup empty signal
	if(sigemptyset(&signal_set)<0){
		unix_error("Error sigemptyset");
	}

	//Add SIGCHLD to signal set
	if(sigaddset(&signal_set, SIGCHLD)<0 ||
	   sigaddset(&signal_set, SIGINT) <0 || 
	   sigaddset(&signal_set, SIGTSTP)<0) {
		unix_error("Error sigaddset");
	}

	//Check if arguments is a build in command
	if(!builtin_cmd(argv)){

		//block SIGCHLD using sigprocmask
		//so that the child won't terminate before we add it to the list of
		//jobs.
		if(sigprocmask(SIG_BLOCK, &signal_set, NULL) < 0){
			unix_error("Failed block SIGCHILD");
		}

		//Fork child process
		c_pid = fork();

		if(c_pid < 0){
			unix_error("Failed to fork proccess");
		}

		//Child runs this
		else if(c_pid == 0){

			//Unblock the signals sent to the child
			if(sigprocmask(SIG_UNBLOCK, &signal_set, NULL) < 0){
				unix_error("Failed unblock SIGCHILD");
			}

			//Put the child in a new process group who group ID matches the child's pid.
			if(setpgid(0,0)<0){
				unix_error("Failed setpgid");
			} /* sets the calling process id equal to the group id*/

			//Execute the child in a new enviorment pointed to by argv[0]
			if(execve(argv[0], argv, environ)<0){
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
		}
		else{
			//Add jobs BG || FG jobs to the jobslist base on the job type.
			if(!addjob(jobs,c_pid,(background_job == 1 ? BG:FG), cmdline)){
				unix_error("error addjob");
			}

			//Unblock signal that was blocked before jobs were added.
			if(sigprocmask(SIG_UNBLOCK, &signal_set, NULL)){
				unix_error("error sigprocmask");
			}

			//Foreground jobs are waited for here.
			if(!background_job){
				waitfg(c_pid);
				return;
			}

			//Don't wait for the background jobs.
			else{
				printf("[%d] (%d) %s", pid2jid(c_pid), c_pid, cmdline);	
			}
		}
	}
	return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	//Terminate the shell program.
    if(!strcmp(argv[0],"quit")){
      exit(0);
    }
    
    //Print out the job list.
    if(!strcmp(argv[0], "jobs")){
      listjobs(jobs);
      return 1;
    }

    //if argv is bg or fg handle them in do_bgfg()
    if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")){
      do_bgfg(argv);
      return 1;
    }

    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	char *id;
	int id_num;
	struct job_t *job;

	if(argv[1]== NULL){
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
	}

	//Source: http://www.cplusplus.com/reference/cstring/strstr/
	if(strstr(argv[1],"%")!= NULL){

		//Source: https://stackoverflow.com/questions/4108286/why-is-atoi-giving-me-a-segmentation-fault
		//Checks if the argument after the % is a integer with atoi.
		id = &argv[1][1];
		if((id_num = atoi(id)) == 0){
			printf("%%%s: No such job\n", &argv[1][1]);
			return;
		}

		//get the job returned from getjobjid.
		job = getjobjid(jobs,id_num);

		//check if it is null.
		if(job==NULL){
			printf("%%%d: No such job\n", id_num);
			return;
		}


	}

	//checks if the argument pointer to argv[1][0] at digit ex. fg 3
	else if(isdigit(argv[1][0])){
		id_num = atoi(argv[1]);

		//Check if job exists
		job = getjobpid(jobs, id_num);
		if(job==NULL){
			printf("(%d): No such process\n", id_num);
			return;
		}
	}
	else {
		//Argument is not a digit print error
		if(!isdigit(atoi(argv[1]++))){
			printf("%s: argument must be a PID or %%jobid\n", argv[0]);
			return;
		}
	}

	kill(-job->pid, SIGCONT); //Sends the SIGCONT to restart the job.

	if(!strcmp(argv[0], "bg")){
		//Change a stopped background job to a running background job.
		job->state = BG;
		printf("[%d] (%d) %s", job->jid, job->pid,job->cmdline);
	}
	else{
		// Change a stopped or running background job to a running in the foreground.
		job->state = FG;
		waitfg(job->pid);
	}
	
	
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	while(1)
	{
		//Check when there is no more foreground jobs in joblist and return.
		if(!fgpid(jobs)) {
			
			if(verbose) {
				printf("waitfg: Process (%d) no longer the fg process\n", pid);
			}
			return;
		}
		
	}

    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	if(verbose){
		printf("sigchld_handler: entering\n");
	}
	
	/* Idea on how to use waitpid options*/ 
	/*link: https://stackoverflow.com/questions/33508997/waitpid-wnohang-wuntraced-how-do-i-use-these*/
	pid_t pid;
	int status;
	struct job_t *job;
	while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED))> 0){

		job = getjobpid(jobs, pid);
		/* Child exited normally job is deleted from the job list */
		if(WIFEXITED(status)){
			if(verbose){
				printf("sigchld_handler: Job [%d] (%d) deleted\n",pid2jid(pid), pid);
				printf("sigchld_handler: Job [%d] (%d) terminates OK (status %d)\n", pid2jid(pid), pid, WTERMSIG(status));
			}
			if(!deletejob(jobs,pid)) {
				app_error("Deleting job failed line 362\n");
			}
		}

		/*Child process stopped, change its state to ST and handle signal*/
		else if(WIFSTOPPED(status)){
			printf("Job [%d] (%d) stopped by signal %d\n",job->jid, job->pid, WSTOPSIG(status));
			job->state = ST;
		}

		/* Idea about signal and why SIGINT could not be caught */
		/* https://stackoverflow.com/questions/21619086/signal-not-caught-when-sending-sigusr1-or-sigint-to-stopped-process-until-you-co */
		/* Child process terminated due to recieving uncaught signal */
		else if(WIFSIGNALED(status)){
			if(verbose){
				printf("sigchld_handler: Job [%d] (%d) deleted\n",pid2jid(pid), pid);
			}
			printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, WTERMSIG(status));
			
			if(!deletejob(jobs,pid)) {
				app_error("Deleting job failed line 362\n");
			}/* delete the job*/
		}
	}

	if(verbose){
		printf("sigchld_handler: exiting\n");
	}
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
  if(verbose){
  	printf("sigint_handler: entering\n");
  }
  /* Sends the SIGINT to its processes */
  int pid = fgpid(jobs);
  if(pid != 0){ /* check if is a foreground process */

  	//Sends SIGINT to the process to the foreground job.
 	 if(kill(-pid,sig) <0){ 
 	 	unix_error("kill (int) error");
  	 }
  	 else{
  	 	if(verbose){
  	 	printf("sigint_handler: Job (%d) killed\n", pid2jid(pid));
  	 	}
  	 }
  	 
  }

  if(verbose){
  	printf("sigint_handler: exiting\n");
  }
  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
  if(verbose){
  	printf("sigtstp_handler: entering\n");
  }
  /* Sends SIGTSTP to its processes*/
  int pid = fgpid(jobs);
  if(pid != 0){ /* check if is a foreground process */

  	//Sends SIGTSTP to the foreground process.
 	if(kill(-pid,sig) < 0){
  	  unix_error("kill (int) error");
  	}
  	else{
  		if(verbose){
  			printf("sigtstp_handler: Job [%d] (%d) stopped\n", pid2jid(pid), pid);
  		}
  	}

  }

  if(verbose){
  	printf("sigtstp_handler: exiting\n");
  }
  return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
