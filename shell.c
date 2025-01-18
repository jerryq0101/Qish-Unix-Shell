#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>


#define MAXLINE 100

int main(void)
{
        char* input = malloc(MAXLINE);
        if (freopen("input.txt", "r", stdin) == NULL)
        {
                perror("error opening stdin");
        }

        // while (1)              // Main While loop
        // {
        printf("process>");
        size_t variable = (long) MAXLINE;
        getline(&input, &variable, stdin);
        
        // Parse the line
        char parsed_input[MAXLINE];
        int count = 0;
        while (*input != '\n')
        {
                parsed_input[count] = *input;
                input++;
                count++;
        }
        parsed_input[count] = '\0';
        
        pid_t process = fork();
        if (process < 0)
        {
                printf("fork failed\n");
        }
        else if (process == 0)
        {
                // child process, and do stuff in here
                char *args[] = {parsed_input, "-l", NULL};
                execv("/bin/ls", args);

                // if execv failed
                exit(1);
        }

        // parent thing

        // do its 
        printf("\n");
        // }
}

// Command

