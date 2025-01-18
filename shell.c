#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>


void generate_execv_args(char* parsed_input, char** args);
void null_terminate_input(char* parsed_input, char* raw_input);

#define MAXLINE 100
#define MAXARGS 20      // Including the NULL TERMINATOR
#define MAXNAME 10       

int main(void)
{
        char* input = malloc(MAXLINE);
        // if (freopen("input.txt", "r", stdin) == NULL)
        // {
        //         perror("error opening stdin");
        // }

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

                // free arguments' memory
                for (int i = 0; args[i] != NULL; i++)
                {
                        free(args[i]);
                }
                free(args);

                // do its 
                printf("\n");
        }
        // Free input memory at the end
        free(input);
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

