#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>

#define INP_SIZE 100

struct Job {    //struct/class for every single job
    int pid;    //pid from fork
    int job_id; //index in jobs, start from 1
    int state;  //0 = foreground, 1 = background, 2 = stopped, 3 = terminated
    char user_inp[INP_SIZE];
};

struct Job jobs[50];
int new_job = 1;

int fgid = 0;

int saved_stdin;
int saved_stdout;

void printjobs(){
    for(int i = 1; i < new_job; ++i){
        char curr_state[100];
        if(jobs[i].state == 0){
            strcpy(curr_state, "foreground");       //THIS SHOULD NEVER PRINT (?)
        }else if(jobs[i].state == 1){
            strcpy(curr_state, "background");
        }else if(jobs[i].state == 2){
            strcpy(curr_state, "stopped");
        }else{
            continue;
        }
        printf("[%2d] (%6d) %s %s \n", jobs[i].job_id, jobs[i].pid, curr_state, jobs[i].user_inp);
    }
}

void change_state(int pid, int new_state){ //change state of pid in job to state
    for(int i = 0; i < new_job; i++){
        if(pid == jobs[i].pid){
            jobs[i].state = new_state;
            break;
        }
    }
}

void waiting4pid(pid_t processID){
    //main process waiting for foreground process to finish

    int waitCondition = WUNTRACED | WCONTINUED;
    int currentState;
    pid_t childpid;
    childpid = waitpid(processID, &currentState, waitCondition);
    if(WTERMSIG(currentState)){
    }
    if(WIFSIGNALED(currentState) || WIFEXITED(currentState)){
        //change
        change_state(processID, 3);
        printf("\n Child %d Exited!\n", processID);
    }
    if(__WIFSTOPPED(currentState)){
        //change
        change_state(processID, 2);
        printf("\n Child %d stopped\n", processID);
    }
    return;
}

void sig_handler(int signum){
    //used when quit, kills and reaps all child processes
    for(int i = 1; i < new_job; i++){
        if(jobs[i].state == 0){
            int p_id = jobs[i].pid;
            printf("SIGNAL RECEIVED - %d\n", p_id);
            if(signum == SIGINT){
                kill(p_id, SIGINT);
                waiting4pid(p_id);
                change_state(p_id, 3);
            } else if(signum == SIGTSTP){
                kill(p_id, signum);
                //change
                change_state(p_id, 2);
            }
        }
    }
}

void sigchild_handler(int signum){
    //reaps all child processes
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        //change
        change_state(pid, 3);
        if (WIFEXITED(status)) {
            printf("Child process %d exited with status %d\n", pid, WEXITSTATUS(status)); //handle child process errors
        }
    }
}

void quit_helper(){ // kills all children before quitting program
    for(int i = 1; i < new_job; i++){
        if(jobs[i].state != 3){
            kill(jobs[i].pid, SIGKILL);
            waiting4pid(jobs[i].pid);
        }
    }
    kill(fgid, SIGKILL);
}

void kill_helper(char* argv[]){ // kills specific jobid or pid
    int p_id;
    int p_state;
    if (argv[1][0] == '%') {
        p_id = jobs[atoi(argv[1] + 1)].pid;
        p_state = jobs[atoi(argv[1] + 1)].state;
        jobs[atoi(argv[1] + 1)].state = 3;
        setpgid(p_id, 0);
    } else {
        p_id = atoi(argv[1]);
        for (int i = 1; i < new_job; i++) {
            if (jobs[i].pid == p_id) {
                p_state = jobs[i].state;
                jobs[i].state = 3;
                setpgid(p_id, 0);
                break;
            }
        }
    }
    if(p_state == 2){
        kill(p_id, SIGCONT);
    }
    kill(p_id, SIGINT);
}

void bg_process(char* argv[]){ // move process to background
    int p_id;
    if (argv[1][0] == '%') {
        // If the job id is given, it can be used to index directly to change the Job status
        p_id = jobs[atoi(argv[1] + 1)].pid;
        jobs[atoi(argv[1] + 1)].state = 1; // Set to background
    } else {
        // If pid is given, then find the matching Job in Jobs to change its status
        p_id = atoi(argv[1]);
        //change
        change_state(p_id,1);
    }
    setpgid(p_id, 0);
    kill(p_id, SIGCONT); // Continue previously stopped process
}

void fg_process(char* argv[]){ // move process to foreground
    int p_id;
    if (argv[1][0] == '%') {
        p_id = jobs[atoi(argv[1] + 1)].pid;
        jobs[atoi(argv[1] + 1)].state = 0; // Set to foreground
    } else {
        p_id = atoi(argv[1]);
        //change
        change_state(p_id, 0);
    }
    setpgid(p_id, p_id);
    fgid = p_id;
    kill(p_id, SIGCONT);
    waiting4pid(p_id); // wait for new foreground process to terminate
    fgid = 0;
}

void run_file(char* argv[], char* inp_copy, int num_args){ //runs file given in argv
    int bg = 0;
    if(num_args > 0 && strcmp(argv[num_args - 1],"&") == 0){
        // background process: bg = 1
        argv[num_args - 1] = NULL;
        bg = 1;
    } else{
        // foreground process: bg=0
        argv[num_args] = NULL;
    }
    int pid = fork();
    struct Job j = {pid, new_job, bg, *inp_copy};
    strcpy(j.user_inp, inp_copy);
    jobs[new_job] = j;
    new_job +=1;
    if(bg == 1){
        setpgid(pid, 0);
    } else{
        setpgid(pid, pid);
    }
    if(pid == 0){
        // try both execvp and execv
        if(execvp(argv[0], argv) < 0){
            if(execv(argv[0], argv) < 0){
                exit(1);
            }
        }
    } else {
        // add new Job to jobs list using its pid
        printf("CHILD %d\n", pid);
        if(bg == 0){
            // wait for process if its foreground
            fgid = pid;
            waiting4pid(pid);
            fgid = 0;
        }
    }
}

void run_input_redirect(int *inFileID, mode_t mode, char* input_redirect){ //redirects input from file
    char* input_file = strtok(input_redirect + 1, " ");
    printf("Input file %s\n", input_file);
    *inFileID = open(input_file, O_RDONLY, mode);
    if (*inFileID < 0) {
        perror("Failed to open input file");
    }
    saved_stdin = dup(0);
    dup2(*inFileID, STDIN_FILENO);
}

void run_output_redirect(int *outFileID, mode_t mode, char* output_redirect){ //redirects output to file
    char* output_file;
    if (output_redirect[1] == '>') { // If there is 2 arrows, append to file
        output_file = strtok(output_redirect + 2, " ");
        *outFileID = open(output_file, O_CREAT|O_APPEND|O_WRONLY, mode);
    } else { // If there is 1 arrow, overwrite the file
        output_file = strtok(output_redirect + 1, " ");
        *outFileID = open(output_file, O_CREAT|O_WRONLY|O_TRUNC, mode);
    }
    if (*outFileID < 0) {
        perror("Failed to open output file");
    }
    saved_stdout = dup(1);
    dup2(*outFileID, STDOUT_FILENO);
}

int main(){
    char inp[INP_SIZE];

    //signal init
    signal(SIGINT, sig_handler);
    signal(SIGCHLD, sigchild_handler);
    signal(SIGTSTP, sig_handler);

    while(true){
        printf("prompt > ");
        fflush(stdout);

        // parse argv from input
        fgets(inp, INP_SIZE, stdin);
        inp[strlen(inp)-1] = '\0';

        int inFileID = -1;
        int outFileID = -1;
        char* inp_copy = strdup(inp);
        mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;
        char* input_redirect = strstr(inp, "<");
        char* output_redirect = strstr(inp_copy, ">");

        if (input_redirect != NULL) {
            run_input_redirect(&inFileID, mode, input_redirect);
        }
        if (output_redirect != NULL) { // If > is in the command then output is set to the following file
            run_output_redirect(&outFileID, mode, output_redirect);
        }
        
        // tokenize input to make it into usable as argv
        char *tmp = strtok(inp, " ");
        if(tmp == NULL){
            continue;
        }
        char *argv[50];
        int num_args = 0;
        while (tmp != NULL) {
            argv[num_args] = strdup(tmp);
            num_args += 1;
            tmp = strtok(NULL, " ");
        }

        // built in commands - quit, cd, pwd, kill, jobs
        if(strcmp(argv[0], "quit") == 0){
            quit_helper();
            break;
        } else if(strcmp(argv[0], "pwd") == 0){
            char res[INP_SIZE];
            getcwd(res, INP_SIZE);
            printf("%s\n", res);
        } else if(strcmp(argv[0], "cd") == 0){
            chdir(argv[1]);
        } else if(strcmp(argv[0], "jobs") == 0){
            printjobs();
        } else if(strcmp(argv[0], "kill") == 0){
            kill_helper(argv);
        }
        // Stopped foreground process is set to the background
        else if (strcmp(argv[0], "bg") == 0) {
            bg_process(argv);
        }
        // running background process is set to the foreground
        else if (strcmp(argv[0], "fg") == 0) {
            fg_process(argv);
        }else{
            // execute inputted command if not one of previous built-ins
            run_file(argv, inp_copy, num_args);
        }
        // Close opened files and revert I/O to STD
        if (inFileID >= 0) {
            dup2(saved_stdin, 0);
            close(inFileID);
        }
        if (outFileID >= 0) {
            dup2(saved_stdout, 1);
            close(outFileID);
        }
    }
}