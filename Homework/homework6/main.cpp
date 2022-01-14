#include <boost/asio.hpp>
#include <string>
#include "HTTPFile.h"
#include "ChildProcess.h"

// Convenience namespace to streamline the code below.
using namespace boost::asio;
using namespace boost::asio::ip;

// Forward declaration for url_decode method defined below.  We need
// this declaration this just to organize the starter code into
// sections that students should not modify (to minimize problems for
// students).
std::string url_decode(std::string url);

/**
 * Process HTTP request (from first line & headers) and provide
 * suitable HTTP response back to the client.  This method handles
 * program execution requests in which the URL starts with
 * "/cgi-bin/exec?cmd=".  All other URLs are assumed to be file
 * requests.
 *
 * \note For running commands this method uses ChildProcess class from
 * prior exercises/projects.
 *
 * \note For file requests this method uses the HTTPFile convenience
 * class supplied as starter code.
 * 
 * @param is The input stream to read HTTP reqeust data from client
 * (or web-browser).
 *
 * @param os The output stream to send chunked HTTP response data back
 * to the client (or web-browser).
 */
void serveClient(std::istream& is, std::ostream& os) {
    // Process the GET request from the input stream. See example
    // inputs for the format of the URLs that you will be provided.
    
    // To send contents of a file as HTTP chunks, the code is very
    // simple:
    //   const std::string path = file_name_from_GET_resut
    //   os << http::file(path);
}


//------------------------------------------------------------------
//  DO  NOT  MODIFY  CODE  BELOW  THIS  LINE
//------------------------------------------------------------------

/**
 * Runs the program as a server.  It accepts connections and listens
 * to incoming connections.
 * 
 * @param port The port number on which the server should listen.
 */
void runServer(int port) {
    // Setup a server socket to accept connections on the socket
    io_service service;
    // Create end point.  If the port number is zero, then myEndpoint
    // uses a port automatically assigned to it by the operating
    // system.
    tcp::endpoint myEndpoint(tcp::v4(), port);
    // Create a socket that accepts connections
    tcp::acceptor server(service, myEndpoint);
    std::cout << "Server is listening on "
              << server.local_endpoint().port()
              << " & ready to process clients...\n";
    // Process client connections one-by-one...forever
    while (true) {
        // Wait for a client to connect.
        tcp::iostream client;
        // The following method calls waits (could wait forever) until
        // a client connects.
        server.accept(*client.rdbuf());
        // Have helper method process the client connection.
        serveClient(client, client);
    }
}

/** Convenience method to decode HTML/URL encoded strings.

    This method must be used to decode query string parameters
    supplied along with GET request.  This method converts URL encoded
    entities in the from %nn (where 'n' is a hexadecimal digit) to
    corresponding ASCII characters.

    \param[in] str The string to be decoded.  If the string does not
    have any URL encoded characters then this original string is
    returned.  So it is always safe to call this method!

    \return The decoded string.
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

/**
 * The main function that serves as a test harness based on
 * command-line arguments.
 *
 * \param[in] argc The number of command-line arguments.  This test
 * harness can work with zero or one command-line argument.
 *
 * \param[in] argv The actual command-line arguments.  If this is an
 * number it is assumed to be a port number.  Otherwise it is assumed
 * to be an file name that contains inputs for testing.
 */
int main(int argc, char *argv[]) {
    // Check and use first command-line argument if any as port or file
    std::string arg = (argc > 1 ? argv[1] : "0");
    
    // Check and use a given input data file for testing.
    if (arg.find_first_not_of("1234567890") == std::string::npos) {
        // All characters are digits. So we assume this is a port
        // number and run as a standard web-server
        runServer(std::stoi(arg));
    } else {
        // In this situation, this program processes inputs from a
        // given data file for testing.  That is, instead of a
        // web-browser we just read inputs from a data file.
        std::ifstream getReq(arg);
        if (!getReq.good()) {
            std::cerr << "Unable to open " << arg << ". Aborting.\n";
            return 2;
        }
        // Have the serveClient method process the inputs from a given
        // file for testing.
        serveClient(getReq, std::cout);
    }
    // All done.
    return 0;
}
