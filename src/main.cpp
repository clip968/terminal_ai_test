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
#include "utils.hpp"
#include "file_ops.hpp"

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
    1. First, analyze the user's request and write your thinking process enclosed in <think> and </think> tags.
    2. YOU MUST CLOSE THE </think> TAG BEFORE WRITING YOUR FINAL RESPONSE.
    3. The content inside <think>...</think> is for your internal reasoning only. The user will not see it as the main answer.
    4. After </think>, write the actual response to the user.
    5. If the user asks to perform a system action, output the command inside a code block labeled 'execute'.
    6. To WRITE a file, use a code block labeled 'write:filename'.
    7. To READ a file, use 'cat filename' inside an 'execute' block.
    8. NEVER use the 'execute' or 'write' tags for examples or explanations. Only use them when you intend to trigger an actual action.
    9. If you want to show an example of code creation, just use a normal code block without the 'write:' prefix.
    10. You MUST answer in Korean.

    Example (Write):
    <think>User wants to create main.py.</think>
    I will create the file for you.
    ```write:main.py
    print("Hello World")
    ```

    Example (Read):
    <think>User wants to read main.py.</think>
    I will read the file.
    ```execute
    cat main.py
    ```
    )";

    std::vector<Message> history;
    history.push_back({"system", system_prompt});

    Mode current_mode = Mode::Agent;
    std::regex re_think(R"(<think>([\s\S]*?)</think>)");
    std::regex re_execute(R"(```execute\s*([\s\S]*?)\s*```)");
    std::regex re_write(R"(```write:([^\s`]+)\s*([\s\S]*?)\s*```)");

    bool auto_continue = false;
    while (true) {
        std::string input;
        if (!auto_continue) {
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

        input = input_cstr;
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
            if (!auto_continue) {
                history.push_back({"user", input});
            } else {
                std::cout << ANSI::CYAN << "(Auto-continuing...)" << ANSI::RESET << std::endl;
                auto_continue = false;
            }
            std::cout << "Thinking..." << std::flush;
            
            // Streaming state
            bool is_thinking = false;
            std::string full_response;
            
            // Clear "Thinking..." line before streaming starts
            std::cout << "\r\033[K"; 

            auto stream_callback = [&](const std::string& chunk) -> bool {
                full_response += chunk;
                
                // Simple state machine for coloring <think> blocks
                // Note: This is a basic implementation. For perfect handling of split tags, 
                // a more robust parser would be needed, but this suffices for typical token-by-token output.
                
                std::string display_chunk = chunk;
                
                // Check for <think> start
                size_t think_start = display_chunk.find("<think>");
                if (think_start != std::string::npos) {
                    is_thinking = true;
                    // Print part before <think>
                    std::cout << display_chunk.substr(0, think_start);
                    std::cout << ANSI::GRAY << ANSI::ITALIC << "🧠 Thinking Process:\n" << ANSI::GRAY;
                    // Print part after <think>
                    std::cout << display_chunk.substr(think_start + 7);
                    return true; 
                }
                
                // Check for </think> end
                size_t think_end = display_chunk.find("</think>");
                if (think_end != std::string::npos) {
                    is_thinking = false;
                    // Print part before </think>
                    std::cout << display_chunk.substr(0, think_end);
                    std::cout << ANSI::RESET << "\n" << ANSI::GRAY << "----------------------------------------" << ANSI::RESET << "\n";
                    // Print part after </think>
                    std::cout << display_chunk.substr(think_end + 8);
                    return true;
                }
                
                if (is_thinking) {
                    std::cout << ANSI::GRAY << display_chunk;
                } else {
                    std::cout << display_chunk;
                }
                std::cout << std::flush;
                
                return true;
            };

            std::string response = ollama.chat(selected_model, history, stream_callback);
            
            // If response was built via streaming, use full_response. 
            // However, ollama.chat returns the full text anyway in our implementation.
            // But let's ensure we have the final clean output for history.
            if (response.empty() && !full_response.empty()) {
                response = full_response;
            }
            
            // Add a newline at the end if not present
            if (!response.empty() && response.back() != '\n') {
                std::cout << std::endl;
            }

            // Parse <think> for history storage
            std::smatch match;
            std::string final_answer = response;
            if (std::regex_search(response, match, re_think)) {
                // We already displayed the thought process. 
                // Just remove it for the "final_answer" variable if needed for other logic.
                final_answer = std::regex_replace(response, re_think, "");
            }
            
            // We already printed the output via stream, so we don't need to print it again using MarkdownRenderer.
            // However, the stream output didn't use MarkdownRenderer for the non-thinking part (it just printed raw chunks).
            // If we want nice markdown (colors for code blocks), we might want to re-print?
            // But that would duplicate the output.
            // Ideally, the stream callback should handle markdown rendering.
            // For now, let's accept that streaming might be raw text, or we can clear screen and reprint?
            // No, clearing screen is bad UX for long outputs.
            // Let's just keep the raw stream output for now, as requested "real-time".
            // The user asked for "process", so raw text is acceptable.
            // But wait, the original code used MarkdownRenderer.
            // If we want to support markdown in stream, we need a streaming markdown parser.
            // That's complex. 
            // Alternative: After streaming is done, we could re-render the *final answer* part?
            // But that would duplicate.
            // Let's stick to raw output for now, but maybe we can apply basic coloring in the callback if possible.
            // For this task, let's just ensure we don't double print.
            
            // std::cout << MarkdownRenderer::render(trim(final_answer)) << std::endl; // REMOVED to avoid double print
            history.push_back({"assistant", response});

            // Parse execute block
            if (std::regex_search(response, match, re_execute)) {
                std::string command = trim(match[1].str());
                std::cout << "\n[!] AI wants to execute:\n" << ANSI::YELLOW << command << ANSI::RESET << std::endl;
                
                char* confirm = readline("Execute? (y/n) ");
                if (confirm && (strcmp(confirm, "y") == 0 || strcmp(confirm, "Y") == 0)) {
                    std::cout << "Running..." << std::endl;
                    std::string output = shell.execute(command);
                    history.push_back({"user", "System Output: " + output});
                    auto_continue = true;
                } else {
                    std::cout << "Cancelled." << std::endl;
                    history.push_back({"user", "User cancelled execution."});
                }
                if (confirm) free(confirm);
            }

            // Parse write block
            if (std::regex_search(response, match, re_write)) {
                std::string filename = trim(match[1].str());
                std::string content = match[2].str();
                // Trim leading/trailing newline from content if present
                if (!content.empty() && content.front() == '\n') content.erase(0, 1);
                if (!content.empty() && content.back() == '\n') content.pop_back();

                std::cout << "\n[!] AI wants to WRITE to file: " << ANSI::CYAN << filename << ANSI::RESET << std::endl;
                std::cout << "Content preview:\n" << ANSI::GRAY << content.substr(0, 100) << (content.length() > 100 ? "..." : "") << ANSI::RESET << std::endl;
                
                char* confirm = readline("Write file? (y/n) ");
                if (confirm && (strcmp(confirm, "y") == 0 || strcmp(confirm, "Y") == 0)) {
                    if (FileOperations::write_file(filename, content)) {
                        std::cout << "File written successfully." << std::endl;
                        history.push_back({"user", "System: File " + filename + " written successfully."});
                        auto_continue = true;
                    } else {
                        std::cout << "Failed to write file." << std::endl;
                        history.push_back({"user", "System: Failed to write file " + filename});
                    }
                } else {
                    std::cout << "Cancelled." << std::endl;
                    history.push_back({"user", "User cancelled file write."});
                }
                if (confirm) free(confirm);
            }
        }
    }

    return 0;
}
