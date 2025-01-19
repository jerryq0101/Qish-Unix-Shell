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
void handle_exit(char **args);

// Finding the correct PATH dir
void select_search_path(char *path, char* name);

// freeing stuff
void free_nested_arr(char** nested);

// Redirection
void configure_redirection(char **args);

// Parallel Command
int configure_parallel(char ***arg_list, char **args);

// Makes life easier
void add_bin_path_automatically();

#define MAXLINE 100
#define MAXARGS 100
#define MAXPATHS 100
#define CONCAT_PATH_MAX 100
#define MAX_PARALLEL_COMMANDS 100

char* search_paths[MAXPATHS * sizeof(char*)];

int main(int argc, char *argv[])
{
        char* input = malloc(MAXLINE);
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
                
                // TODO: INPUT PARSING FOR > and & operators (no need space)
                char parsed_input[MAXLINE];
                null_terminate_input(parsed_input, input);              // parse line
                collapse_white_space_group(parsed_input, parsed_input);
                
                // printf("PARSED INPUT! %s\n", parsed_input);

                // Check for parallel commands
                
                // Generate execv arguments / redirection arguments or parallel commands
                // SIZE: 20 * sizeof(char*) = 20 * 8 = 160 (20 strings vs 20 bytes)
                char **args = malloc(MAXARGS * sizeof(char*));  
                generate_execv_args(parsed_input, args);

                // printf("AfTER GENERATING EXECV FIRST ELEMENT ARGS: %s\n", args[0]);

                // generated arguments that are cleanly separated
                char** command_arg_list[MAX_PARALLEL_COMMANDS];
                int parallel_commands = configure_parallel(command_arg_list, args);
                // configure_parallel modifies the command arg list s.t.
                // there is an char **args like sequence at each memory slot until NULL ptr

                for (int i = 0; command_arg_list[i] != NULL; i++)
                {
                        char **single_command = command_arg_list[i];
                        // printf("COMMAND SELECTION %s\n", single_command[0]);

                        // Check if built in command (exit, cd, path)
                        // TODO: handle these in the child process
                        if (strcmp("exit", single_command[0]) == 0)
                        {
                                handle_exit(single_command);
                                continue;
                        }
                        if (strcmp("cd", single_command[0]) == 0)
                        {
                                handle_cd(single_command);
                                continue;
                        }
                        if (strcmp("path", single_command[0]) == 0)
                        {
                                handle_path(single_command);
                                continue;
                        }

                        // Not a built in command
                        // check if process exists in the different search paths
                        char path[CONCAT_PATH_MAX];                                     // 100 characters MAX
                        // for (int i = 0 ; search_paths[i] != NULL; i++)
                        // {
                        //         printf("search_path item: %s\n", search_paths[i]);
                        // }
                        // printf("ARGS[0] BEFORE : %s\n", single_command[0]);

                        // TODO: HANDLE THE CASE WHERE ITS NOT A COMMAND BUT A SHELL SCRIPT
                        select_search_path(path, single_command[0]);         // finds suitable search path out of search_path and puts it in path, if not...
                        
                        // printf("ARGS[0] AFTER : %s\n", single_command[0]);
                        // printf("ARGS[1] AFTER : %s\n", single_command[1]);


                        // strcat(path, args[0]);
                        // printf("Selected Search Path: %s\n", path);

                        // Single child process for now.
                        pid_t process = fork();
                        if (process < 0)
                        {
                                printf("fork failed\n");
                        }
                        else if (process == 0)
                        {
                                // // Print before redirection
                                // printf("Child start - path=%s\n", path);
                                // for (int i = 0; single_command[i] != NULL; i++) {
                                //         printf("Child start - args[%d]=%s\n", i, single_command[i]);
                                // }

                                configure_redirection(single_command);

                                // // Print after redirection
                                // printf("Child after redirection - path=%s\n", path);
                                // for (int i = 0; single_command[i] != NULL; i++) {
                                //         printf("Child after redirection - args[%d]=%s\n", i, single_command[i]);
                                // }

                                // // DEBUG!!
                                // FILE *f = fopen("debug.txt", "a");
                                // fprintf(f, "Debug child: path=%s\n", path);
                                // fclose(f);

                                // process, we have access to parsed input.
                                execv(path, single_command);
                                // char *args[] = {"ls", "-l", NULL};
                                // execv("/bin/ls", args);
                                
                                // if execv failed
                                fprintf(stderr, "An error has occurred\n");
                                exit(1);                // Exit the child process
                        }
                }

                // parent waits for children
                while (wait(NULL) > 0);

                // Free mem
                free_nested_arr(args);
                free(args);

                // Universally, there should always be a newline after each command (batch mode and interactive mode)
                // printf("\n");
        }
        
        // Free global vars at the end
        free_nested_arr(search_paths);
        free(input);                            // free input
}

int configure_parallel(char ***arg_list, char **args)
{
        // Strategy;
        // go through the args allocated storage,
        // don't allocate anymore for arg_list
        // when we find a &, set that to NULL, 
        // and continue on the next element
        int commands_count = 0;
        int index = 0;
        int set_pointer = 1;

        // assume args terminates by the null pointer
        while (args[index] != NULL)
        {
                if (set_pointer)                        // not &
                {
                        // set the command arglist to be the beginning of each command
                        arg_list[commands_count] = args+index;
                        set_pointer = 0;
                }
                else if (strcmp("&", args[index]) == 0)      // yes &
                {
                        args[index] = NULL;
                        set_pointer = 1;
                        commands_count++;
                }
                index++;
        }

        return commands_count - 1;
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
        int res = chdir(*(args+1));
        if (res == -1)
        {
                fprintf(stderr, "An error has occurred\n");
                return;
        }
}

void handle_exit(char **args)
{
        // since we got rid of any spaces, any existence of non space character must be at the second arg
        char *second_arg = args[1];

        if (second_arg != NULL)
        {
                fprintf(stderr, "An error has occurred\n");
                return;
        }
        exit(0);
}

void handle_path(char **args)
{
        args++;
        int count = 0;
        while (*(args) != NULL)
        {
                search_paths[count] = malloc(strlen(*(args)) + 3);
                if (search_paths[count] == NULL)
                {
                        fprintf(stderr, "An error has occurred\n");
                        return;
                }
                strcpy(search_paths[count], *(args));
                strcat(search_paths[count], "/");
                

                args++;
                count++;
        }
        search_paths[count] = NULL;
        // Note: empty search_paths[i] are 0x0 naturally
}

void select_search_path(char *path, char* name)
{
        int count = 0;
        char temp[100];
        // printf("SELECT SEARCH PATH FUNCTION\n");
        // printf("NAME: %s\n", name);
        while (search_paths[count] != NULL)
        {
                strcpy(temp, search_paths[count]);
                strcat(temp, name);
                
                // printf("complete path searching for: %s\n", temp);
                // char cwd[1024];
                // getcwd(cwd, sizeof(cwd));
                // printf("Current working directory: %s\n", cwd);
                if (access(temp, X_OK) == 0)
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
        if (*(args+count+1) == NULL || strcmp(*(args+count+1) == 0, ">"))           // Case: doesn't exist any valid file/directory after the redirection character
        {
                fprintf(stderr, "Invalid token after redirection character");
                exit(1);                                                        // Exits this specific process, keeps looking for the next command though.
        }

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

