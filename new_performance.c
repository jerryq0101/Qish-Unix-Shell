#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#define NUM_ITERATIONS 100
#define COMMAND_SIZE 1024
#define OUTPUT_FILE "benchmark.txt"
#define PROGRESS_BAR_WIDTH 50

// Store all results in a struct
struct BenchmarkResults {
    double parallel_time;
    double redirection_time;
    double builtin_time;
    struct {
        const char* name;
        double time;
    } external_times[20];
    int num_external;
    double external_avg;
    double overall_avg;
};

// Progress bar function
void print_progress(const char* shell_name, const char* test_name, int current, int total) {
    float percentage = (float)current / total * 100;
    printf("\r\033[K[");
    int pos = PROGRESS_BAR_WIDTH * current / total;
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3.0f%% | %-8s | %-20s", percentage, shell_name, test_name);
    fflush(stdout);
}

long get_microseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

double measure_command(const char* shell, const char* command) {
    long start, end;
    int status;
    
    start = get_microseconds();
    
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
        execlp(shell, shell, "-c", command, NULL);
        exit(1);
    }
    
    waitpid(pid, &status, 0);
    end = get_microseconds();
    
    return (end - start) / 1000.0;
}

double measure_parallel_commands(const char* shell) {
    const char* command = (strcmp(shell, "./qish") == 0) ?
        "sleep 0.1 & sleep 0.1 & sleep 0.1" :
        "sleep 0.1 & sleep 0.1 & sleep 0.1 & wait";
    return measure_command(shell, command);
}

double measure_redirection(const char* shell) {
    return measure_command(shell, "echo test > /dev/null");
}

double measure_builtin(const char* shell) {
    return measure_command(shell, "cd .");
}

// Test definitions
struct CommandTest {
    const char* name;
    const char* command;
} external_tests[] = {
    {"Simple ls", "ls"},
    {"File stats", "wc shell.c"},
    {"File preview", "more shell.c"},
    {"File difference", "diff shell.c performance.c"},
    {"Make directory", "mkdir TEST"},
    {"Remove directory", "rmdir TEST"},
    {"Recursive ls", "ls -R /etc"},
    {"File search", "find /etc -name 'hosts'"},
    {"Process list", "ps aux"},
    {"Disk usage", "du -sh /etc"},
    {"System info", "uname -a"},
    {"File count", "ls -1 /etc | wc -l"},
    {NULL, NULL}
};

struct BenchmarkResults run_benchmarks(const char* shell_name, const char* shell_path) {
    struct BenchmarkResults results = {0};
    double total_parallel = 0, total_redirection = 0, total_builtin = 0;
    int total_tests = NUM_ITERATIONS * (3 + 10);
    int current_test = 0;
    
    // Basic tests
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        total_parallel += measure_parallel_commands(shell_path);
        total_redirection += measure_redirection(shell_path);
        total_builtin += measure_builtin(shell_path);
        
        current_test += 3;
        print_progress(shell_name, "Basic Tests", current_test, total_tests);
    }
    printf("\n");
    
    results.parallel_time = total_parallel / NUM_ITERATIONS;
    results.redirection_time = total_redirection / NUM_ITERATIONS;
    results.builtin_time = total_builtin / NUM_ITERATIONS;
    
    // External command tests
    double total_external = 0;
    results.num_external = 0;
    
    for (int i = 0; external_tests[i].command != NULL; i++) {
        double total_time = 0;
        
        for (int j = 0; j < NUM_ITERATIONS; j++) {
            total_time += measure_command(shell_path, external_tests[i].command);
            current_test++;
            print_progress(shell_name, external_tests[i].name, current_test, total_tests);
        }
        
        results.external_times[i].name = external_tests[i].name;
        results.external_times[i].time = total_time / NUM_ITERATIONS;
        total_external += results.external_times[i].time;
        results.num_external++;
    }
    printf("\n");
    
    results.external_avg = total_external / results.num_external;
    results.overall_avg = (results.parallel_time + results.redirection_time + 
                          results.builtin_time + results.external_avg) / 4;
    
    return results;
}

void write_results_to_string(const char* shell_name, struct BenchmarkResults results, char* buffer) {
    int offset = 0;
    
    offset += sprintf(buffer + offset, "\nResults for %s:\n", shell_name);
    offset += sprintf(buffer + offset, "----------------------------------------\n");
    offset += sprintf(buffer + offset, "Basic Command Tests:\n");
    offset += sprintf(buffer + offset, "  Parallel execution time: %.3f ms\n", results.parallel_time);
    offset += sprintf(buffer + offset, "  Redirection time: %.3f ms\n", results.redirection_time);
    offset += sprintf(buffer + offset, "  Built-in command time: %.3f ms\n", results.builtin_time);
    
    offset += sprintf(buffer + offset, "\nExternal Command Tests:\n");
    offset += sprintf(buffer + offset, "%-25s %15s\n", "Command", "Time (ms)");
    offset += sprintf(buffer + offset, "----------------------------------------\n");
    
    for (int i = 0; i < results.num_external; i++) {
        offset += sprintf(buffer + offset, "%-25s %15.3f\n", 
                results.external_times[i].name, 
                results.external_times[i].time);
    }
    
    offset += sprintf(buffer + offset, "\nSummary:\n");
    offset += sprintf(buffer + offset, "  Average external command time: %.3f ms\n", results.external_avg);
    offset += sprintf(buffer + offset, "  Overall average: %.3f ms\n\n", results.overall_avg);
}

int main() {
    // Generate all results first
    struct BenchmarkResults bash_results = run_benchmarks("Bash", "/bin/bash");
    struct BenchmarkResults qish_results = run_benchmarks("qish", "./qish");
    
    // Prepare the complete output in memory
    char* output_buffer = malloc(10000);  // Allocate plenty of space
    char* current_pos = output_buffer;
    
    // Write header
    time_t now = time(NULL);
    current_pos += sprintf(current_pos, "Shell Performance Benchmark Results\n");
    current_pos += sprintf(current_pos, "Date: %s", ctime(&now));
    current_pos += sprintf(current_pos, "Number of iterations per test: %d\n\n", NUM_ITERATIONS);
    
    // Write results for both shells
    write_results_to_string("Bash", bash_results, current_pos);
    current_pos += strlen(current_pos);
    write_results_to_string("qish", qish_results, current_pos);
    
    // Write everything to file at once
    FILE* output = fopen(OUTPUT_FILE, "w");
    if (!output) {
        perror("Failed to open output file");
        free(output_buffer);
        return 1;
    }
    
    fputs(output_buffer, output);
    fclose(output);
    free(output_buffer);
    
    printf("\nBenchmark complete! Results written to %s\n", OUTPUT_FILE);
    return 0;
}
