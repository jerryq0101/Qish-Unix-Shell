#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

void generate_execv_args(char* parsed_input, char** args);
void null_terminate_input(char* parsed_input, char* raw_input);
// Built in function
void handle_cd(char **args);
void handle_path(char **args);

#define MAXLINE 100
#define MAXARGS 20      // Including the NULL TERMINATOR
#define MAXNAME 10       
#define MAXPATHS 10

char* search_paths[MAXPATHS];

int main(void)
{
        char* input = malloc(MAXLINE);
        if (freopen("input.txt", "r", stdin) == NULL)
        {
                perror("error opening stdin");
        }

        while (1)              // Main While loop
        {
                printf("process> ");
                size_t variable = (long) MAXLINE;
                getline(&input, &variable, stdin);
                
                // Parse the line
                char parsed_input[MAXLINE];
                null_terminate_input(parsed_input, input);  

                // Generate execv arguments
                char **args = malloc(MAXARGS);
                generate_execv_args(parsed_input, args);

                // check if this is a built in command (exit, cd, path)
                if (!strcmp("exit", args[0]))
                {
                        exit(0);
                }
                if (!strcmp("cd", args[0]))
                {
                        handle_cd(args);
                        continue;
                }

                if (!strcmp("path", args[0]))
                {
                        handle_path(args);
                        free_args(args);
                        continue;
                }

                // Not a built in command
                char path[10] = "/bin/";
                strcat(path, args[0]);

                // Single child process for now.
                pid_t process = fork();
                if (process < 0)
                {
                        printf("fork failed\n");
                }
                else if (process == 0)
                {
                        // process, we have access to parsed input.
                        execv(path, args);
                        // char *args[] = {"ls", "-l", NULL};
                        // execv("/bin/ls", args);
                        
                        // if execv failed
                        perror("Error executing command");
                        exit(1);
                }

                // parent stuff
                wait(NULL);

                // Free mem
                free_args(args);

                // do its 
                printf("\n");
        }
        // Free input memory at the end
        free(input);
}

void free_args(char** args)
{
        // free arguments' memory
        for (int i = 0; args[i] != NULL; i++)
        {
                free(args[i]);
        }
        free(args);
}

// null_terminate_input - replaces the \n with \0 from raw input
void null_terminate_input(char* parsed_input, char* raw_input)
{
        int count = 0;
        while (*raw_input != '\n')
        {
                parsed_input[count] = *raw_input;
                raw_input++;
                count++;
        }
        parsed_input[count] = '\0';
}

// generate_execv_args - parses and generates the args array for execv.
// program_name - char pointer (string)
// args - an array of char pointers (strings)

// PRECONDITION: dealing with a single program and not handling any redirection
void generate_execv_args(char* parsed_input, char** args)
{
        // parse input in here and set those variables
        // btw: args need to be NULL terminated
        char* token;

        int count = 0;
        while ((token = strsep(&parsed_input, " ")) != NULL)    // each token is nul terminated
        {
                *(args+count) = malloc(strlen(token));
                strcpy(*(args+count), token);
                count++;
        }
        *(args+count) = NULL;
}

void handle_cd(char **args)
{
        chdir(*(args+1));
}

void handle_path(char **args)
{
        args++;
        int count = 0;
        while (*(args) != NULL)
        {
                // TODO: Check if the path is valid
                *(search_paths+count) = malloc(strlen(*(args)));
                strcpy(*(search_paths+count), *(args));
                args++;
                count++;
        }
        *(search_paths+count) = malloc(strlen(*(args)));
        *(search_paths+count) = NULL;
}

