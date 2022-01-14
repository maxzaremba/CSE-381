/* 
 * A simple online stock exchange web-server.  
 * 
 * This multi-threaded web-server performs simple stock trading
 * transactions on stocks.  Stocks must be maintained in an
 * unordered_map.
 *
 * Copyright 2021 zarembmj@miamioh.edu
 */

// The commonly used headers are included.  Of course, you may add any
// additional headers as needed.
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <iomanip>
#include <vector>
#include "Stock.h"

// Setup a server socket to accept connections on the socket
using namespace boost::asio;
using namespace boost::asio::ip;

// Shortcut to smart pointer with TcpStream
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

// Prototype for helper method defined in main.cpp
std::string url_decode(std::string);

// The name space to hold all of the information that is shared
// between multiple threads.
namespace sm {
    // Unordered map including stock's name as the key (std::string)
    // and the actual Stock entry as the value.
    std::unordered_map<std::string, Stock> stockMap;
    
    // Homework 9 Code --------------------------------------------------------
    // Atomic variable that counts the number of active threads
    std::atomic<int> threadCount(0);
    // Condition variable used to check if the number of active threads
    // exceeds the limit.
    std::condition_variable threadStatus;
    // ------------------------------------------------------------------------
    
}  // namespace sm

/**
 * This method is used to create a new stock entry in stockMap.
 * If the stock already exists, then then no new entry is added.
 *
 * @note This method assumes that it will be called in single-threaded mode
 * by stock_client.
 *
 * @param stock The name of the stock.
 * 
 * @param balance The balance amount.
 */
std::string createStock(const std::string& stock, unsigned int balance) {
    // Check if stock exists
    if (sm::stockMap.find(stock) == sm::stockMap.end()) {
         // Create a new stock entry
        sm::stockMap[stock].name = stock;
        sm::stockMap[stock].balance = balance;
        return "Stock " + stock + " created with balance = "
            + std::to_string(balance);
    }
    // Returned if there is a duplicate account number
    return "Stock " + stock + " already exists";
}

/**
 * This method returns the status of the current balance on the specified stock.
 * It is assumed the the stock already exists.
 *
 * @param stock The name of the stock whose balance is to be returned.
 *
 * @return A string containing the balance information for the given
 * stock.
 */
std::string balanceStatus(const std::string& stock) {
    std::ostringstream output;

    {  // !Begin critical section!
        std::lock_guard<std::mutex> lock(sm::stockMap.at(stock).mutex);
        output << "Balance for stock " << stock << " = "
           << sm::stockMap.at(stock).balance;
    }  // !End critical section!

    return output.str();
}

/**
 * This method updates the balance of available stocks using the sleep-wait
 * approach that's required for Homework 9.
 * The balance can never go below zero, so if the transaction is "buy" and
 * the required number of stocks isn't available, this method waits until it is.
 *
 * @param trans The stock transaction to be initiated.  
 * This string should either be "sell" (add) or "buy" (subtract).
 *
 * @param name The name of the stock where the transaction is initiated.
 *
 * @param trades The quantity of stocks being bought or sold.
 */
std::string updateBalance(const std::string& trans, const std::string& name,
                          const unsigned trades) {
    Stock& stock = sm::stockMap.at(name);
    // Lock the specified stock entry with a unique_lock
    std::unique_lock<std::mutex> lock(stock.mutex);
    if (trans == "sell") {
        // Add stocks to the balance
        stock.balance += trades;
        // Wake up any threads that are waiting to sell
        stock.condVar.notify_one();
    } else {
        // We can only sell a number of stocks if we have that number of stocks
        // Hence, we wait for that condition to be true using a lambda
        stock.condVar.wait(lock,
                [&stock, trades] {
                    return stock.balance >= trades; });
        // Now sufficient balance is guaranteed
        assert(stock.balance >= trades);
        stock.balance -= trades;
    }

    // Return the output message
    return "Stock " + name + "'s balance updated";
}

/**
 * This method processes different types of transactions 
 * that are user-requested.
 * 
 * @note MT-safe.
 *
 * @param trans The transaction to be performed. The values for this parameter
 *  will be "reset", create", "buy", "sell", or "status".
 *
 * @param stock The name of the stock where the transaction will be initiated.
 *
 * @param trades The quantity of stocks being bought or sold.
 */
std::string processTrans(const std::string& trans, const std::string& stock, 
                         const unsigned trades = 0) {
    // The default output if the request isn't one of the designated 5
    std::string output = "Invalid request";
    if (trans == "reset") {
        // Resets stockMap by clearing out all stocks
        sm::stockMap.clear();
        output = "Stocks reset";
    } else if (trans == "create") { 
        // Create account if it does not currently exist
        output = createStock(stock, trades);
    } else {
        // Check if stock exists
        if (sm::stockMap.find(stock) == sm::stockMap.end()) {
            // If stock doesn't exist, display error
            output = "Stock not found";
        } else {
            // If stock exists, process buy or sell operations
            if ((trans == "buy") || (trans == "sell")) {
                output = updateBalance(trans, stock, trades);
            } else if (trans == "status") {
                // Get balance status
                output = balanceStatus(stock);
            }
        }
    }
    // Return the output message
    return output;
}

/** This method uses code copied from Homework 1 to send a 
 * fixed HTTP 200 OK response back to the client.
 *
 * @param os The output stream.
 * 
 * @param output The message sent to the client.
*/
void sendResponse(std::ostream& os, const std::string& output) {
    // Sends a fixed output message back to the client
    // In this case, the message is the output of processTrans
    os << "HTTP/1.1 200 OK\r\n"
       << "Server: StockServer\r\n"
       << "Content-Length: " << output.size() << "\r\n"
       << "Connection: Close\r\n"
       << "Content-Type: text/plain\r\n\r\n"
       << output;
}

/**
 * This method is called from a separate detached/background thread
 * from the runServer() method.  This method processes 1 transaction
 * from a client.  This method extracts the transaction information
 * and processes the transaction by calling the processTrans helper
 * method.
 * 
 * \param[in] is The input stream from where the HTTP request is to be
 * read and processed.
 *
 * \param[out] os The output stream to where the HTTP response is to
 * be written.
 */
void clientThread(std::istream& is, std::ostream& os) {
    // Read the HTTP request from the client, process it, and send an
    // HTTP response back to the client.
    std::string dummy, request;
    is >> dummy >> request;
    
    while (std::getline(is, dummy), dummy != "\r") {}
    // Decode the request using the url_decode method
    request = url_decode(request); 
    // Replace '&' and '=' characters in the request to make it
    // easier to parse out the data
    std::replace(request.begin(), request.end(), '&', ' ');
    std::replace(request.begin(), request.end(), '=', ' ');
    // Create a string input stream to read words out
    std::string trans, stock;
    unsigned int amount = 0;
    std::istringstream(request) >> dummy >> trans >> dummy >> stock 
                                >> dummy >> amount;
    // Process the transaction in an MT-safe method
    const std::string result = processTrans(trans, stock, amount);
    
    // Homework 9 Code --------------------------------------------------------
    // Decrement thread count
    sm::threadCount--;
    // Wake server thread if it's asleep
    sm::threadStatus.notify_one();
    // ------------------------------------------------------------------------

    // Send result back to the client
    sendResponse(os, result); 
}

/**
 * Top-level method to run a custom HTTP server to process stock trade
 * requests. 
 *
 * Phase 1: Multithreading is not needed.
 *
 * Phase 2: This method must use multiple threads -- each request
 * should be processed using a separate detached thread. Optional
 * feature: Limit number of detached-threads to be <= maxThreads.
 *
 * \param[in] server The boost::tcp::acceptor object to be used to accept
 * connections from various clients.
 *
 * \param[in] maxThreads The maximum number of threads that the server
 * should use at any given time.
 */
void runServer(tcp::acceptor& server, const int maxThreads) {
    // Process client connections one-by-one...forever
    while (true) {
        // Creates garbage-collected connection on heap 
        TcpStreamPtr client = std::make_shared<tcp::iostream>();
        server.accept(*client->rdbuf());  // wait for client to connect
        // Now we have a I/O stream to talk to the client. Have a
        // conversation using the protocol.
        
        // Homework 9 Code ----------------------------------------------------
        // This critical section ensures that maxThread will not be exceeded 
        // before we create any threads
        {  // !Begin critical section!
            std::mutex mutex;
            std::unique_lock<std::mutex> lock(mutex);
            // Lambda that waits until threadCount is less than maxThreads
            sm::threadStatus.wait(lock, [maxThreads] {
                return sm::threadCount < maxThreads; }); 
            // Increment threadCount
            sm::threadCount++;        
        }  // !End critical section!
        // --------------------------------------------------------------------
        
        std::thread thr([client]{ clientThread(*client, *client); });
        thr.detach();  // Process transaction independently
    }
}

// End of source code

// Note -----------------------------------------------------------------------
// The main.cpp file didn't seem to be included with the CODE source files and
// this program was breaking because of that, so I just copied 
// the necessary code here.
// Was that file intentionally excluded? Hopefully this addition isn't
// an issue.
// ----------------------------------------------------------------------------

/** Convenience method to decode HTML/URL encoded strings.
 *
 * This method must be used to decode query string parameters supplied
 * along with GET request.  This method converts URL encoded entities
 * in the from %nn (where 'n' is a hexadecimal digit) to corresponding
 * ASCII characters.
 *
 * \param[in] str The string to be decoded.  If the string does not
 * have any URL encoded characters then this original string is
 * returned.  So it is always safe to call this method!
 *
 * \return The decoded string.
*/
std::string url_decode(std::string str) {
    // Decode entities in the from "%xx"
    size_t pos = 0;
    while ((pos = str.find_first_of("%+", pos)) != std::string::npos) {
        switch (str.at(pos)) {
            case '+': str.replace(pos, 1, " ");
            break;
            case '%': {
                std::string hex = str.substr(pos + 1, 2);
                char ascii = std::stoi(hex, nullptr, 16);
                str.replace(pos, 3, 1, ascii);
            }
        }
        pos++;
    }
    return str;
}

// Helper method for testing.
void checkRunClient(const std::string& port, const bool printResp = false);

/*
 * The main method that performs the basic task of accepting
 * connections from the user and processing each request using
 * multiple threads.
 *
 * \param[in] argc This program accepts up to 2 optional command-line
 * arguments (both are optional)
 *
 * \param[in] argv The actual command-line arguments that are
 * interpreted as:
 *    1. First one is a port number (default is zero)
 *    2. The maximum number of threads to use (default is 20).
 */
int main(int argc, char** argv) {
    // Setup the port number for use by the server
    const int port   = (argc > 1 ? std::stoi(argv[1]) : 0);
    // Setup the maximum number of threads to be used.
    const int maxThr = (argc > 2 ? std::stoi(argv[2]) : 20);

    // Create end point.  If port is zero a random port will be set
    io_service service;    
    tcp::endpoint myEndpoint(tcp::v4(), port);
    tcp::acceptor server(service, myEndpoint);  // create a server socket
    // Print information where the server is operating.    
    std::cout << "Listening for commands on port "
              << server.local_endpoint().port() << std::endl;

    // Check and start tester client for automatic testing.
#ifdef TEST_CLIENT
    checkRunClient(argv[1]);
#endif

    // Run the server on the specified acceptor
    runServer(server, maxThr);
    
    // All done.
    return 0;
}

// End of Homework 8 main.cpp source code
