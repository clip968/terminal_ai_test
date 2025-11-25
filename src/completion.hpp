#pragma once

#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// Helper to get all executables in PATH
std::vector<std::string> get_executables() {
    std::vector<std::string> execs;
    std::string path_env = std::getenv("PATH");
    std::string delimiter = ":";
    size_t pos = 0;
    std::string token;
    
    std::set<std::string> seen;

    // Add builtins
    std::vector<std::string> builtins = {"cd", "exit", "quit", "history", "help", "export", "alias", "unalias"};
    for (const auto& b : builtins) {
        seen.insert(b);
        execs.push_back(b);
    }

    while ((pos = path_env.find(delimiter)) != std::string::npos) {
        token = path_env.substr(0, pos);
        if (fs::exists(token) && fs::is_directory(token)) {
            for (const auto& entry : fs::directory_iterator(token)) {
                if (entry.is_regular_file()) {
                    // Check executable permission? For now just name.
                    // In C++17 fs::perms::owner_exec is available but let's just list all files for speed/simplicity or check perms.
                    // auto p = entry.status().permissions();
                    // if ((p & fs::perms::owner_exec) != fs::perms::none) ...
                    
                    std::string name = entry.path().filename().string();
                    if (seen.find(name) == seen.end()) {
                        seen.insert(name);
                        execs.push_back(name);
                    }
                }
            }
        }
        path_env.erase(0, pos + delimiter.length());
    }
    // Last token
    if (fs::exists(path_env) && fs::is_directory(path_env)) {
        for (const auto& entry : fs::directory_iterator(path_env)) {
            if (entry.is_regular_file()) {
                std::string name = entry.path().filename().string();
                if (seen.find(name) == seen.end()) {
                    seen.insert(name);
                    execs.push_back(name);
                }
            }
        }
    }
    return execs;
}

// Global list for generator
static std::vector<std::string> command_candidates;

// Generator function for commands
char* command_generator(const char* text, int state) {
    static size_t list_index, len;
    
    if (!state) {
        list_index = 0;
        len = strlen(text);
        // Refresh candidates only once or cache them? 
        // For performance, let's cache them globally or refresh if empty.
        if (command_candidates.empty()) {
            command_candidates = get_executables();
        }
    }

    while (list_index < command_candidates.size()) {
        const std::string& name = command_candidates[list_index];
        list_index++;
        if (strncmp(name.c_str(), text, len) == 0) {
            return strdup(name.c_str());
        }
    }

    return nullptr;
}

// Custom completion function
char** my_completion(const char* text, int start, int end) {
    rl_attempted_completion_over = 1; // Prevent default completion if we handle it

    // Check if we are at the start of the line (command position)
    // start is the index in the line buffer.
    // We need to check if there are non-whitespace characters before 'start'.
    // rl_line_buffer is the full line.
    
    bool is_command = true;
    for (int i = 0; i < start; ++i) {
        if (!isspace(rl_line_buffer[i])) {
            is_command = false;
            break;
        }
    }

    if (is_command) {
        return rl_completion_matches(text, command_generator);
    } else {
        // Fallback to filename completion
        rl_attempted_completion_over = 0; // Let readline handle filename completion
        return nullptr;
    }
}

void setup_readline() {
    rl_attempted_completion_function = my_completion;
}
