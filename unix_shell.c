#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_LINE 80 
#define DELIMITERS " \t\n\v\f\r"

size_t parse(char *args[], char *command) {
    size_t num = 0;
    char p[MAX_LINE + 1];
    strcpy(p, command);  
    char *token = strtok(p, DELIMITERS);
    while(token != NULL) {
        args[num] = malloc(strlen(token) + 1);
        strcpy(args[num], token);
        ++num;
        token = strtok(NULL, DELIMITERS);
    }
    return num;
}
void argsv(char *args[]) {
    for(size_t i = 0; i != MAX_LINE / 2 + 1; ++i) {
        args[i] = NULL;
    }
}
void refresh_args(char *args[]) {
    while(*args) {
        *args++ = NULL;
    }
}
void commandv(char *command) {
    strcpy(command, "");
}
int get_input(char *command) {
    char buffer[MAX_LINE + 1];
    if(fgets(buffer, MAX_LINE + 1, stdin) == NULL) {
        fprintf(stderr, "Failed to read input!\n");
        return 0;
    }
    if(strncmp(buffer, "!!", 2) == 0) {
        if(strlen(command) == 0) {  
            fprintf(stderr, "No history available yet!\n");
            return 0;
        }
        printf("%s", command);    
        return 1;
    }
    strcpy(command, buffer);  
    return 1;
}
int ampersand(char **args, size_t *size) {
    size_t len = strlen(args[*size - 1]);
    if(args[*size - 1][len - 1] != '&') {
        return 0;
    }
    if(len == 1) {  
        free(args[*size - 1]);
        args[*size - 1] = NULL;
        --(*size);  
    } else {
        args[*size - 1][len - 1] = '\0';
    }
    return 1;
}
unsigned redirection(char **args, size_t *size, char **in_file, char **out_file) {
    unsigned num = 0;
    size_t redi[4], redi_cnt = 0;
    for(size_t i = 0; i != *size; ++i) {
        if(redi_cnt >= 4) {
            break;
        }
        if(strcmp("<", args[i]) == 0) {     // in
            redi[redi_cnt++] = i;
            if(i == (*size) - 1) {
                fprintf(stderr, "No input file provided!\n");
                break;
            }
            num |= 1;
            *in_file = args[i + 1];
            redi[redi_cnt++] = ++i;
        } else if(strcmp(">", args[i]) == 0) {   // out
            redi[redi_cnt++] = i;
            if(i == (*size) - 1) {
                fprintf(stderr, "No output file provided!\n");
                break;
            }
            num |= 2;
            *out_file = args[i + 1];
            redi[redi_cnt++] = ++i;
        }
    }
    for(int i = redi_cnt - 1; i >= 0; --i) {
        size_t pos = redi[i];  
        while(pos != *size) {
            args[pos] = args[pos + 1];
            ++pos;
        }
        --(*size);
    }
    return num;
}
int redirect(unsigned io_num, char *in_file, char *out_file, int *in_desc, int *out_desc) {
    if(io_num & 2) { 
        *out_desc = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 123);
        if(*out_desc < 0) {
            fprintf(stderr, "Failed to open the output file: %s\n", out_file);
            return 0;
        }
        dup2(*out_desc, STDOUT_FILENO);
    }
    if(io_num & 1) { 
        *in_desc = open(in_file, O_RDONLY, 0123);
        if(*in_desc < 0) {
            fprintf(stderr, "Failed to open the input file: %s\n", in_file);
            return 0;
        }
        dup2(*in_desc, STDIN_FILENO);
    }
    return 1;
}
void close_file(unsigned io_num, int in_desc, int out_desc) {
    if(io_num & 2) {
        close(out_desc);
    }
    if(io_num & 1) {
        close(in_desc);
    }
}
void find_pipe(char **args, size_t *args_num, char ***args2, size_t *args_num2) {
    for(size_t i = 0; i != *args_num; ++i) {
        if (strcmp(args[i], "|") == 0) {
            free(args[i]);
            args[i] = NULL;
            *args_num2 = *args_num -  i - 1;
            *args_num = i;
            *args2 = args + i + 1;
            break;
        }
    }
}
int run_command(char **args, size_t args_num) {
    int ampersands = ampersand(args, &args_num);
    char **args2;
    size_t args_num2 = 0;
    find_pipe(args, &args_num, &args2, &args_num2);
    pid_t pid = fork();
    if(pid < 0) {  
        fprintf(stderr, "Failed to fork!\n");
        return 0;
    } else if (pid == 0) { 
        if(args_num2 != 0) {    
            int fd[2];
            pipe(fd);
            pid_t pid2 = fork();
            if(pid2 > 0) {  
                char *in_file, *out_file;
                int in_desc, out_desc;
                unsigned io_num = redirection(args2, &args_num2, &in_file, &out_file);   
                io_num &= 2;   
                if(redirect(io_num, in_file, out_file, &in_desc, &out_desc) == 0) {
                    return 0;
                }
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
                wait(NULL);   
                execvp(args2[0], args2);
                close_file(io_num, in_desc, out_desc);
                close(fd[0]);
                fflush(stdin);
            } else if(pid2 == 0) {  
                char *in_file, *out_file;
                int in_desc, out_desc;
                unsigned io_flag = redirection(args, &args_num, &in_file, &out_file);    
                io_flag &= 1;   
                if(redirect(io_flag, in_file, out_file, &in_desc, &out_desc) == 0) {
                    return 0;
                }
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                execvp(args[0], args);
                close_file(io_flag, in_desc, out_desc);
                close(fd[1]);
                fflush(stdin);
            }
        } else {    
            char *in_file, *out_file;
            int in_desc, out_desc;
            unsigned io_num = redirection(args, &args_num, &in_file, &out_file);    // bit 1 for output, bit 0 for input
            if(redirect(io_num, in_file, out_file, &in_desc, &out_desc) == 0) {
                return 0;
            }
            execvp(args[0], args);
            close_file(io_num, in_desc, out_desc);
            fflush(stdin);
        }
    } else { 
        if(!ampersands) { 
            wait(NULL);
        }
    }
    return 1;
}
int main(void) {
    char *input[MAX_LINE / 2 + 1]; 
    char last_command[MAX_LINE + 1];
    argsv(input);
    commandv(last_command);
    while (1) {
        printf("osh>");
        fflush(stdout);
        fflush(stdin);
        refresh_args(input);
        if(!get_input(last_command)) {
            continue;
        }
        size_t args_num = parse(input, last_command);
        if(args_num == 0) { 
            printf("No command in history\n");
            continue;
        }
        if(strcmp(input[0], "exit") == 0) {
            break;
        }
        run_command(input, args_num);
    }
    refresh_args(input);    
    return 0;
}