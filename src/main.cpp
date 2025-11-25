#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "ollama.hpp"
#include "shell.hpp"
#include "completion.hpp"

enum class Mode {
    Agent,
    Shell
};

// Helper to trim whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

int main() {
    std::cout << "=== Terminal AI (C++ Version) ===" << std::endl;

    // Initialize components
    Ollama ollama;
    Shell shell;
    setup_readline();

    // Fetch models
    std::cout << "Fetching models..." << std::endl;
    auto models = ollama.list_models();
    if (models.empty()) {
        std::cerr << "No models found or Ollama not running." << std::endl;
        return 1;
    }

    // Simple model selection (default to first)
    std::string selected_model = models[0];
    std::cout << "Using model: " << selected_model << std::endl;

    // System prompt
    std::string system_prompt = R"(
    You are a Linux Terminal Assistant running on Arch Linux (Fish Shell).
    [IMPORTANT RULES]
    1. Before answering, you MUST provide your thinking process enclosed in <think> and </think> tags.
    2. If the user asks to perform a system action, you MUST output the command inside a code block labeled 'execute'.
    Example:
    <think>User wants to update npm.</think>
    ```execute
    npm update -g
    ```
    )";

    std::vector<Message> history;
    history.push_back({"system", system_prompt});

    Mode current_mode = Mode::Agent;
    std::regex re_think(R"(<think>([\s\S]*?)</think>)");
    std::regex re_execute(R"(```execute\s*([\s\S]*?)\s*```)");

    while (true) {
        std::string prompt;
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            strcpy(cwd, "unknown");
        }
        std::string cwd_str(cwd);

        if (current_mode == Mode::Agent) {
            prompt = "\n(Agent) >>> ";
        } else {
            prompt = "\n(Shell:" + cwd_str + ") $ ";
        }

        char* input_cstr = readline(prompt.c_str());
        if (!input_cstr) {
            std::cout << "\nBye!" << std::endl;
            break;
        }

        std::string input(input_cstr);
        if (!input.empty()) {
            add_history(input_cstr);
        }
        free(input_cstr);

        input = trim(input);
        if (input.empty()) continue;

        if (input == "exit" || input == "quit") {
            break;
        }

        // Mode switching
        if (input == "!shell") {
            current_mode = Mode::Shell;
            std::cout << "Switched to Shell Mode." << std::endl;
            continue;
        } else if (input == "!agent") {
            current_mode = Mode::Agent;
            std::cout << "Switched to Agent Mode." << std::endl;
            continue;
        } else if (input == "!model") {
            std::cout << "Fetching models..." << std::endl;
            auto current_models = ollama.list_models();
            if (current_models.empty()) {
                std::cerr << "No models found." << std::endl;
                continue;
            }
            
            std::cout << "Available models:" << std::endl;
            for (size_t i = 0; i < current_models.size(); ++i) {
                std::cout << i + 1 << ". " << current_models[i] << std::endl;
            }
            
            char* selection = readline("Select model (number): ");
            if (selection) {
                int idx = atoi(selection);
                if (idx > 0 && idx <= (int)current_models.size()) {
                    selected_model = current_models[idx - 1];
                    std::cout << "Switched to model: " << selected_model << std::endl;
                } else {
                    std::cout << "Invalid selection." << std::endl;
                }
                free(selection);
            }
            continue;
        }

        if (current_mode == Mode::Shell) {
            // Handle cd command manually
            if (input.rfind("cd ", 0) == 0 || input == "cd") {
                std::string path;
                if (input == "cd") {
                    const char* home = getenv("HOME");
                    if (home) path = home;
                } else {
                    path = trim(input.substr(3));
                }

                if (!path.empty()) {
                    if (chdir(path.c_str()) != 0) {
                        perror("cd failed");
                    }
                }
                continue;
            }

            std::string output = shell.execute(input);
            // Add to history for AI context
            history.push_back({"user", "Executed Shell Command: " + input + "\nOutput:\n" + output});
        } else {
            history.push_back({"user", input});
            std::cout << "Thinking..." << std::flush;
            
            std::string response = ollama.chat(selected_model, history);
            std::cout << "\r\033[K"; // Clear "Thinking..." line

            // Parse <think>
            std::smatch match;
            std::string final_answer = response;
            if (std::regex_search(response, match, re_think)) {
                std::string thought = match[1].str();
                std::cout << "\n\033[1;90m🧠 Thinking Process:\033[0m" << std::endl;
                std::cout << "\033[90m\033[3m" << trim(thought) << "\033[0m\n" << std::endl;
                std::cout << "\033[1;90m----------------------------------------\033[0m\n" << std::endl;
                
                final_answer = std::regex_replace(response, re_think, "");
            }
            
            std::cout << trim(final_answer) << std::endl;
            history.push_back({"assistant", response});

            // Parse execute block
            if (std::regex_search(response, match, re_execute)) {
                std::string command = trim(match[1].str());
                std::cout << "\n[!] AI wants to execute:\n\033[33m" << command << "\033[0m" << std::endl;
                
                char* confirm = readline("Execute? (y/n) ");
                if (confirm && (strcmp(confirm, "y") == 0 || strcmp(confirm, "Y") == 0)) {
                    std::cout << "Running..." << std::endl;
                    std::string output = shell.execute(command);
                    history.push_back({"user", "System Output: " + output});
                } else {
                    std::cout << "Cancelled." << std::endl;
                    history.push_back({"user", "User cancelled execution."});
                }
                if (confirm) free(confirm);
            }
        }
    }

    return 0;
}
