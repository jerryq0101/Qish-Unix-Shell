#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h> 
#include <sys/wait.h>
#include <ctype.h>

// Formatting
void parse_command_line(char* parsed_input, char** args);
void null_terminate_input(char* parsed_input, char* raw_input);
void collapse_white_space_group(char *dest, char *input);

void parse_operator_in_args(char ***args, const char symbol);

// Built-in-command handlers
void handle_cd(char **args);
void handle_path(char **args);
void handle_exit(char **args);

// Finding the correct PATH dir
void select_search_path(char *path, char* name);

// freeing stuff
void free_nested_arr(char** nested);

// Input redirection
void configure_redirection(char **args);

// Piped Commands
void execute_piped_command(char **args);

// Parallel Command
int configure_parallel(char ***arg_list, char **args);

// Makes life easier
void add_bin_path_automatically();
void add_path(char** search_paths, int index, const char* path);

#define MAXLINE 100
#define MAXARGS 200
#define MAXPATHS 100
#define CONCAT_PATH_MAX 100
#define MAX_PARALLEL_COMMANDS 100
#define ERROR_MESSAGE "An error has occurred\n"

char* search_paths[MAXPATHS * sizeof(char*)];

int main(int argc, char *argv[])
{
        char* input = malloc(MAXLINE);
        if (input == NULL)
        {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                exit(1);
        }
        int batch_mode = 0;


        if (freopen("input.txt", "r", stdin) == NULL)
        {
                perror("error opening stdin");
        }

        // Handle Batch mode
        if (argc > 1)
        {
                if (argc > 2)                                           // Catching case where there is many file inputs
                {
                        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                        exit(1);
                }
                batch_mode = 1;

                int fd = open(argv[1], O_RDONLY);
                if ((fd = open(argv[1], O_RDONLY)) == -1)
                {
                        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                        exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
        }
        
        add_bin_path_automatically();

        while (1)                                                       // Main While loop
        {
                if (!batch_mode)                                        // Interactive mode prompt
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
                null_terminate_input(parsed_input, input);
                collapse_white_space_group(parsed_input, parsed_input);

                // Generate arguments across the line
                // SIZE: 20 * sizeof(char*) = 20 * 8 = 160 (20 strings vs 20 bytes)
                char **args = malloc(MAXARGS * sizeof(char*));  
                if (args == NULL)
                {
                        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                        continue;
                }
                parse_command_line(parsed_input, args);
                parse_operator_in_args(&args, '&');
                parse_operator_in_args(&args, '|');
                
                // Check for parallel commands
                char** command_arg_list[MAX_PARALLEL_COMMANDS] = {0};
                int parallel_commands = configure_parallel(command_arg_list, args);


                // Create a pipe
                int p[2];
                if (pipe(p) < 0)
                {
                        fprintf(stderr, ERROR_MESSAGE);
                        continue;
                }
                
                for (int i = 0; command_arg_list[i] != NULL; i++)
                {
                        char **single_command = command_arg_list[i];

                        // Check if built in command (exit, cd, path)
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
                        
                        int has_pipe = 0;
                        for (int i = 0 ; single_command[i] != NULL; i++)
                        {
                                if (strcmp(single_command[i], "|") == 0)
                                {
                                        has_pipe = 1;
                                        break;
                                }
                        }

                        // has a pipe in the external command
                        if (has_pipe)
                        {
                                execute_piped_command(single_command);
                        }
                        else
                        {
                                // default execution code
                                // Single child process for now.
                                pid_t process = fork();
                                if (process < 0)
                                {
                                        printf("fork failed\n");
                                }
                                else if (process == 0)
                                {
                                        char path[CONCAT_PATH_MAX] = {0};                                     // 100 characters MAX
                                        select_search_path(path, single_command[0]);                          // finds suitable search path out of search_path
                                
                                        // check the final element of the command for |
                                        // This signals the first element giving the output
                                        
                                        configure_redirection(single_command);                        
                                        execv(path, single_command);
                                        
                                        // if execv failed
                                        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                                        exit(1);                                                        // Exit the child process
                                }
                                // parent waits for children
                        }
                }
                while (wait(NULL) > 0);

                // TODO: ANALYSE CHANGED WAY OF FREEING MEMORY
                // Free mem
                free_nested_arr(args);
                free(args);
        }
        // Free global vars at the end
        free_nested_arr(search_paths);
        free(input);
}


void execute_piped_command(char **args)
{
        int pipe_count = 0;
        for (int i = 0; args[i] != NULL; i++)
        {
                if (strcmp(args[i], "|") == 0)
                {
                        pipe_count++;
                }
        }

        // a struct that represents a command
        struct Command {
                char** command;                 // Command array
                int need_redirection;           // If there is a file redirection in here
                int personal_pipe[2];           // Personal pipe for holding redirection
                char* file_name;                // file to redirect to
                int pipe_to_read_from;          // pipe to read from
                int pipe_to_write_to;           // pipe to write to
        };

        struct Command commands[sizeof(struct Command) * (pipe_count + 1)];
        int cmd_index = 0;

        commands[0] = (struct Command){args, 0, 0, 0, 0, 0};

        // Divides the command based on the location of the pipe operator
        for (int i = 0; args[i] != NULL; i++)
        {
                if (strcmp(args[i], "|") == 0)
                {
                        args[i] = NULL;
                        commands[++cmd_index] = (struct Command){&args[i + 1], 0, 0, 0, 0, 0};
                }
        }

        // check if there is need for for all the commands, if so setup necessary pipes for it
        for (int i = 0; commands[i].command != NULL; i++)
        {
                for (int j = 0; commands[i].command[j] != NULL; j++)            // loop until the command has ended
                {
                        if (strcmp(commands[i].command[j], ">") == 0)                      // found redirection operator
                        {
                                commands[i].need_redirection = 1;
                                commands[i].file_name = strdup(commands[i].command[j+1]);               // Get file name
                                pipe(commands[i].personal_pipe);                                        // setup pipe of struct
                                
                                // Set the command to break here
                                commands[i].command[j] = NULL;
                                break;
                        }
                }
                // if need_redirection is 0
                // personal pipe should be 0
                // file_name should be 0
        }

        // creates pipes for shared use
        int pipes[pipe_count][2];

        // Make the pipes
        for (size_t i = 0; i < pipe_count; i++)
        {
                if (pipe(pipes[i]) < 0)
                {
                        fprintf(stderr, ERROR_MESSAGE);
                        exit(1);
                }
        }
        
        // Setup individual redirection
        for (int i = 0; i < pipe_count+1; i++)                  // Loop through the commands
        {
                if (i < pipe_count)
                {
                        commands[i].pipe_to_write_to = pipes[i][1];     // the current pipe's write end
                }
                if (i > 0)
                {
                        commands[i].pipe_to_read_from = pipes[i-1][0];  // the previous pipe's read end
                }
                // Each command should write to its corresponding pipe, except for the last one
        }
        commands[0].pipe_to_read_from = STDIN_FILENO;
        commands[pipe_count].pipe_to_write_to = STDOUT_FILENO;


        // Calling now
        for (int i = 0; commands[i].command != NULL; i++)
        {
                struct Command current_command = commands[i];
                if (current_command.need_redirection)
                {
                        // TODO: define a maximum for terminal output
                        char buffer[4096];
                        int bytes_read;
                        
                        int file_fd = open(current_command.file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        
                        pid_t child = fork();
                        if (child < 0)
                        {
                                fprintf(stderr, "FORK FAILED");
                                return;
                        }
                        else if (child == 0)
                        {
                                dup2(current_command.pipe_to_read_from, STDIN_FILENO);
                                close(current_command.pipe_to_read_from);
                                close(current_command.personal_pipe[0]);
                                dup2(current_command.personal_pipe[1], STDOUT_FILENO);
                                close(current_command.personal_pipe[1]);

                                char path[CONCAT_PATH_MAX] = {0};
                                select_search_path(path, current_command.command[0]);

                                execv(path, current_command.command);
                                exit(1);
                        }

                        close(current_command.personal_pipe[1]);

                        while ((bytes_read = read(current_command.personal_pipe[0], 
                                buffer, 
                                sizeof(buffer))))
                        {
                                write(file_fd, buffer, bytes_read);
                                write(current_command.pipe_to_write_to, buffer, bytes_read);
                        }

                        close(current_command.personal_pipe[0]);
                }
                else
                {

                        pid_t child = fork();
                        if (child < 0)
                        {
                                fprintf(stderr, "FORK FAILED");
                                return;
                        }
                        else if (child == 0)
                        {
                                dup2(current_command.pipe_to_read_from, STDIN_FILENO);
                                close(current_command.pipe_to_read_from);
                                dup2(current_command.pipe_to_write_to, STDOUT_FILENO);
                                close(current_command.pipe_to_write_to);

                                char path[CONCAT_PATH_MAX] = {0};
                                select_search_path(path, current_command.command[0]);

                                execv(path, current_command.command);
                                exit(1);
                        }

                }
                

        }

        // // Parent waits for all children to finish
        // for (int i = 0; i <= pipe_count; i++)
        // {
        //         waitpid(child_pids[i], NULL, 0);
        // }
}

// configure_parallel - returns number of parallel commands there are
int configure_parallel(char ***arg_list, char **args)
{
        int commands_count = 0;
        int index = 0;
        int set_pointer = 1;

        // Handle single & case
        if (args[0] != NULL && strcmp("&", args[0]) == 0 && args[1] == NULL) {
                return 0;  // No commands to run
        }

        int ended_with_amp = 0;

        // assume args terminates by the null pointer
        while (args[index] != NULL)
        {
                if (set_pointer)                                // Case: not &
                {
                        // Error if first token is &
                        if (strcmp("&", args[index]) == 0) {
                                return -1;  // Invalid format
                        }

                        // Set the command arglist to be the beginning of each command
                        arg_list[commands_count] = args+index;
                        set_pointer = 0;
                }
                else if (strcmp("&", args[index]) == 0)         // Case: yes &
                {
                        args[index] = NULL;
                        set_pointer = 1;
                        ended_with_amp = 1;
                        commands_count++;
                }
                index++;
        }

        return commands_count + (set_pointer ? 0 : 1);
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
// parses arguments from null terminated char arr, puts them into array of strings
// Also handles the > & operators not having a space
void parse_command_line(char* parsed_input, char** args)
{
        // parse input in here and set those variables
        // btw: args need to be NULL terminated
        char* token;
        int arg_count = 0;

        while ((token = strsep(&parsed_input, " ")) != NULL) {
                if (*token == '\0') continue;
                
                char* redirect = strchr(token, '>');

                if (redirect != NULL) {
                        // We found a > character
                        // Case 1: token is just ">" 
                        if (strlen(token) == 1) {
                                args[arg_count++] = strdup(">");
                                continue;
                        }
                        
                        // Case 2: ">filename"
                        if (redirect == token) {
                                args[arg_count++] = strdup(">");
                                args[arg_count++] = strdup(redirect + 1);
                                continue;
                        }
                        
                        // Case 3: "filename>"
                        if (*(redirect + 1) == '\0') {
                                *redirect = '\0';  // Split at >
                                args[arg_count++] = strdup(token);
                                args[arg_count++] = strdup(">");
                                continue;
                        }
                        
                        // Case 4: "filename>filename"
                        *redirect = '\0';  // Split at >
                        args[arg_count++] = strdup(token);
                        args[arg_count++] = strdup(">");
                        args[arg_count++] = strdup(redirect + 1);
                        continue;
                }
                
                // TODO: for true correctness, I have to split this and the pipeline processing into three parts

                // Normal token without >
                args[arg_count++] = strdup(token);
        }
        args[arg_count] = NULL;
}


// parse_operator_in_args: looks through the current args for ampersands and formats them correctly
void parse_operator_in_args(char ***args, const char symbol)
{
        char symbol_str_form[2];
        symbol_str_form[0] = symbol;
        symbol_str_form[1] = '\0';
        
        int index = 0;
        int new_args_index = 0;

        char** new_args = malloc(MAXARGS * sizeof(char *));
        char** dereferenced_args = *args;

        while (dereferenced_args[index] != NULL)
        {
                char* parallel = strchr(dereferenced_args[index], symbol);
                if (parallel != NULL)                           // Character exists in this string
                {
                        // Process the item until no more & is found
                        char* current = dereferenced_args[index];                    // index of current position in string
                        
                        // Case: the | is the only thing in here
                        if (strlen(current) == 1)
                        {
                                new_args[new_args_index++] = strdup(symbol_str_form);
                                index++;
                                continue;
                        }
                        while (1) {
                                parallel = strchr(current, symbol);

                                // Case: there are no more &s
                                if (parallel == NULL)
                                {
                                        if (*current != '\0')            // Case: if there are more characters at this point
                                        {
                                                new_args[new_args_index++] = strdup(current);
                                        }
                                        break;
                                }
                                

                                // Case: there are more &s
                                // terminate this character in current string
                                *parallel = '\0';
                                
                                // add the part before if not empty
                                if (*current != '\0')
                                {
                                        new_args[new_args_index++] = strdup(current);
                                }
                                
                                // add the & token
                                new_args[new_args_index++] = strdup(symbol_str_form);
                                
                                current = parallel+1;
                        }
                }
                else
                {
                        new_args[new_args_index++] = strdup(dereferenced_args[index]);
                }
                index++;
        }
        new_args[new_args_index] = NULL;

        free_nested_arr(dereferenced_args);
        free(*args);
        *args = new_args;
}



// BUILT IN COMMAND HANDLERS
void handle_cd(char **args)
{
        int res = chdir(*(args+1));
        if (res == -1)
        {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                return;
        }
}


void handle_exit(char **args)
{
        // since we got rid of any spaces, any existence of non space character must be at the second arg
        char *second_arg = args[1];

        if (second_arg != NULL)
        {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                return;
        }
        exit(0);
}


void handle_path(char **args)
{
        int count = 0;
        int index = 1;
        while (args[index] != NULL)
        {
                add_path(search_paths, count, args[index]);
                index++;
                count++;
        }
        // Even if path has no arguments, we clear the search paths
        search_paths[count] = NULL;                     // Note: empty search_paths[i] starts 0x0
}


void select_search_path(char *path, char* name)
{
        int count = 0;
        char temp[100] = {0};
        while (search_paths[count] != NULL)
        {
                strcpy(temp, search_paths[count]);
                strcat(temp, name);
                
                if (access(temp, X_OK) == 0)
                {
                        strcpy(path, temp);
                        return;
                }
                count++;
        }

}


// configure_redirection - check for redirection operators and modify the arg list to fit
// PRECONDITION: args is a single process' command, therefore there shouldn't exist stuff after the filename
// NOTE that item of args is terminated by a NULL pointer, therefore we check *(args+count) != NULL. 
void configure_redirection(char **args)
{
        int count = 0;
        while (args[count] != NULL && strcmp(args[count], ">") != 0) 
        {
                count++;
        }
        
        if (args[count] == NULL)                                                // Case: no redirection operators
        {
                return;
        }
        if (args[count+1] == NULL || strcmp(args[count+1], ">") == 0)           // Case: doesn't exist any valid file/directory after the redirection character
        {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                exit(1);                                                        // Exits this specific process, keeps looking for the next command though.
        }
        if (args[count+1] != NULL && args[count+2] != NULL)                     // check for multiple redirection operators / files to the right
        {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                exit(1);
        }
        
        // Delete redirection operator by null termination
        args[count] = NULL;
        
        // Setup redirection
        // Note: *(args+count) is > character
        int fd = open(*(args+count+1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                exit(1);
        }
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


void add_path(char** search_paths, int index, const char* path) {
    search_paths[index] = malloc(strlen(path) + 2);
    if (search_paths[index] == NULL) {
        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
        exit(1);
    }
    strcpy(search_paths[index], path);
    strcat(search_paths[index], "/");
}


void add_bin_path_automatically()
{
    add_path(search_paths, 0, "/bin");
    add_path(search_paths, 1, "/usr/bin");
    add_path(search_paths, 2, "/sbin");
    search_paths[3] = NULL;
}
