#include "job.h"
#include "util.h"
#include "execute.h"

extern struct job_manager *manager;

// Job: one command line that the user entered tracked by the shell’s job manager.
// a job may contain one process or multiple processes depending on whether the command has pipes

/*--------------------------------------------------------------------*/
void init_job_manager() {
	manager = (struct job_manager *)calloc(1, sizeof(struct job_manager));
	if (manager == NULL) {
		fprintf(stderr, "[Error] job manager allocation failed\n");
		exit(EXIT_FAILURE);
	}

    /*
     * TODO: Init job manager
     */
    manager->n_jobs = 0;
    manager->jobs = calloc(MAX_JOBS, sizeof(struct job));
    if (manager->jobs == NULL) {
        fprintf(stderr, "[Error] job list allocation failed\n");
        free(manager);
        exit(EXIT_FAILURE);
    }
    manager->next_job_id = 1;
}
/*--------------------------------------------------------------------*/
/* This is just a placeholder for compilation. You can modify it if you want. */
struct job *find_job_by_jid(int job_id) {
    /*
     * TODO: Implement find_job_by_jid()
     */
    for (int i = 0; i < (manager->n_jobs) ; i++) {
        if(manager->jobs[i].job_id == job_id){
            return &manager->jobs[i];
        }
    }
    return NULL;
}
/*--------------------------------------------------------------------*/
int remove_pid_from_job(struct job *job, pid_t pid) {

    /*
     * TODO: Implement remove_pid_from_job()
    */
    if (job == NULL || job->pids == NULL) return 0;
    block_signal(SIGCHLD, TRUE); //Block SIGCHLD before modifying job structures
    block_signal(SIGINT, TRUE);

    for (int i=0; i < job->remaining_processes; i++){ //for every pid in that job,
        if(job->pids[i] == pid){                      //if its pid == pid,
            for(int j=i; j < (job->remaining_processes - 1); j++){    //remove pid by shifting all pids after it to the left
                job->pids[j] = job->pids[j+1];
            }
            job->remaining_processes--;
            block_signal(SIGINT, FALSE);
            block_signal(SIGCHLD, FALSE); //Unblock SIGCHLD after mutating shared data is done
            return 1;
        }
    }
    block_signal(SIGINT, FALSE);
    block_signal(SIGCHLD, FALSE); //unblock SIGCHLD even if pid not found
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int jobid) {
	
    /*
     * TODO: Implement delete_job()
     */
    //Put the block calls before the guard, so every return path has matching unblock calls
    block_signal(SIGCHLD, TRUE);
    block_signal(SIGINT, TRUE);

    if (manager == NULL || manager->jobs == NULL || manager->n_jobs == 0) {
        block_signal(SIGINT, FALSE);
        block_signal(SIGCHLD, FALSE);
        return 0;
    }
    
    for (int i = 0; i < (manager->n_jobs) ; i++) {  //delete the job from job manager
        if(manager->jobs[i].job_id == jobid){
            free(manager->jobs[i].pids);

            if (i != manager->n_jobs - 1) { //don't do anything if job[i] IS the last job
                manager->jobs[i] = manager->jobs[manager->n_jobs - 1]; //move the last job to the empty slot
            }

            memset(&manager->jobs[manager->n_jobs - 1], 0, sizeof(struct job)); // fill the n-1 slot with 0
            manager->n_jobs--;

            block_signal(SIGINT, FALSE);
            block_signal(SIGCHLD, FALSE);
            return 1;
        }
    }
    block_signal(SIGINT, FALSE);
    block_signal(SIGCHLD, FALSE);
    return 0;
}
/*--------------------------------------------------------------------*/
/*
 * TODO: Implement any necessary job-control code in job.c 
 */

int add_job(pid_t pgid, pid_t *pids, int num_pids, job_state state){

    // Checks before using manager
    if (manager == NULL || manager->jobs == NULL) return -1;
    if (pids == NULL || num_pids <= 0) return -1;

    block_signal(SIGCHLD, TRUE);
    block_signal(SIGINT, TRUE);

    if (manager->n_jobs >= MAX_JOBS) { // job table full
        block_signal(SIGINT, FALSE);
        block_signal(SIGCHLD, FALSE);
        return -1;
    }

    struct job *newjob = &manager->jobs[manager->n_jobs];
    memset(newjob, 0, sizeof(struct job));

    int jobid = manager->next_job_id++;
    newjob->job_id = jobid;
    newjob->pgid = pgid;
    newjob->state = state;
    newjob->remaining_processes = num_pids;
    newjob->completed = 0;

    newjob->pids = malloc(sizeof(pid_t) * num_pids);
    if (!newjob->pids) {
        block_signal(SIGINT, FALSE);
        block_signal(SIGCHLD, FALSE);
        return -1;
    }

    for (int i = 0; i < num_pids; i++) {
        newjob->pids[i] = pids[i];
    }

    manager->n_jobs++;

    block_signal(SIGINT, FALSE);
    block_signal(SIGCHLD, FALSE);
    return jobid;
}

int add_pid_to_job(struct job *job, pid_t pid){
    if(job == NULL){
        return -1;
    }
    block_signal(SIGCHLD, TRUE);
    block_signal(SIGINT, TRUE);
    pid_t *new_arr = realloc(job->pids, sizeof(pid_t) * (job->remaining_processes + 1)); //create new array with space for new pid
    if (!new_arr) {
        block_signal(SIGINT, FALSE);
        block_signal(SIGCHLD, FALSE);
        return -1;
    }

    job->pids = new_arr;
    job->pids[job->remaining_processes] = pid;
    job->remaining_processes++;
    block_signal(SIGINT, FALSE);
    block_signal(SIGCHLD, FALSE);
    return 1;
}
// free_job_manager() ????

struct job *find_job_by_pid(pid_t pid){
    if (manager == NULL || manager->jobs == NULL) return NULL;
    for (int i = 0; i < (manager->n_jobs) ; i++) {
        for(int j=0; j < (manager->jobs[i].remaining_processes); j++){
            if(manager->jobs[i].pids[j] == pid){
                return &manager->jobs[i];
            }
        }
    }
    return NULL;
}

struct job *find_fg_job(void){ // the job the shell is currently waiting for or currently giving control to
    if (manager == NULL || manager->jobs == NULL) return NULL;

    for (int i = 0; i < (manager->n_jobs) ; i++) {
        if(manager->jobs[i].state == FOREGROUND){
            return &manager->jobs[i];
        }
    }
    return NULL;
}

struct job *find_completed_bg_job(void){
    if (manager == NULL || manager->jobs == NULL) return NULL;

    for (int i = 0; i < (manager->n_jobs) ; i++) {
        if(manager->jobs[i].state == BACKGROUND && manager->jobs[i].completed){
            return &manager->jobs[i];
        }
    }
    return NULL;
}
