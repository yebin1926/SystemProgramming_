#include "dynarray.h"
#include "token.h"
#include "util.h"
#include "lexsyn.h"
#include "snush.h"
#include "execute.h"
#include "job.h"

extern struct job_manager *manager;
extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;

/*--------------------------------------------------------------------*/
void block_signal(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) < 0) {
    	fprintf(stderr, 
			"[Error] block_signal: sigprocmask(%s, sig=%d) failed: %s\n",
            block ? "SIG_BLOCK" : "SIG_UNBLOCK", sig, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
void handle_sigchld(void) { //a signal sent by the kernel to a parent process whenever one of its child processes terminates, stops, or continues

	/*
	 * TODO: Implement handle_sigchld() in execute.c
	 * Call waitpid() to wait for the child process to terminate.
	 * If the child process terminates, handle the job accordingly.
	 * Be careful to handle the SIGCHLD signal flag and unblock SIGCHLD.
	
	1. Check whether SIGCHLD actually happened
	2: Reset the flag
	3: Reap every child that has already exited
	4: For each exited child, get its PID
	5: Find which job that PID belonged to
	6: If no job is found, do not crash
	7: Remove the exited PID from that job
	8: If removing the PID failed, handle it safely
	9: Check whether the whole job is finished
	10: If the job is foreground and finished, delete it
	11: If the job is background and finished, do not immediately print from here
	12: Continue reaping until no more exited children are available
	13: If there are no more exited children, stop the loop
	14: Handle interrupted wait safely
	15: Handle “no children” safely
	16: For unexpected wait errors, print an error and stop
	*/

	block_signal(SIGCHLD, TRUE);
	block_signal(SIGINT, TRUE);

	if(!sigchld_flag){ //Check whether SIGCHLD actually happened
		block_signal(SIGINT, FALSE);
		block_signal(SIGCHLD, FALSE);
		return;
	}
	sigchld_flag = 0; //Reset the flag
	block_signal(SIGINT, FALSE);
	block_signal(SIGCHLD, FALSE);

	int status;
	pid_t child_pid;
	while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) { //Reap every child that has already exited
		//Find which job that PID belonged to
		struct job *exited_job = find_job_by_pid(child_pid);
		if(exited_job == NULL){
			continue;
		}

		//Remove the exited PID from that job
		if(!remove_pid_from_job(exited_job, child_pid)){
			continue;
		}

		// Mark finished background jobs for check_bg_status().
		if(exited_job->state == BACKGROUND &&
			exited_job->remaining_processes == 0){
			exited_job->completed = 1;
		}
	}

	if (child_pid < 0 && errno != ECHILD && errno != EINTR) {
		error_print("Unknown error waitpid() in handle_sigchld()", PERROR);
	}
}
/*--------------------------------------------------------------------*/
void handle_sigint(void) {
	
	/*
	 * TODO: Implement handle_sigint() in execute.c
	 * Find the foreground job and send signal to every process in the
	 * process group.
	 * Be careful to handle the SIGINT signal flag and unblock SIGINT.
	
	1. Check whether a SIGINT was actually requested
	2. Block signals while handling shared job state
	3. Clear sigint_flag
	4. Find the current foreground job
	5. If there is no foreground job, unblock signals and return
	6. Get the foreground job’s process group ID
	7. Send SIGINT to the foreground process group
	8. Handle the case where the process group no longer exists
	9. Do not delete the job directly inside handle_sigint()
	10. Unblock signals before returning

	 */
  block_signal(SIGCHLD, TRUE);
	block_signal(SIGINT, TRUE);

	if(!sigint_flag){ //Check whether SIGINT actually happened
		block_signal(SIGINT, FALSE);
		block_signal(SIGCHLD, FALSE);
		return;
	}
	sigint_flag = 0; //Reset the flag

	struct job *fg_job = find_fg_job(); //Find the current foreground job

	if(!fg_job){ //If there is no foreground job, unblock signals and return
		block_signal(SIGINT, FALSE);
		block_signal(SIGCHLD, FALSE);
		return;
	}

	//Send SIGINT to the foreground process group
	pid_t pgid = fg_job->pgid;
	if(kill(-pgid, SIGINT) == -1){
		if (errno != ESRCH) {
			fprintf(stderr, "Error sending sigint into pgid=%d\n", (int)pgid);
		}
	}

	block_signal(SIGINT, FALSE);
	block_signal(SIGCHLD, FALSE);
	return;
}
/*--------------------------------------------------------------------*/

//Tries to make stdout point to fd
//If dup2 fails, print an error message and exit the child.
void dup2_e(int oldfd, int newfd, const char *func, const int line) {
	int ret;

	ret = dup2(oldfd, newfd);
	if (ret < 0) {
		fprintf(stderr, 
			"Error dup2(%d, %d): %s(%s) at (%s:%d)\n", 
			oldfd, newfd, strerror(errno), errno_name(errno), func, line);
		_exit(127);
	}
}

/*--------------------------------------------------------------------*/

/* Do not modify this function. It is used to check the signals and 
 * handle them accordingly. It is called in the main loop of snush.c.
 */
void check_signals(void) {
    handle_sigchld();
    handle_sigint();
}

/*--------------------------------------------------------------------*/
void redout_handler(char *fname) {
	/*
	TODO: Implement redout_handler in execute.c
	1. Receive the output filename (fname)
	2. Open or create that file for writing. 
		- If file doesn't exist, create. If file exists, erase/truncate its old contents
	3. Set appropriate file permissions if creating a new file
	4. Check whether opening the file failed
	5. Replace standard output with the file descriptor
	6. Close the extra file descriptor
	*/
	int fd;

	fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0640); //owner can read/write, group can read, others have no permission
	if (fd < 0) {
		error_print(NULL, PERROR);
		_exit(127);
	}

	dup2_e(fd, STDOUT_FILENO, __func__, __LINE__);
	close(fd);
}
/*--------------------------------------------------------------------*/
void redin_handler(char *fname) {
	int fd;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		error_print(NULL, PERROR);
		_exit(127);
	}

	dup2_e(fd, STDIN_FILENO, __func__, __LINE__);
	close(fd);
}
/*--------------------------------------------------------------------*/
//takes part of the token array and converts it into the args[] array that you can pass to execvp()
// extracts only the command words for one command segment/process. Redirection or pipe is not included.
void build_command_partial(DynArray_T oTokens, int start, 
						int end, char *args[]) {
	// oTokens: whole token array from parsing
	// start: first token index that this function should process, in the whole token array
	// end: stopping index
	// *args[] : output argument array that execvp() needs
	int i, redin = FALSE, redout = FALSE, cnt = 0;
	struct Token *t;

	/* Build command */
	for (i = start; i < end; i++) {
		t = dynarray_get(oTokens, i);

		if (t->token_type == TOKEN_WORD) {
			if (redin == TRUE) {
				redin_handler(t->token_value);
				redin = FALSE;
			}
			else if (redout == TRUE) {
				redout_handler(t->token_value);
				redout = FALSE;
			}
			else {
				args[cnt++] = t->token_value;
			}
		}
		else if (t->token_type == TOKEN_REDIN)
			redin = TRUE;
		else if (t->token_type == TOKEN_REDOUT)
			redout = TRUE;
	}

	if (cnt >= MAX_ARGS_CNT) 
		fprintf(stderr, "[BUG] args overflow! cnt=%d\n", cnt);

	args[cnt] = NULL;

#ifdef DEBUG
	for (i = 0; i < cnt; i++) {
		if (args[i] == NULL)
			printf("CMD: NULL\n");
		else
			printf("CMD: %s\n", args[i]);
	}
	printf("END\n");
#endif
}
/*--------------------------------------------------------------------*/
void build_command(DynArray_T oTokens, char *args[]) {
	build_command_partial(oTokens, 0, 
						dynarray_get_length(oTokens), 
						args);
}
/*--------------------------------------------------------------------*/
int execute_builtin_partial(DynArray_T toks, int start, int end,
                            enum BuiltinType btype, int in_child) {
    
	int argc = end - start;
	struct Token *t1;
	int ret;
    char *dir;

    switch (btype) {
    case B_EXIT:
        if (in_child) return 0;
        
		if (argc == 1) {
			dynarray_map(toks, free_token, NULL);
			dynarray_free(toks);
			exit(EXIT_SUCCESS);
		}
		else {
			error_print("exit does not take any parameters", FPRINTF);
			return -1;
		}

    case B_CD: {
        if (argc == 1) {
            dir = getenv("HOME");
            if (!dir) {
                error_print("cd: HOME variable not set", FPRINTF);
                return -1;
            }
        } 
		else if (argc == 2) {
            t1 = dynarray_get(toks, start + 1);
            if (t1 && t1->token_type == TOKEN_WORD) 
				dir = t1->token_value;
        } 
		else {
            error_print("cd: Too many parameters", FPRINTF);
            return -1;
        }

        ret = chdir(dir);
        if (ret < 0) {
            error_print(NULL, PERROR);
            return -1;
        }
        return 0;
    }

    default:
        error_print("Bug found in execute_builtin_partial", FPRINTF);
        return -1;
    }
}
/*--------------------------------------------------------------------*/
int execute_builtin(DynArray_T oTokens, enum BuiltinType btype) {
	return execute_builtin_partial(oTokens, 0, 
								dynarray_get_length(oTokens), btype, FALSE);
}
/*--------------------------------------------------------------------*/
/* 
 * You need to finish implementing job related APIs. (find_job_by_jid(),
 * remove_pid_from_job(), delete_job()) in job.c to handle the job.
 * Feel free to modify the format of the job API according to your design.
 */
void wait_fg(int jobid) {
	pid_t pid;
	int status;

	 // Find the job structure by job ID
    struct job *job = find_job_by_jid(jobid);
    if (!job) {
        fprintf(stderr, "Job: %d not found\n", jobid);
        return;
    }

    while (1) {
        pid = waitpid(-job->pgid, &status, 0);

        if (pid > 0) {
			// Remove the finished process from the job's pid list
			if (!remove_pid_from_job(job, pid)) {
				fprintf(stderr, "Pid %d not found in the job: %d list\n", 
					pid, job->job_id);
			}

			if (job->remaining_processes == 0) break;
        }

        if (pid == 0) continue;

		if (pid < 0) {
			if (errno == EINTR) continue;
			if (errno == ECHILD) break;
			error_print("Unknown error waitpid() in wait_fg()", PERROR);
		}
    }

	// Clean up job table entry if all processes are done
    if (job->remaining_processes == 0)
        delete_job(job->job_id);
}
/*--------------------------------------------------------------------*/
void print_job(int jobid, pid_t pgid) {
    fprintf(stdout, 
		"[%d] Process group: %d running in the background\n", jobid, pgid);
}
/*--------------------------------------------------------------------*/
int fork_exec(DynArray_T oTokens, int is_background) {
	/*
	 * TODO: Implement fork_exec() in execute.c
	 * To run a newly forked process in the foreground, call wait_fg() 
	 * to wait for the process to finish.  
	 * To run it in the background, call print_job() to print job id and process group id.  
	 * All terminated processes must be handled by sigchld_handler() in * snush.c. 

		1. Create child with fork()
		2. Put child in its own process group with setpgid()
		3. Parent registers job in job manager
		4. Child builds argv using build_command()
		5. Child calls execvp()
		6. Parent waits if foreground
		7. Parent prints job info if background
	 */

	//Create pipe for synchronization
	int sync_pipe[2]; //sync_pipe[0]: used for reading, sync_pipe[1]: used for writing
	char dummy;

	if (pipe(sync_pipe) < 0) {
			error_print(NULL, PERROR);
			return -1;
	}

	//Fork
	pid_t pid = fork();
	int jobid;

	if(pid < 0){ //if fork failed
		error_print(NULL, PERROR);
		close(sync_pipe[0]);
    close(sync_pipe[1]);
    return -1;

	} else if(pid == 0){ //child process
		close(sync_pipe[1]); // Child closes the write end

		if(setpgid(0, 0) == -1){ //Put child in its own process group with setpgid(). //if setpgid fails:
			error_print(NULL, PERROR);
			close(sync_pipe[0]);
			_exit(127);
		} 

		if (read(sync_pipe[0], &dummy, 1) != 1) {
			_exit(127);
		}
		close(sync_pipe[0]); //Child closes its read end

		//Child builds argv using build_command()
		char *args[MAX_ARGS_CNT];
		build_command(oTokens, args);
		if (args[0] == NULL) _exit(127);
		execvp(args[0], args); //calls execvp()

		//If execvp() fails, print error
		error_print(NULL, PERROR);
		_exit(127);

	} else{ //parent process

		close(sync_pipe[0]); //Parent closes read end

		//setting child in its own process group
		if(setpgid(pid, pid) == -1){ //if setpgid fails
			error_print(NULL, PERROR);
			close(sync_pipe[1]);
			kill(pid, SIGTERM);
			waitpid(pid, NULL, 0);
			return -1;
		}

		//register job to job manager
		pid_t pids[1] = {pid};
		job_state state = is_background ? BACKGROUND : FOREGROUND;
		jobid = add_job(pid, pids, 1, state);


		if(jobid < 0){
			close(sync_pipe[1]);
			kill(pid, SIGTERM);
			waitpid(pid, NULL, 0);
			return -1;
		}

		if (write(sync_pipe[1], "x", 1) < 0) {
			error_print(NULL, PERROR);
			close(sync_pipe[1]);
			kill(pid, SIGTERM);
			waitpid(pid, NULL, 0);
			return -1;
		}
		close(sync_pipe[1]); //close the write-end

		if(!is_background){ //Parent waits if foreground
			wait_fg(jobid);
		} else{ //Parent prints job info if background
			print_job(jobid, pid);
		}
	}

	return jobid;
}

//Helper function to close all pipes
void close_pipes(int pipefd[][2], int n_pipe){
	if (pipefd == NULL) return;

	for(int i=0; i<n_pipe; i++){
		close(pipefd[i][0]);
		close(pipefd[i][1]);
	}
}

/*--------------------------------------------------------------------*/
int iter_pipe_fork_exec(int n_pipe, DynArray_T oTokens, int is_background) {
	/*
	 * TODO: Implement iter_pipe_fork_exec() in execute.c
	 * To run a newly forked process in the foreground, call wait_fg() 
	 * to wait for the process to finish.  
	 * To run it in the background, call print_job() to print job id and
	 * process group id.  
	 * All terminated processes must be handled by sigchld_handler() in * snush.c. 
	
	1. Compute the number of commands in the pipeline
	2. Prepare storage for child PIDs
	3. Prepare storage for pipe file descriptors
	4. Create all pipes before forking children
	5. Create a synchronization pipe - Like in fork_exec(), use a separate internal pipe to make children wait until the parent has registered the job.
	6. Find token ranges for each pipeline stage
	7. Fork one child per pipeline stage
	8. Assign all children to one process group
	9. In each child: close the unused side of the sync pipe
	10. In each child: connect pipe input/output
	11. In each child: apply dup2() before closing pipe fds
	12. In each child: close all pipe file descriptors
	13. In each child: wait on the synchronization pipe
	14. In each child: close the sync pipe read end
	15. In each child: build the command for only its stage
	16. In each child: detect whether the command is a built-in
	17. In each child: execute built-ins directly
	18. In each child: execute external commands with execvp()
	19. In the parent after each fork: store child PID
			In the parent: close pipe fds after all children are forked
			In the parent: register the whole pipeline as one job
			In the parent: release the children
			In the parent: close the sync pipe
	24. If job registration fails
	25. If foreground pipeline, if background pipeline
	27. Return the job ID
	28. Make sure built-in standalone behavior remains separate]
	29. Make sure redirection and pipes interact correctly
	30. Make sure every error path closes file descriptors
	31. Make sure children exit on failure, Make sure parent does not _exit()
	 */

	int n_commands = n_pipe + 1;

	int sync_pipe[2]; //sync_pipe[0]: used for reading, sync_pipe[1]: used for writing

	pid_t pids[n_commands]; //storage for child PIDs
	int pipefd[n_pipe][2]; //storage for file, read and write end

	int cmd_starts[n_commands]; //storage for command indexes
	int cmd_ends[n_commands];

	for(int i=0; i<n_pipe; i++){ //Create all pipes before forking children
		if(pipe(pipefd[i]) == -1){ 
			error_print("creating pipe failed", FPRINTF);
			close(pipefd[i][0]);
			close(pipefd[i][1]);
			return -1;
		}
	}

	//Save starting and ending indexes of each command
	cmd_starts[0] = 0;
	cmd_ends[n_commands] = oTokens->iLength;
	int cnt = 0;

	for(int i=0; i<oTokens->iLength; i++){
		if(oTokens->ppvArray[i] == '|'){
			cmd_ends[cnt] = i-1;
			if(cnt<n_commands) cmd_starts[++cnt] = i+1;
		}
	}

	//Fork one child per pipeline stage
	for(int i=0; i<n_commands; i++){
		int start = cmd_starts[i];
		int end = cmd_ends[i];

		pid_t pid = fork();

		if(pid < 0){ //if fork failed
			error_print(NULL, PERROR);
			close(sync_pipe[0]);
			close(sync_pipe[1]);
			return -1;
		} 
		else if(pid == 0){ //child process
		}
		else{ //parent process
			//
		}

	int jobid = 1;	
	return jobid;
}
/*--------------------------------------------------------------------*/
