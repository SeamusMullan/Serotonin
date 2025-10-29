/**
 * @file shell.c
 * @brief Simple command-line shell for Serotonin OS
 * 
 * Provides a basic interactive shell that reads commands from stdin,
 * forks child processes to execute them, and waits for completion.
 * The shell runs in a loop until EOF (Ctrl+D) is received.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

/**
 * @brief Main shell loop
 * 
 * Implements a read-eval-execute loop that:
 * 1. Displays a prompt
 * 2. Reads user input
 * 3. Forks a child process
 * 4. Executes the command in the child
 * 5. Waits for the child to complete
 * 
 * @return 0 on normal exit, 1 on error
 */
int main()
{
	char command[256];
	const char *prompt = "serotonin# ";
	const char *read_error = "Error: failed to read input\n";
	const char *fork_error = "Error: failed to fork process\n";
	const char *exec_error = "Error: failed to execute command\n";
	const char *empty_cmd = "Error: empty command\n";
	
	for (;;) {
		if (write(1, prompt, strlen(prompt)) < 0) {
			// If we can't write to stdout, we're fuckin cooked
			_exit(1);
		}
		
		int count = read(0, command, sizeof(command) - 1);
		
		if (count < 0) {
			write(2, read_error, 28);
			continue;
		}
		
		// Handle EOF (Ctrl+D)
		if (count == 0) {
			break;
		}
		
		// Null-terminate the command
		command[count] = '\0';
		
		// Remove trailing newline if present
		if (count > 0 && command[count - 1] == '\n') {
			command[count - 1] = '\0';
			count--;
		}
		
		// Skip empty commands
		if (count == 0 || command[0] == '\0') {
			continue;
		}
		
		// Check for buffer overflow (command too long)
		if (count >= (int)(sizeof(command) - 1)) {
			write(2, "Error: command too long\n", 24);
			// Drain remaining input
			char drain;
			while (read(0, &drain, 1) > 0 && drain != '\n');
			continue;
		}
		
		// Fork the process
		pid_t fork_result = fork();
		
		if (fork_result < 0) {
			// Fork failed
			write(2, fork_error, 28);
			continue;
		} else if (fork_result == 0) {
			// Child process: execute the command
			execve(command, 0, 0);
			
			// If execve returns, it failed
			write(2, exec_error, 30);
			_exit(1);
		} else {
			// Parent process: wait for child
			int status;
			pid_t wait_result = waitpid(fork_result, &status, 0);
			
			if (wait_result < 0) {
				write(2, "Error: failed to wait for child process\n", 41);
			}
			
			// Optionally check exit status
			// if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			//     write(2, "Command exited with non-zero status\n", 37);
			// }
		}
	}

	_exit(0);
}
