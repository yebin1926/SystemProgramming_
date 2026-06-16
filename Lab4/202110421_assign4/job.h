#ifndef _JOB_H_
#define _JOB_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define MAX_JOBS 16

typedef enum State {
    UNKNOWN = 0,
    FOREGROUND,
    BACKGROUND,
    STOPPED,
} job_state;

/* 
 * Job = The user's command line input
 * ex) if the user's command line input is "ps -ef | grep job" then it's one job, with two processes. 
 */
struct job {
    int job_id;
    pid_t pgid; //process group ID for the job (pgid lets the shell send one signal to all processes in the job, e.g. same PGID to 3 processes in 'ls | grep c | sort')
    int remaining_processes; //how many child processes in this job are still not fully handled
    /* TODO: Add any necessary fields to the job */
    pid_t *pids; //which PIDs belong to it (process ID)
    job_state state; //Whether the job is foreground or background
    int completed; //Background job finished and is waiting to be reported
};

/* 
 * One global variable for a job manager. 
 * When a job is created, register it with the job manager, 
 * regardless of whether it is a foreground or background job.
 */
struct job_manager {
    int n_jobs; //how many jobs are currently registered
    struct job *jobs; //points to an array of struct job
    /* TODO: Add any necessary fields to the job manager */
    int next_job_id; //job ID to use next time
};

void init_job_manager();
struct job *find_job_by_jid(int job_id);
int remove_pid_from_job(struct job *job, pid_t pid);
int delete_job(int job_id);

/*
 * TODO: Implement any necessary job-control code in job.h 
 */

int add_job(pid_t pgid, pid_t *pids, int num_pids, job_state state);
int add_pid_to_job(struct job *job, pid_t pid);
struct job *find_job_by_pid(pid_t pid);
struct job *find_fg_job(void);
struct job *find_completed_bg_job(void);


#endif /* _JOB_H_ */
