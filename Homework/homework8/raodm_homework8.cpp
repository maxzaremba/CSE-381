/* 
 * A simple online stock exchange web-server.  
 * 
 * This multithreaded web-server performs simple stock trading
 * transactions on stocks.  Stocks must be maintained in an
 * unordered_map.
 *
 * Copyright 2020 raodm@miamioh.edu
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

// Commonly used namespace to streamline code.
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::literals::chrono_literals;

// Shortcut to smart pointer with TcpStream
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

// Forward declaration for methods defined further below
std::string url_decode(std::string);

// The name space to hold all of the information that is shared
// between multiple threads.
namespace sm {
    // Unordered map including stock's name as the key (std::string)
    // and the actual Stock entry as the value.
    std::unordered_map<std::string, Stock> stockMap;

    // -------------[ Limit number of threads ]-------------------    
    // The atomic counter that tracks the number of active threads.
    std::atomic<int> numThreads(0);

    // A condition variable to wait if number of threads being used
    // exceeds a given limit.
    std::condition_variable thrCond;
    // -----------------------------------------------------------    

}  // namespace sm

/** Helper method to send a fixed HTTP 200 OK response back to the
 *  client.
 *
 * \param[out] os The output stream to where the data is to be
 * written.
 * \param[in] msg The message to be sent to the client.
*/
void sendResponse(std::ostream& os, const std::string& msg) {
    // Send a fixed message back to the client.
    os << "HTTP/1.1 200 OK\r\n"
       << "Server: StockServer\r\n"
       << "Content-Length: " << msg.size() << "\r\n"
       << "Connection: Close\r\n"
       << "Content-Type: text/plain\r\n\r\n"
       << msg;
}

/**
 * This is a helper method used to check and create a new stock
 * entry. If the given stock name already exists, then this method
 * does not add a new entry.
 *
 * \note This method assumes that it will be called initially from a
 * single threaded mode only.
 *
 * \param[in] stock The name of the stock to be added to the map.
 * \param[in] balance The balance value to be initialized.
 */
std::string
checkCreate(const std::string& stock, unsigned int balance) {
    if (sm::stockMap.find(stock) == sm::stockMap.end()) {
        sm::stockMap[stock].name = stock;
        sm::stockMap[stock].balance = balance;
        return "Stock " + stock + " created with balance = "
            + std::to_string(balance);
    }
    // Duplicate account number.
    return "Stock " + stock + " already exists";
}

/**
 * Return the current balance on the specified stock.
 *
 * \param[in] stock The name of the stock whose balance is to be reported.
 *
 * \note This method assumes that the stock already exists.
 *
 * \return A string containing the balance information for the given
 * stock.
 */
std::string getStatus(const std::string& stock) {
    std::ostringstream os;

    {  // Start critical section.
        std::unique_lock<std::mutex> lock(sm::stockMap.at(stock).mutex);
        os << "Balance for stock " << stock << " = "
           << sm::stockMap.at(stock).balance;
    }  // End critical section.

    return os.str();
}

/**
 * Helper method to update the balance of available stocks.  Note that
 * the balance can never go below zero.  So, if the transaction is to
 * "buy" and sufficient stocks are not available, then this method
 * waits until sufficient stocks are available.
 *
 * \param[in] trans The transaction to be performed.  This must be the
 * string "sell" (to add) or "buy" (to subtract).
 *
 * \param[in] name The name of the stock on which the trasaction is
 * to be performed.  This method assumes that the stock already exists.
 *
 * \param[in] trade The number of stocks being bought/sold.
 */
std::string updateBalance(const std::string& trans, const std::string& name,
                          const unsigned trade) {
    // First lock the specific stock entry
    Stock& stock = sm::stockMap.at(name);
    std::unique_lock<std::mutex> lock(stock.mutex);
    
    // If the transaction is to add, then we can do it right away
    // without any issues.
    if (trans == "sell") {
        // Add stocks to the balance.
        stock.balance += trade;
        // Wake-up any threads that may be waiting to sell
        stock.condVar.notify_one();
    } else {
        // ------------------[ Extra feature ]-----------------------
        // We can sell stocks only if we have sufficient stock. So we
        // will wait for that condition to be true.
        stock.condVar.wait(lock,  // The lock on a given stock.
                           [&stock, trade]  // Capture for lambda
                           { return stock.balance >= trade; });
        // Now we are guaranteed to have sufficient balance.
        assert(stock.balance >= trade);
        stock.balance -= trade;
        // -----------------------------------------------------------
    }

    // Return message back to the caller.
    return "Stock " + name + "'s balance updated";
}

/**
 * Helper method to process different types of transactions as
 * requested by the user.
 *
 * \param[in] trans The transaction to be performed. This must be one
 * of the following values: "create", "buy", "sell", or "status"
 *
 * \param[in] stock The name of the stock (e.g. "msft") on which the
 * transation is to be applied.
 *
 * \param[in] trade An optional value indicating the number of stocks
 * being bought-or-sold in this transaction.
 */
std::string processTrans(const std::string& trans, const std::string& stock, 
                         const unsigned trade = 0) {
    std::string msg = "Invalid request";
    // All lines below are in a critical section to ensure operations
    // on the shared bank is thread safe!
    if (trans == "create") {
        // Create account only if it does not already exists
        msg = checkCreate(stock, trade);
    } else {
        // If the stock does not exist we cannot do operations on it.
        if (sm::stockMap.find(stock) == sm::stockMap.end()) {
            msg = "Stock not found";  // Invalid account ID
        } else {
            // Process request if operation is correct.
            if ((trans == "buy") || (trans == "sell")) {
                msg = updateBalance(trans, stock, trade);
            } else if (trans == "status") {
                msg = getStatus(stock);
            }
        }
    }
    // Return the result back to the caller.
    return msg;
}


/**
 * This method is called from a separate detached/background thread
 * from the runServer() method.  This method processes 1 transaction
 * from a client.  This method extracts the transaction information
 * and processes the transaction by calling the processTrans helper
 * method.
 * 
 * \param[in,out] client The socket stream to be used for performing
 * I/O operations.
 */
void clientThread(TcpStreamPtr client) {
    // Read the request to process from the web client.
    std::string dummy, request;
    *client >> dummy >> request;  // Read the word "GET"
    // Read and discard rest of the HTTP request & headers.
    while (std::getline(*client, dummy), dummy != "\r") {}
    // URL decode the request (just in case it is url encoded by client).
    request = url_decode(request);
    // Replace '&' and '=' characters in the request to make it
    // easier to parse out the data.
    std::replace(request.begin(), request.end(), '&', ' ');
    std::replace(request.begin(), request.end(), '=', ' ');
    // Create a string input stream to read words out.
    std::istringstream is(request);
    std::string trans, stock;
    unsigned int amount = 0;
    is >> dummy >> trans >> dummy >> stock >> dummy >> amount;
    // Process the requested transaction appropriately using helper
    // method that is MT-safe
    const std::string result = processTrans(trans, stock, amount);

    // -------------[ Limit number of threads ]-------------------    
    // Decrement current number of running threads so that the
    // runServer method can keep track of currently running threads.
    sm::numThreads--;
    sm::thrCond.notify_one();  // Wake up server thread if it is sleeping.
    // ------------------------------------------------------------

    // Send result back to the client
    sendResponse(*client, result);    
}

/**
 * Top-level method to run a custom HTTP server to process stock trade
 * requests using multiple threads. Each request should be processed
 * using a separate detached thread. This method just loops for-ever.
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

        // -------------[ Limit number of threads ]-------------------
        // But prior to spinning-up threads, ensure we don't exceed
        // the maxThread limit.
        {   // Begin critical section
            std::mutex tempMutex;
            std::unique_lock<std::mutex> lock(tempMutex);
            // Wait until total count falls below maxThreads.
            sm::thrCond.wait(lock, [maxThreads]  // capture clause for lambda
                                   { return sm::numThreads < maxThreads; }); 
            sm::numThreads++;  // Increase number of threads.
        }   // End critical section
        // -----------------------------------------------------------

        // Now we are guranteed we have fewer than maxThreads.
        std::thread thr(clientThread, client);
        thr.detach();  // Run independently
    }
}

//-------------------------------------------------------------------
//  DO  NOT   MODIFY  CODE  BELOW  THIS  LINE
//-------------------------------------------------------------------

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

// End of source code
