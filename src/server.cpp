#include <asio.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

using namespace std;
using asio::ip::tcp;

namespace HttpServer {

static const unsigned int timeoutMs = 30000;

static const string notFound =
    "HTTP/1.0 404 NOT FOUND\r\nContent-Type: text/html\r\n\r\n";

class SetRcvTimeout {
public:
  explicit SetRcvTimeout(unsigned int timeout) : timeout_(timeout){};

  template <class Protocol> static int level(const Protocol &p) {
    return SOL_SOCKET;
  }

  template <class Protocol> static int name(const Protocol &p) {
    return SO_RCVTIMEO;
  }

  template <class Protocol> const void *data(const Protocol &p) const {
    return &timeout_;
  }

  template <class Protocol> size_t size(const Protocol &p) const {
    return sizeof(timeout_);
  }

private:
  unsigned int timeout_;
};

class SetSndTimeout {
public:
  explicit SetSndTimeout(unsigned int timeout) : timeout_(timeout){};

  template <class Protocol> static int level(const Protocol &p) {
    return SOL_SOCKET;
  }

  template <class Protocol> static int name(const Protocol &p) {
    return SO_SNDTIMEO;
  }

  template <class Protocol> const void *data(const Protocol &p) const {
    return &timeout_;
  }

  template <class Protocol> size_t size(const Protocol &p) const {
    return sizeof(timeout_);
  }

private:
  unsigned int timeout_;
};

class Session : public enable_shared_from_this<Session> {
public:
  static const int max_length = 1024;

  explicit Session(tcp::socket socket, string dir)
      : socket_{move(socket)}, dir_{dir} {}

  void operator()() {
   // cout << "Starting thread: " << this_thread::get_id() << endl;
    run();
    //cout << "Exiting thread: " << this_thread::get_id() << endl;
  }

private:
  void run() {
    try {
      while (true) {
       // cout << "Start reading..." << endl;

        char data[max_length];
        asio::error_code error;
        size_t length = socket_.read_some(asio::buffer(data), error);
        if (error == asio::error::eof)
          break; // Connection closed cleanly by peer.
        if (error)
          throw asio::system_error(error); // Some other error.

        string dataStr(data, length);
        stringstream ss(dataStr);
        string method;
        string path;
        ss >> method >> path;
        int p = path.find('?');
        if (p != string::npos) {
          path = path.substr(0, p);
        }

        //cout << "Parsed: " << method << " -> " << path << endl;
        //cout << "Read in " << this_thread::get_id() << endl;
        //cout << dataStr << endl;

        if (method == "GET" && !path.empty()) {
          path = dir_ + "/" + path;
		 // cout << "Opening: " << path << endl;
          ifstream ifs(path);
          if (!ifs) {
            write(socket_, asio::buffer(notFound));
            break;
          }
          string content((istreambuf_iterator<char>(ifs)),
                         istreambuf_iterator<char>());
          stringstream resp;
          resp << "HTTP/1.0 200 OK" << endl;
          resp << "Content-length: " << content.size() + 1 << endl;
          resp << "Connection: close" << endl;
          resp << "Content-Type: text/html" << endl;
          resp << "\r\n" << endl;
          resp << content << endl;
          write(socket_, asio::buffer(resp.str()));
        } else {
          write(socket_, asio::buffer(notFound));
          break;
        }
      }
    } catch (const exception &e) {
      cerr << "Session exception: " << e.what() << "\n";
    }
  }

  tcp::socket socket_;
  string dir_;
};

void server(asio::io_service &service, asio::ip::address ip, int port,
            string dir) {
  //cout << "Starting server with address " << ip << " port " << port
  //     << " and dir " << dir << endl;
  tcp::acceptor a(service, tcp::endpoint(ip, port));
  for (;;) {
    tcp::socket sock(service);
    a.accept(sock);
    thread(Session(move(sock), dir)).detach();
  }
}
}

int main(int argc, char **argv) {
  if (argc != 4) {
    cerr << "Not enough arguments" << endl;
    exit(2);
  }
  try {
    string ip = argv[1];
    string port = argv[2];
    string dir = argv[3];

    asio::io_service io_service;
    HttpServer::server(io_service, asio::ip::address::from_string(ip),
                       stoi(port), dir);
    io_service.run();
  } catch (const exception &e) {
    cerr << "Exception: " << e.what() << "\n";
  }
  return 0;
}
