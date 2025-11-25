#pragma once

#include <string>
#include <regex>
#include <iostream>
#include <sstream>
#include <vector>

namespace ANSI {
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string DIM = "\033[2m";
    const std::string ITALIC = "\033[3m";
    const std::string UNDERLINE = "\033[4m";

    const std::string BLACK = "\033[30m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string GRAY = "\033[90m";
    
    const std::string BG_BLACK = "\033[40m";
    const std::string BG_RED = "\033[41m";
    const std::string BG_GREEN = "\033[42m";
    const std::string BG_YELLOW = "\033[43m";
    const std::string BG_BLUE = "\033[44m";
    const std::string BG_MAGENTA = "\033[45m";
    const std::string BG_CYAN = "\033[46m";
    const std::string BG_WHITE = "\033[47m";
}

class MarkdownRenderer {
public:
    static std::string render(const std::string& markdown) {
        std::string text = markdown;

        // 1. Code Blocks (```language ... ```)
        // We handle this first to prevent other regexes from messing up code content.
        // However, regex for multiline code blocks can be tricky.
        // Let's iterate through lines to handle code blocks manually for better control.
        
        std::stringstream ss(text);
        std::string line;
        std::string result;
        bool in_code_block = false;
        std::string code_lang;

        while (std::getline(ss, line)) {
            // Trim leading whitespace for code block check
            std::string trimmed_line = line;
            size_t first_non_space = trimmed_line.find_first_not_of(" \t");
            if (first_non_space != std::string::npos) {
                trimmed_line = trimmed_line.substr(first_non_space);
            } else {
                trimmed_line = "";
            }

            // Check for code block delimiter
            if (trimmed_line.rfind("```", 0) == 0) { // Starts with ```
                if (in_code_block) {
                    // End of code block
                    in_code_block = false;
                    result += ANSI::RESET + "\n";
                } else {
                    // Start of code block
                    in_code_block = true;
                    code_lang = trimmed_line.substr(3);
                    // Trim whitespace
                    code_lang.erase(0, code_lang.find_first_not_of(" \t\r\n"));
                    code_lang.erase(code_lang.find_last_not_of(" \t\r\n") + 1);
                    
                    result += "\n" + ANSI::BG_BLACK + ANSI::CYAN + " [" + (code_lang.empty() ? "CODE" : code_lang) + "] " + ANSI::RESET + "\n";
                    result += ANSI::CYAN; // Set color for code content
                }
                continue;
            }

            if (in_code_block) {
                result += line + "\n";
            } else {
                // Apply inline formatting for non-code-block lines
                std::string processed_line = line;

                // Headers
                // # Header -> Bold Magenta Underline
                processed_line = std::regex_replace(processed_line, std::regex(R"(^#\s+(.*))"), ANSI::BOLD + ANSI::MAGENTA + ANSI::UNDERLINE + "$1" + ANSI::RESET);
                processed_line = std::regex_replace(processed_line, std::regex(R"(^##\s+(.*))"), ANSI::BOLD + ANSI::BLUE + "$1" + ANSI::RESET);
                processed_line = std::regex_replace(processed_line, std::regex(R"(^###\s+(.*))"), ANSI::BOLD + ANSI::GREEN + "$1" + ANSI::RESET);

                // Bold (**text**)
                processed_line = std::regex_replace(processed_line, std::regex(R"(\*\*(.*?)\*\*)"), ANSI::BOLD + "$1" + ANSI::RESET);
                
                // Inline Code (`text`)
                processed_line = std::regex_replace(processed_line, std::regex(R"(`([^`]+)`)"), ANSI::BG_BLACK + ANSI::YELLOW + " $1 " + ANSI::RESET);

                // Lists (- item or * item)
                processed_line = std::regex_replace(processed_line, std::regex(R"(^(\s*)[-*]\s+)"), "$1" + ANSI::BOLD + ANSI::YELLOW + "• " + ANSI::RESET);

                result += processed_line + "\n";
            }
        }
        
        return result;
    }
};
