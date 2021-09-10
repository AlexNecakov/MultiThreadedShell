
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define TRUE 1
#define STD_INPUT 0
#define STD_OUTPUT 1


void type_prompt();
char** read_command();
void execArgs(char** args);

//sigaction struct and handler
void zombieHandler(int sig){
    pid_t pid;
    int status;
    while(waitpid(-1,&status,WNOHANG) >0); 
}


int main(int argc, char* argv[]){
    
    //check for -n flag
    int noPrompt = 0;
    if(argc > 1){
            int i;
            for(i=0; i<argc; i++){
                if(strcmp(argv[i],"-n") == 0){
                    noPrompt = 1;
                }
            }
    }

    char** sepInput = malloc(sizeof(char*)*512);
    //main loop
    while(TRUE){
    
        (!noPrompt)?type_prompt():0;
        sepInput = read_command();
        execArgs(sepInput);
    }
}

//print prompt
void type_prompt(){
    printf("my_shell$");
}

//read input, store command and parameters
char** read_command(){
    
    //get raw input
    char line[512];
    //because we're adding whitespace, this string can potentiall be more than 512 chars
    char* pipeSplitArguments= (char*)malloc(sizeof(char)*1024);
    //but even then we have a max of 512 args, ie if every other arg is 1 char, then 1 pipe
    char** arguments = (char**)malloc(sizeof(char*)*512);
    
    
    if(fgets(line,512,stdin) == NULL){
        if(feof(stdin)){
            printf("\n");
            exit(EXIT_SUCCESS);
        } else{
            perror("ERROR: Failed to read input\n");
            return;
        }
    }

    //to deal with no spacing consideration on input for piping, we pad all piping characters with whitespace to tokenize them individually
    int currRaw = 0;
    int currNew = 0;
    while(line[currRaw] != '\0'){
        if((line[currRaw]=='|')||(line[currRaw]=='>')||(line[currRaw]=='<')||(line[currRaw]=='&')){
            pipeSplitArguments[currNew++] = ' ';
            pipeSplitArguments[currNew++] = line[currRaw++];
            pipeSplitArguments[currNew++] = ' ';

        } else{
            pipeSplitArguments[currNew++] = line[currRaw++];
        }

    }

    //tokenize input on whitespace
    char* token;
    const char* delim = " \n\t";
    int tokenNum = 0;

    token = strtok(pipeSplitArguments, delim);

    while(token != NULL) {

        //check max toxen length
        if(strlen(token) > 32){
            perror("ERROR: Token larger than maximum token size\n");
            return;
        }
        arguments[tokenNum++] = token;

        token = strtok(NULL, delim);
    }
    
    //set final arg to be NULL for passing into exec
    arguments[tokenNum] = NULL;

    return arguments;
}


//execute based on argument list
void execArgs(char** args){

    
    //keep new argv array to store all actual execvp input and also the pipes
    char** argv = malloc(sizeof(char*)*512);
    int numArgs = 0;
    int numArgsNoSpecial = 0;
    int numCommands = 0;

    //we are also flagging to see if the other special character occur here
    int inputRedirect = 0;
    int outputRedirect = 0;
    int runInBackground = 0;

    char* inputFileName;
    char* outputFileName;
    

    //loop through arglist to build big argv list
    while(args[numArgs] != NULL){
        
        //split by pipe into new commands, also check to see if output has already been redirected
        if(strcmp(args[numArgs],"|") == 0){
            if(outputRedirect == 1){
                perror("ERROR: Only the last command can have output redirected\n");
                return;
            } else if(runInBackground == 1){
                perror("ERROR: & must be after the final command");
                return;
            } else{
                //set last arg in each command to be NULL
                //argv[numArgsNoSpecial++] = NULL;
                argv[numArgsNoSpecial++] = args[numArgs++];      
                numCommands++;
            }
        }

        //only first command can get input redirected
        else if(strcmp(args[numArgs],"<") == 0){
            if(numCommands == 0){
                inputRedirect = 1;
                //skip filename as a command and store that to be opened
                inputFileName = args[++numArgs];
                numArgs++;
            } else{
                perror("ERROR: Only the first command can have input redirected\n");
                return;
            } 
        }  

        //set redirect output to file flag
        else if(strcmp(args[numArgs],">") == 0){
            outputRedirect = 1;
            //skip filename as a command and store that to be opened
            outputFileName = args[++numArgs];
            numArgs++;
        }

        //set background process flag
        else if(strcmp(args[numArgs],"&") == 0){
            runInBackground = 1;
            numArgs++;
        }

        //now we actually build up each command, basically the argv for every command in our pipeline
        else{            
            argv[numArgsNoSpecial++] = args[numArgs++];

        }

    }

    //argv[numArgsNoSpecial] = NULL;
    numCommands++;

    int status;
    pid_t pid;
    int fd[2];
    int fdNext = 0;
    int i;

    //open up processes and link end to end, also redirecting file i/o if flagged
    for(i = 0; i < numCommands; i++){

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sa.sa_handler = zombieHandler;
        sigaction(SIGCHLD, &sa, NULL); 
        

        //build temporary argv of just the current command
        int numPipes = 0;
        int j;
        int numArgsTemp = 0;
        char** argvTemp = malloc(sizeof(char*)*512);
        for(j = 0; j < numArgsNoSpecial; j++){
            if(strcmp(argv[j],"|") == 0){
                
                numPipes++;
            }
            else{
                if(numPipes==i){
                    argvTemp[numArgsTemp++] = argv[j];
                }
            }


        }
        argvTemp[numArgsTemp] = NULL;

        //create a pipe
        if(pipe(fd) < 0){
            perror("ERROR: Failed to pipe");
            return;
        }
        pid = fork();
        
        
        if(pid==0){
            
            //check to see if first command or last command needs file redirection
            if(inputRedirect){
                FILE* inFile = fopen(inputFileName,"r");
                if(inFile == NULL){
                    perror("ERROR: Input file could not be opened\n");
                    fclose(inFile);
                    return;
                }
                dup2(fileno(inFile),STD_INPUT);
                fclose(inFile);
            }
            if(outputRedirect){
                FILE* outFile = fopen(outputFileName,"w");
                if(outFile == NULL){
                    perror("ERROR: Output file could not be opened\n");
                    fclose(outFile);
                    return;
                }
                dup2(fileno(outFile),STD_OUTPUT);
                fclose(outFile);
            }
            
            //now set piping between commands, except not into the first command or out of the last one
            //not first command
            if(i > 0){
                if(dup2(fdNext,STD_INPUT)<0){
                    perror("ERROR: Failed to assign input in pipe\n");
                    return;
                }
            }
            //not last command
            if(i < numCommands-1){
                if(dup2(fd[1],STD_OUTPUT)<0){
                    perror("ERROR: Failed to assign output in pipe\n");
                    return;
                }
            }

            close(fd[0]);

            execvp(argvTemp[0],argvTemp);
            perror("ERROR: Process not replaced\n");
            return;
        } else if(pid<0){
            perror("ERROR: Error creating process\n");
            return;
        } else{
            //use wnohang to background process but still catch
            if(runInBackground){
                waitpid(-1,&status,WNOHANG);
                close(fd[1]);
                //fdnext will now be our input into next process in pipeline
                fdNext = fd[0];
            }else{
                waitpid(-1,&status,0); 
                close(fd[1]);
                //fdnext will now be our input into next process in pipeline
                fdNext = fd[0];
            }

        }
    }

       
}