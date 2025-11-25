#pragma once

#include <string>
#include <fstream>
#include <iostream>

class FileOperations {
public:
    static bool write_file(const std::string& path, const std::string& content) {
        std::ofstream outfile(path);
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open file for writing: " << path << std::endl;
            return false;
        }
        outfile << content;
        outfile.close();
        return true;
    }
};
