/* 
 * A custom shell that uses fork() and execvp() for running commands
 * in serially or in parallel.
 * 
 */

#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>

// A vector of strings to ease running programs with command-line
// arguments.
using StrVec = std::vector<std::string>;

// A vector of integers to hold child process ID's when operating in
// parallel mode.
using IntVec = std::vector<int>;

// Suggestion for generic method to read & run commands
void processCmds(std::istream& is, std::ostream& os, bool parMode,
                 const std::string prompt);  

/** Convenience method to split a given line into individual words.

    \param[in] line The line to be split into individual words.

    \return A vector strings containing the list of words. 
 */
StrVec split(const std::string& line) {
    StrVec words;  // The list to be created
    // Use a string stream to read individual words 
    std::istringstream is(line);
    std::string word;
    while (is >> std::quoted(word)) {
        words.push_back(word);
    }
    return words;
}

