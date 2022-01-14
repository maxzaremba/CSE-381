/** Copyright 2021 zarembmj@miamioh.edu
 * 
 * A program to use multiple threads to count words from data obtained
 * via a given set of URLs.
 */

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cctype>
#include <algorithm>
#include <thread>
#include <unordered_map>

// Using namespace to streamline working with Boost socket
using namespace boost::asio;
using namespace boost::system;

// Shortcut an unordered map of words
using Dictionary = std::unordered_map<std::string, bool>;

// Forward declaration of loadDictionary for use immediately below
Dictionary loadDictionary(const std::string& path);

// The global dictionary of valid words
const Dictionary dictionary = loadDictionary("english.txt");

/** This method returns an unordered map of words from a file 
 *  to use as a dictionary.
 *
 * @param path The file path to the dictionary file.
 *
 * @return The Dictionary containing the list of words loaded from the
 * file supplied to path.
 */
Dictionary loadDictionary(const std::string& path) {
    Dictionary dictionary;
    std::ifstream englishWords(path);
    for (std::string word; englishWords >> word;) {
        dictionary[word] = true;
    }
    return dictionary;
}

/** This method changes the punctuation in a line to blank space.
 *  The line is also converted to lower case.
 *
 * @param The line string supplied.
 *
 * @return A string with all punctuation removed and all letters 
 * converted to lowercase.
 */
std::string changePunct(std::string line) {
    // Remove punctuation in line (and replace with blank space)
    std::replace_if(line.begin(), line.end(), ::ispunct, ' ');
    // Convert each word to lowercase
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);
    return line;
}

/**
 * This method counts the number of total words and the number of valid
 * English words from a given input stream. 
 * Punctuation and special characters are ignored. 
 *
 * @param stream The input stream.
 * 
 * @return A string with the total word count and English word count.
 */
std::string wordCount(std::istream& stream) {
    int wordCount = 0, engWordCount = 0;
    // Processes each line and word
    for (std::string line; std::getline(stream, line);) {
        // Remove punctuation and convert to lowercase
        line = changePunct(line);
        // Process each word
        std::istringstream is(line);
        for (std::string word; is >> word;) {
            wordCount++;
            if (dictionary.find(word) != dictionary.end()) {
                engWordCount++;
            }
        }
    }
    // Return a string with the total word count and English word count
    return ": words=" + std::to_string(wordCount) +
        ", English words=" + std::to_string(engWordCount);
}

/**
 * This method gets the word counts for a given file.
 *
 * A Boost TCP stream from homework 1 is used to send an HTTP GET
 * request to the server and print the response.
 *
 * @param file A string containing the file to be processed.
 * The file path is appended to this url:
 * "http://ceclnx01.cec.miamioh.edu/~raodm/"
 *
 * @return The results of wordCount for the file
 */
std::string getCount(const std::string& file) {
    // Setup the fixed strings required for the TCP stream
    const std::string base = "/~raodm/";
    const std::string host = "ceclnx01.cec.miamioh.edu";
    const std::string resource = base + file;
    // Send request to the HTTP server
    ip::tcp::iostream stream(host, "80");
    stream << "GET " << resource << " HTTP/1.1\r\n";
    stream << "Host: " << host << "\r\n";
    stream << "Connection: Close\r\n\r\n";
    // First read and skip HTTP headers (A classic)
    for (std::string line; std::getline(stream, line) &&
                           !line.empty() && (line != "\r"); ) {}
    // Call wordCount for the count
    auto count = wordCount(stream);
    // Return the result
    return file + count;
}

/** The main method.
 *
 * @param argc The number of command-line arguments.
 *
 * @param argv The list of command-line arguments.
 */
int main(int argc, char *argv[]) {
    // Assume each command-line argument is a file
    std::vector<std::string> stats(argc); 
    std::vector<std::thread> thrList; 

    // Create 1 thread per file
    for (int i = 1; (i < argc); i++) {
        // A lambda is used instead of a threadMain method. 
        // (Just like in class!)
        thrList.push_back(std::thread([&, i]{ stats[i] = getCount(argv[i]); }));
    }
    // Wait for each thread to finish
    for (auto& thr : thrList) {
        thr.join();
    }
    // Print the results from each thread.
    for (int i = 1; (i < argc); i++) {
        std::cout << stats[i] << '\n';
    }
    return 0;
}
