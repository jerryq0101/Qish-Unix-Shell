#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h> 

void generate_execv_args(char* parsed_input, char** args);
void null_terminate_input(char* parsed_input, char* raw_input);

// Built-in-command handlers
void handle_cd(char **args);
void handle_path(char **args);
void select_search_path(char *path);

// freeing stuff
void free_nested_arr(char** nested);

// Redirection
void configure_redirection(char **args);

#define MAXLINE 100
#define MAXARGS 20
#define MAXNAME 10
#define MAXPATHS 10

char* search_paths[MAXPATHS];

int main(int argc, char *argv[])
{
        char* input = malloc(MAXLINE);

        if (freopen("input.txt", "r", stdin) == NULL)
        {
                perror("error opening stdin");
        }

        int batch_mode = 0;

        // Handle Batch mode
        if (argc > 1)
        {
                if (argc > 2)   // Catching case where there is many file inputs
                {
                        fprintf(stderr, "Expected 1 file input\n");
                        exit(1);
                }

                // redirection of stdin to the file
                int fd = open(argv[1], O_RDONLY);
                if ((fd = open(argv[1], O_RDONLY)) == -1)
                {
                        perror("error opening stdin");
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
        }

        
        while (1)              // Main While loop
        {
                printf("process> ");
                size_t variable = (long) MAXLINE;
                getline(&input, &variable, stdin);

                // Check for EOF and end of line
                if (*input == EOF || input == NULL || *input == '\0')
                {
                        exit(0);
                }
                
                // Parse the line
                char parsed_input[MAXLINE];
                null_terminate_input(parsed_input, input);

                // Generate execv arguments or redirection arguments or parallel commands
                char **args = malloc(MAXARGS);
                generate_execv_args(parsed_input, args);

                configure_redirection(args);

                // Check if built in command (exit, cd, path)
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
                        free_nested_arr(args);
                        continue;
                }

                // Not a built in command
                // check if process exists in the different search paths
                char path[10];
                select_search_path(path);         // finds suitable search path out of search_path and puts it in path, if not...
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
                free_nested_arr(args);

                // do its 
                printf("\n");
        }
        
        // Free global vars at the end
        free(input);                    // free input
}

void free_nested_arr(char** nested)
{
        // free arguments' memory
        for (int i = 0; nested[i] != NULL; i++)
        {
                free(nested[i]);
        }
        free(nested);
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
                *(args+count) = malloc(strlen(token)+1);
                strcpy(*(args+count), token);
                count++;
        }
        *(args+count) = NULL;
}


// BUILT IN COMMAND HANDLERS
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
                *(search_paths+count) = malloc(strlen(*(args)) +1);
                strcpy(*(search_paths+count), *(args));
                args++;
                count++;
        }
        *(search_paths+count) = NULL;
        // Note: empty search_paths[i] are 0x0 naturally
        // TODO: ENFORCE ^
}

void select_search_path(char *path)
{
        int count = 0;
        while (search_paths[count] != NULL)
        {
                if (!access(search_paths[count], X_OK))
                {
                        strcpy(path, search_paths[count]);
                        return;
                }
                count++;
        }
}

// configure_redirection - check for redirection operators and modify the arg list to fit
// PRECONDITION: currently dealing with single program redirection
// NOTE that item of args is terminated by a NULL pointer, therefore we check *(args+count) != NULL. 
void configure_redirection(char **args)
{
        int count = 0;
        while (*(args+count) != NULL && strcmp(*(args+count), ">") != 0) 
        {
                count++;
        }
        
        if (*(args+count) == NULL)                                             // Case: no redirection operators
        {
                return;
        }
        if (*(args+count+1) == NULL || !strcmp(*(args+count+1), ">"))           // Case: doesn't exist any valid file/directory after the redirection character
        {
                fprintf(stderr, "Invalid token after redirection character");
                exit(1);
        }

        // DON"T HAVE TO CHECK THE COMMAND and NAME validity, just have to check that characters exist in the format
        // TODO: potentially more error checking cases

        // Delete redirection operator by null termination
        *(args+count) = NULL;
        
        // Setup redirection
        // Note: *(args+count) is > character
        int fd = open(*(args+count+1), O_WRONLY);
        dup2(fd, STDOUT_FILENO);
        close(fd);
}
