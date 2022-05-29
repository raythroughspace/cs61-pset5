#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>

// For the love of God
#undef exit
#define exit __DO_NOT_CALL_EXIT__READ_PROBLEM_SET_DESCRIPTION__


// struct command
//    Data structure describing a command. Add your own stuff.

struct command {
    std::vector<std::string> args;
    std::string stdin_file; //filename for stdin redirection
    std::string stdout_file; //filename for stdout redirection
    std::string stderr_file;//filename for stderr redirection
    bool stdin_redir = false;
    bool stdout_redir = false;
    bool stderr_redir = false;
    pid_t pid = -1;      // process ID running this command, -1 if none
    pid_t pgid = -1; //process group ID running this command, -1 if none
    command* next = nullptr;
    command* prev = nullptr;
    int read_end; //read_end from pipe
    int op = TYPE_SEQUENCE;
    pid_t run(pid_t pgid);
};


// COMMAND EXECUTION

// command::run()
//    Create a single child process running the command in `this`.
//    Sets `this->pid` to the pid of the child process and returns `this->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating an array of `char*` arguments using
//       `this->args[N].c_str()`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: Update the `command` structure and this function to support
//       setting the child process’s process group. To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t command::run(pid_t pgid) {
    assert(this->args.size() > 0);
    char* cargs[this->args.size() + 1];
    for (size_t i = 0; i< this->args.size(); ++i){
        cargs[i] = (char*) this->args[i].c_str();
    }
    cargs[this->args.size()] = nullptr;
    int pfd[2];
    if (this->op == TYPE_PIPE){
        int r = pipe(pfd);
        this->next->read_end = pfd[0];
    }
    int c = fork();
    if (c==0){
        if (!this->prev || (this->prev && this->prev->op != TYPE_PIPE)){
            setpgid(0,0);
            this->pgid = getpid();
        }
        if (this->op == TYPE_PIPE){
            dup2(pfd[1], 1);
            close(pfd[1]);
            close(pfd[0]);
        }
        if (this->prev && this->prev->op == TYPE_PIPE){
            dup2(this->read_end, 0);
            close(this->read_end);
        }
        //redirections have priority over pipes
        if (this->stdin_redir) {
            int fd = open(this->stdin_file.c_str(), O_RDONLY|O_CLOEXEC);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(EXIT_FAILURE);
            }
            dup2(fd, 0);
        }
        if (this->stdout_redir) {
            int fd = open(this->stdout_file.c_str(), O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(EXIT_FAILURE);
            }
            dup2(fd, 1);
        }
        if (this->stderr_redir) {
            int fd = open(this->stderr_file.c_str(), O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(EXIT_FAILURE);
            }
            dup2(fd, 2);
        }
        int r = execvp(cargs[0], cargs);
        fprintf(stderr, "Process %d exited abnormally\n", c);
        _exit(EXIT_FAILURE);
    }
    if (this->op == TYPE_PIPE){
        close(pfd[1]);
    }
    if (this->prev && this->prev->op == TYPE_PIPE){
        close(this->read_end);
    }
    this->pid = c;
    if (pgid != -1){
        this->pgid = pgid;
        setpgid(this->pid, this->pgid);
    }
    return this->pid;
}


// run_list(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `c->run()` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    It is possible, and not too ugly, to handle lists, conditionals,
//    *and* pipelines entirely within `run_list`, but many students choose
//    to introduce `run_conditional` and `run_pipeline` functions that
//    are called by `run_list`. It’s up to you.
//
//    PART 1: Start the single command `c` with `c->run()`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `command::run` (or in helper functions).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Make sure every process in the pipeline has the same
//         process group, `pgid`.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.
bool foreground_pipeline(command* c){

}
int run_pipeline(command* c){
    int status; //status of last command
    pid_t p; //pid_t of last command
    pid_t pgid = -1;
    while (c){
        p = c->run(pgid);
        //first command has pgid set to pid
        if (c->pgid != -1){
            pgid = c->pgid;
        }
        //move to next command in pipe
        while (c && c->op != TYPE_PIPE
                && c->op != TYPE_BACKGROUND && c->op != TYPE_SEQUENCE
                && c->op != TYPE_AND && c->op !=TYPE_OR){
            c= c->next;
        }
        if (!c || c->op == TYPE_BACKGROUND || c->op == TYPE_SEQUENCE
            || c->op == TYPE_AND || c->op ==TYPE_OR){ //no more commands
            int w = waitpid(p, &status, 0);
            return status;
        }
        c = c->next;
    }
    claim_foreground(pgid);
    int w = waitpid(p, &status, 0);
    claim_foreground(0);
    return status;
}

void run_conditional(command* c){
    int status;
    while (c){
        status = run_pipeline(c);
        do{
            //move to first command of next pipeline
            while (c && c->op != TYPE_AND && c->op != TYPE_OR
                    && c->op != TYPE_SEQUENCE && c->op !=TYPE_BACKGROUND){
                c= c->next;
            }
            if (!c
                || c->op == TYPE_SEQUENCE || c->op ==TYPE_BACKGROUND){ //no more commands in current conditional
                return;
            }
            c = c->next;
        } while(!((WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS && c->prev->op == TYPE_AND)
                   ||(!WIFEXITED(status) && c->prev->op == TYPE_OR)
                   ||(WIFEXITED(status) && WEXITSTATUS(status) != EXIT_SUCCESS && c->prev->op == TYPE_OR)));
    }
}
bool cond_in_background(command* c) {
    while (c->op != TYPE_SEQUENCE && c->op != TYPE_BACKGROUND) {
        c = c->next;
    }
    return c->op == TYPE_BACKGROUND;
}

void run_list(command* c) {
    while (c){
        if (cond_in_background(c)){
            pid_t p = fork();
            if (p==0){
                run_conditional(c);
                _exit(EXIT_SUCCESS);
            }
        }
        else{
            run_conditional(c);
        }
        //move to first command of next conditional
        while (c->op != TYPE_BACKGROUND && c->op != TYPE_SEQUENCE){
            c = c->next;
        }
        c = c->next;
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

command* parse_line(const char* s) {
    shell_parser parser(s);
    command* chead = nullptr;    // first command in list
    command* clast = nullptr;    // last command in list
    command* ccur = nullptr;     // current command being built
    for (auto it = parser.begin(); it != parser.end(); ++it) {
        switch (it.type()) {
            case TYPE_NORMAL:
                // Add a new argument to the current command.
                // Might require creating a new command.
                if (!ccur) {
                    ccur = new command;
                    if (clast) {
                        clast->next = ccur;
                        ccur->prev = clast;
                    } else {
                        chead = ccur;
                    }
                }
                ccur->args.push_back(it.str());
                break;
            case TYPE_REDIRECT_OP:
                assert(ccur);
                if (it.str() == "<"){
                    ccur->stdin_redir = true;
                    ++it;
                    ccur->stdin_file = it.str();
                }
                else if (it.str() == ">"){
                    ccur->stdout_redir = true;
                    ++it;
                    ccur->stdout_file = it.str();
                }
                else{
                    ccur->stderr_redir = true;
                    ++it;
                    ccur->stderr_file = it.str();
                }
                break;
            default:
                // These operators terminate the current command.
                assert(ccur);
                clast = ccur;
                clast->op = it.type();
                ccur = nullptr;
                break;
        }
    }
    return chead;
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    bool quiet = false;

    // Check for `-q` option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            return 1;
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            if (command* c = parse_line(buf)) {
                run_list(c);
                delete c;
            }
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        // Your code here!
        while (waitpid(-1, nullptr, WNOHANG) > 0){}
    }

    return 0;
}
