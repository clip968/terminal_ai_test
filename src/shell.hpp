#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <array>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

class Shell {
public:
    // Executes a command and streams output to stdout, returning the full output as string
    std::string execute(const std::string& command) {
        int pipefd[2]; // Pipe for stdout/stderr
        if (pipe(pipefd) == -1) {
            return "Error: pipe failed";
        }

        pid_t pid = fork();
        if (pid == -1) {
            return "Error: fork failed";
        }

        if (pid == 0) {
            // Child process
            close(pipefd[0]); // Close read end
            dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
            dup2(pipefd[1], STDERR_FILENO); // Redirect stderr to pipe
            close(pipefd[1]); // Close write end

            // Execute command using shell
            // We use /bin/sh or fish if available. Let's use sh for portability or fish as requested.
            // The user seems to use fish.
            const char* shell = "/usr/bin/fish";
            // Fallback to sh if fish not found? For now hardcode fish or sh.
            // Let's use "sh -c" for generic, or user's SHELL env.
            const char* shell_env = getenv("SHELL");
            if (!shell_env) shell_env = "/bin/sh";

            execl(shell_env, shell_env, "-c", command.c_str(), nullptr);
            
            // If execl returns, it failed
            std::cerr << "Error: exec failed" << std::endl;
            exit(1);
        } else {
            // Parent process
            close(pipefd[1]); // Close write end

            std::string full_output;
            char buffer[1024];
            ssize_t bytes_read;

            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0';
                std::cout << buffer << std::flush; // Stream to stdout
                full_output += buffer;
            }

            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            
            return full_output;
        }
    }
};
