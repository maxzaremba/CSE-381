/**
 * Copyright (C) 2021 zarembmj@miamioh.edu
 *
 * This program uses the ChildProcess class to create a custom shell 
 * that can run a command entered by the user. This shell also provides:
 *
 *    1. A 'serial' command where commands in a given
 *    shell script are run serially (one after the other).
 *
 *    2. A 'parallel' command where commands in a given
 *    shell script (text file or URL) are run in parallel.
 */

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>

using namespace boost::asio;
using namespace boost::asio::ip;

#include "ChildProcess.h"

// Shortcut to refer to a list of child processes
// This class is defined in ChildProcess.h
using ProcessList = std::vector<ChildProcess>;

// Early declaration of processScript to establish scope
void processScript(const std::string& input, bool parallel);

/** Convenience method to split a given line into individual words.
 *
 *  @param[in] line The line to be split into individual words.
 *
 *  @return A vector strings containing the list of words. 
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

/**
 * Helper method to fork and run a given command.  
 * This method uses the ChildProcess class to run the command.
 *
 * @note This method assumes that the first entry in argList is
 * the command to be run.
 *
 * @param argList The command and its corresponding 
 * arguments to be run by this method.
 *
 * @return This method returns an instance of the ChildProcess class
 * that was used to run the command.
 */
ChildProcess runCmds(const StrVec& argList) {
    // First we print the command we're running
    std::cout << "Running:";
    for (auto& arg : argList) {
        std::cout << " " << arg;
    }
    std::cout << std::endl;

    // Create an instance of ChildProcess to run the command
    ChildProcess child;
    child.forkNexec(argList);
    // Return child for further operations
    return child;
}

/**
 * The primary method to read and run commands.
 *
 * @param is The input stream where the commands are read.
 *
 * @param prompt The string to be displayed to the user.
 * Displays the traditional ">" by default. 
 * 
 * @param parallel If true, then the commands are run in parallel. 
 * If false, the commands are run serially.
 */
void processCmds(std::istream& is, const std::string& prompt = "> ",
        const bool parallel = false) {
    // Parallel only
    ProcessList childList;

    // Process each line until we encounter the exit command.
    std::string line;
    while (std::cout << prompt, std::getline(is, line) && (line != "exit")) {
        if (line.empty() || (line[0] == '#')) {
            continue;
        }

        // Splits line into individual words
        const StrVec argList = split(line);

        // Operations differ depending on command
        if ((argList[0] == "SERIAL") || (argList[0] == "PARALLEL")) {
            // Process input
            processScript(argList[1], argList[0] == "PARALLEL");

        } else {
            // Create a child process and run the command.
            ChildProcess child = runCmds(argList);
            if (!parallel) {
                // Serial only
                // We have to wait for child to finish and print the exit code
                std::cout << "Exit code: " << child.wait() << std::endl;

            } else {
                // Parallel only
                // We have to track each child process created, so we add it
                // to this convenient vector.
                childList.push_back(child);
            }
        }
    }
    // Parallel only
    // After all of the child processes have been created, we need to wait for
    // each one to finish.
    for (auto& proccess : childList) {
        std::cout << "Exit code: " << proccess.wait() << std::endl;
    }
}

/**
 * Helper method to break down a URL into hostname, port and path. For
 * example, given the url: "https://localhost:8080/~raodm/one.txt"
 * this method returns <"localhost", "8080", "/~raodm/one.txt">
 *
 * Similarly, given the url: "ftp://ftp.files.miamioh.edu/index.html"
 * this method returns <"ftp.files.miamioh.edu", "80", "/index.html">
 *
 * @param url A string containing a valid URL. The port number in URL
 * is always optional.  The default port number is assumed to be 80.
 *
 * @return This method returns a std::tuple with 3 strings. The 3
 * strings are in the order: hostname, port, and path.  Here we use
 * std::tuple because a method can return only 1 value.  The
 * std::tuple is a convenient class to encapsulate multiple return
 * values into a single return value.
 */
std::tuple<std::string, std::string, std::string>
breakDownURL(const std::string& url) {
    std::string hostName, port = "80", path = "/";

    const size_t hostStart = url.find("//") + 2;
    const size_t pathStart = url.find('/', hostStart);
    const size_t portPos = url.find(':', hostStart);
    const size_t hostEnd = (portPos == std::string::npos ? pathStart :
            portPos);

    hostName = url.substr(hostStart, hostEnd - hostStart);
    path = url.substr(pathStart);
    if (portPos != std::string::npos) {
        port = url.substr(portPos + 1, pathStart - portPos - 1);
    }

    return {hostName, port, path};
}

/**
 * Helper method that sets up an HTTP stream to download the
 * script to be run from a supplied URL. Basically copied from HW 1.
 *
 * @param url The URL to be processed.
 */
void serveClient(const std::string& url, tcp::iostream& client) {
    std::string hostname, port, path;
    std::tie(hostname, port, path) = breakDownURL(url);

    client.connect(hostname, port);
    client << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << hostname << "\r\n"
            << "Connection: Close\r\n\r\n";

    for (std::string hdr; std::getline(client, hdr) &&
            !hdr.empty() && hdr != "\r";) {
    }
}

/**
 * It finally appears! 
 * Helper method that handles running a script from a given file or a
 * URL.  This method is called from the process() method and then
 * recursively calls the process() method.
 *
 * @param input The file or URL to be processed.
 *
 * @param parallel If true then this method processes 
 * each command in the script in parallel.
 */
void processScript(const std::string& input, bool parallel) {
    if (input.find("http://") == 0) {
        tcp::iostream client;
        serveClient(input, client);
        processCmds(client, "", parallel);

    } else {
        std::ifstream script(input);
        processCmds(script, "", parallel);
    }
}

/**
 * The main method just calls the processCmds method 
 * to run the commands from the console.
 */
int main() {
    processCmds(std::cin);
}
