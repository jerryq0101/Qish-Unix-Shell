#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h> 
#include <ctype.h>

// Formatting
void generate_execv_args(char* parsed_input, char** args);
void null_terminate_input(char* parsed_input, char* raw_input);
void collapse_white_space_group(char *dest, char *input);

// Built-in-command handlers
void handle_cd(char **args);
void handle_path(char **args);
void select_search_path(char *path, char* name);

// freeing stuff
void free_nested_arr(char** nested);

// Redirection
void configure_redirection(char **args);

// Makes life easier
void add_bin_path_automatically();

#define MAXLINE 100
#define MAXARGS 100
#define MAXPATHS 100
#define CONCAT_PATH_MAX 100

char* search_paths[MAXPATHS * sizeof(char*)];

int main(int argc, char *argv[])
{
        char* input = malloc(MAXLINE);

        // if (freopen("input.txt", "r", stdin) == NULL)
        // {
        //         perror("error opening stdin");
        // }

        int batch_mode = 0;

        // Handle Batch mode
        if (argc > 1)
        {
                if (argc > 2)   // Catching case where there is many file inputs
                {
                        fprintf(stderr, "Expected 1 file input\n");
                        exit(1);
                }

                // Toggle Batch mode
                batch_mode = 1;

                // redirection of stdin to the file
                int fd = open(argv[1], O_RDONLY);
                if ((fd = open(argv[1], O_RDONLY)) == -1)
                {
                        perror("Error opening specified file");
                        exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
        }
        
        add_bin_path_automatically();

        while (1)              // Main While loop
        {
                if (!batch_mode)        // Interactive mode prompt
                {
                        printf("process> ");
                }
                size_t variable = (long) MAXLINE;
                getline(&input, &variable, stdin);

                if (*input == EOF || input == NULL || *input == '\0')   // Handle input termination on batch mode
                {
                        exit(0);
                }
                
                char parsed_input[MAXLINE];

                null_terminate_input(parsed_input, input);              // parse line

                // Get rid of groups of white spaces, until the \n symbol
                collapse_white_space_group(parsed_input, parsed_input);
                
                printf("PARSED INPUT! %s\n", parsed_input);
                
                // Generate execv arguments / redirection arguments or parallel commands
                // SIZE: 20 * sizeof(char*) = 20 * 8 = 160 (20 strings vs 20 bytes)
                char **args = malloc(MAXARGS * sizeof(char*));  
                generate_execv_args(parsed_input, args);

                printf("AfTER GENERATING EXECV FIRST ELEMENT ARGS: %s\n", args[0]);


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
                char path[CONCAT_PATH_MAX];                                     // 100 characters MAX
                for (int i = 0 ; search_paths[i] != NULL; i++)
                {
                        printf("search_path item: %s\n", search_paths[i]);
                }
                printf("ARGS[0] BEFORE : %s\n", args[0]);
                select_search_path(path, args[0]);         // finds suitable search path out of search_path and puts it in path, if not...
                printf("ARGS[0] AFTER : %s\n", args[0]);

                // strcat(path, args[0]);
                printf("Selected Search Path: %s\n", path);

                // Single child process for now.
                pid_t process = fork();
                if (process < 0)
                {
                        printf("fork failed\n");
                }
                else if (process == 0)
                {
                        // Print before redirection
                        printf("Child start - path=%s\n", path);
                        for (int i = 0; args[i] != NULL; i++) {
                                printf("Child start - args[%d]=%s\n", i, args[i]);
                        }

                        configure_redirection(args);

                        // Print after redirection
                        printf("Child after redirection - path=%s\n", path);
                        for (int i = 0; args[i] != NULL; i++) {
                                printf("Child after redirection - args[%d]=%s\n", i, args[i]);
                        }

                        // DEBUG!!
                        FILE *f = fopen("debug.txt", "a");
                        fprintf(f, "Debug child: path=%s\n", path);
                        fclose(f);

                        // process, we have access to parsed input.
                        execv(path, args);
                        // char *args[] = {"ls", "-l", NULL};
                        // execv("/bin/ls", args);
                        
                        // if execv failed
                        perror("Error executing command");
                        exit(1);
                }

                // parent waits for children
                wait(NULL);

                // Free mem
                // TODO: not sure why this breaks when I free args.
                free_nested_arr(args);
                free(args);

                // Universally, there should always be a newline after each command (batch mode and interactive mode)
                printf("\n");
        }
        
        // Free global vars at the end
        free_nested_arr(search_paths);
        free(input);                            // free input
}

void free_nested_arr(char** nested)
{
        // free arguments' memory
        for (int i = 0; nested[i] != NULL; i++)
        {
                if (nested[i] != NULL)
                {
                        free(nested[i]);
                }
        }
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

void generate_execv_args(char* parsed_input, char** args)
{
        // parse input in here and set those variables
        // btw: args need to be NULL terminated
        char* token;

        int count = 0;
        while ((token = strsep(&parsed_input, " ")) != NULL)    // each token is nul terminated
        {
                // this position will set the arr[count] for first 2 as 8 byte pointer, 
                // as soon as we arrive at the 3rd token, the args memory block is not enough, 
                // so the function likely terminates
                // When we write beyond the allocated memory (storing it in the third pointer spot),
                // anything can happen to the entire args array because I am corrupting memory.
                args[count] = malloc(strlen(token)+1);
                strcpy(args[count], token);
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

void select_search_path(char *path, char* name)
{
        int count = 0;
        char temp[100];
        printf("SELECT SEARCH PATH FUNCTION\n");
        printf("NAME: %s\n", name);
        while (search_paths[count] != NULL)
        {
                strcpy(temp, search_paths[count]);
                strcat(temp, name);
                if (!access(temp, X_OK))
                {
                        strcpy(path, temp);
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
                exit(1);                                                        // Exits this specific process, keeps looking for the next command though.
        }

        // DON"T HAVE TO CHECK THE COMMAND and NAME validity, just have to check that characters exist in the format
        // TODO: potentially more error checking cases

        // Delete redirection operator by null termination
        *(args+count) = NULL;
        
        // Setup redirection
        // Note: *(args+count) is > character
        int fd = open(*(args+count+1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDOUT_FILENO);
        close(fd);
}

// Collapses groups of blank space into one
// Between the last char and /0 space, delete any blank space
// PRECONDITION: all space (group) positionings are correct
void collapse_white_space_group(char *dest, char *input)
{
        int count = 0;
        int insertion_index = 0;

        // Collapses all space groups into one space
        while (input[count] != '\0')
        {
                if (isspace(input[count]))                    // Found a space at this position
                {
                        dest[insertion_index] = ' ';          // Place a space at this position
                        while (isspace(input[count]))         // Go to next non space char.
                        {
                                count++;
                        }
                        insertion_index++;
                }
                else                                          // Character here
                {
                        dest[insertion_index] = input[count];
                        count++;
                        insertion_index++;
                }

        }
        // Check if slot before is a space, then null terminate it before.
        if (dest[insertion_index-1] == ' ')
        {
                dest[insertion_index-1] = '\0';
        }
        else
        {
                dest[insertion_index] = '\0';
        }
}

void add_bin_path_automatically()
{
        search_paths[0] = malloc(6);
        strcpy(search_paths[0], "/bin/");
        search_paths[1] = malloc(9);
        strcpy(search_paths[1], "/usr/bin/");
        search_paths[2] = malloc(7);
        strcpy(search_paths[2], "/sbin/");
        search_paths[3] = NULL;
}
