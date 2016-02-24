#include <asio.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#ifndef WIN32

#include <getopt.h>
#include <unistd.h>

#else

///////////////////////////////////////////////////////////////////////////////
// getopt.h
///////////////////////////////////////////////////////////////////////////////

extern const int no_argument;
extern const int required_argument;
extern const int optional_argument;

extern char *optarg;
extern int optind, opterr, optopt;

struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

int getopt(int argc, char *const argv[], const char *optstring);

#endif

///////////////////////////////////////////////////////////////////////////////
// final -h <ip> -p <port> -d <directory>
///////////////////////////////////////////////////////////////////////////////

using namespace std;
using asio::ip::tcp;

namespace HttpServer {

static const unsigned int timeoutMs = 30000;

static const string notFound =
    "HTTP/1.0 404 NOT FOUND\r\nContent-Type: text/html\r\n\r\n";

class Session : public enable_shared_from_this<Session> {
public:
  static const int max_length = 1024;

  explicit Session(tcp::socket socket, string dir)
      : socket_{move(socket)}, dir_{dir} {}

  void operator()() {
    // cout << "Starting thread: " << this_thread::get_id() << endl;
    run();
    // cout << "Exiting thread: " << this_thread::get_id() << endl;
  }

private:
  void run() {
    try {
      while (true) {
        // cout << "Start reading..." << endl;

        char data[max_length];
        asio::error_code error;
        size_t length =
            socket_.read_some(asio::buffer(data, max_length), error);
        if (error == asio::error::eof)
          break; // Connection closed cleanly by peer.
        if (error)
          throw asio::system_error(error); // Some other error.

        string dataStr(data, length);
        if (dataStr.empty()) {
          break;
        }
        stringstream ss(dataStr);
        string method;
        string path;
        ss >> method >> path;
        int p = path.find('?');
        if (p != string::npos) {
          path = path.substr(0, p);
        }

        // cout << "Parsed: " << method << " -> " << path << endl;
        // cout << "Read in " << this_thread::get_id() << endl;
        // cout << dataStr << endl;

        if (method == "GET" && path.size() > 1) {
          path = dir_ + path;

          // cout << "Opening: " << path << endl;
          ifstream ifs(path, ios_base::in);
          if (!ifs.good()) {
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
  // cout << "Starting server with address " << ip << " port " << port
  //     << " and dir " << dir << endl;
  tcp::acceptor a(service, tcp::endpoint(ip, port));
  for (;;) {
    tcp::socket sock(service);
    a.accept(sock);
    thread(Session(move(sock), dir)).detach();
  }
}
}

void run(string ip, string port, string dir) {
  try {
    asio::io_service io_service;
    HttpServer::server(io_service, asio::ip::address::from_string(ip),
                       stoi(port), dir);
    io_service.run();
  } catch (const exception &e) {
    cerr << "Exception: " << e.what() << "\n";
  }
}

int main(int argc, char **argv) {
  string ip;
  string port;
  string dir;

  int c;
  int errflg = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "h:p:d:")) != -1) {
    switch (c) {
    case 'h':
      ip = optarg;
      break;
    case 'p':
      port = optarg;
      break;
    case 'd':
      dir = optarg;
      break;
    case ':':
      fprintf(stderr, "Option -%c requires an operand\n", optopt);
      errflg++;
      break;
    case '?':
      fprintf(stderr, "Unrecognized option: -%c\n", optopt);
      errflg++;
    }
  }
  if (errflg || ip.empty() || port.empty() || dir.empty()) {
    fprintf(stderr, "usage: -h <ip> -p <port> -d <directory>\n");
    exit(2);
  }

#ifndef WIN32
  pid_t pid;
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then
     we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }
#endif

  run(ip, port, dir);

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// getopt.c
///////////////////////////////////////////////////////////////////////////////
#ifdef WIN32
const int no_argument = 0;
const int required_argument = 1;
const int optional_argument = 2;

char *optarg;
int optopt;
/* The variable optind [...] shall be initialized to 1 by the system. */
int optind = 1;
int opterr;

static char *optcursor = NULL;

/* Implemented based on [1] and [2] for optional arguments.
   optopt is handled FreeBSD-style, per [3].
   Other GNU and FreeBSD extensions are purely accidental.

[1] http://pubs.opengroup.org/onlinepubs/000095399/functions/getopt.html
[2] http://www.kernel.org/doc/man-pages/online/pages/man3/getopt.3.html
[3]
http://www.freebsd.org/cgi/man.cgi?query=getopt&sektion=3&manpath=FreeBSD+9.0-RELEASE
*/
int getopt(int argc, char *const argv[], const char *optstring) {
  int optchar = -1;
  const char *optdecl = NULL;

  optarg = NULL;
  opterr = 0;
  optopt = 0;

  /* Unspecified, but we need it to avoid overrunning the argv bounds. */
  if (optind >= argc)
    goto no_more_optchars;

  /* If, when getopt() is called argv[optind] is a null pointer, getopt()
     shall return -1 without changing optind. */
  if (argv[optind] == NULL)
    goto no_more_optchars;

  /* If, when getopt() is called *argv[optind]  is not the character '-',
     getopt() shall return -1 without changing optind. */
  if (*argv[optind] != '-')
    goto no_more_optchars;

  /* If, when getopt() is called argv[optind] points to the string "-",
     getopt() shall return -1 without changing optind. */
  if (strcmp(argv[optind], "-") == 0)
    goto no_more_optchars;

  /* If, when getopt() is called argv[optind] points to the string "--",
     getopt() shall return -1 after incrementing optind. */
  if (strcmp(argv[optind], "--") == 0) {
    ++optind;
    goto no_more_optchars;
  }

  if (optcursor == NULL || *optcursor == '\0')
    optcursor = argv[optind] + 1;

  optchar = *optcursor;

  /* FreeBSD: The variable optopt saves the last known option character
     returned by getopt(). */
  optopt = optchar;

  /* The getopt() function shall return the next option character (if one is
     found) from argv that matches a character in optstring, if there is
     one that matches. */
  optdecl = strchr(optstring, optchar);
  if (optdecl) {
    /* [I]f a character is followed by a colon, the option takes an
       argument. */
    if (optdecl[1] == ':') {
      optarg = ++optcursor;
      if (*optarg == '\0') {
        /* GNU extension: Two colons mean an option takes an
           optional arg; if there is text in the current argv-element
           (i.e., in the same word as the option name itself, for example,
           "-oarg"), then it is returned in optarg, otherwise optarg is set
           to zero. */
        if (optdecl[2] != ':') {
          /* If the option was the last character in the string pointed to by
             an element of argv, then optarg shall contain the next element
             of argv, and optind shall be incremented by 2. If the resulting
             value of optind is greater than argc, this indicates a missing
             option-argument, and getopt() shall return an error indication.

             Otherwise, optarg shall point to the string following the
             option character in that element of argv, and optind shall be
             incremented by 1.
          */
          if (++optind < argc) {
            optarg = argv[optind];
          } else {
            /* If it detects a missing option-argument, it shall return the
               colon character ( ':' ) if the first character of optstring
               was a colon, or a question-mark character ( '?' ) otherwise.
            */
            optarg = NULL;
            optchar = (optstring[0] == ':') ? ':' : '?';
          }
        } else {
          optarg = NULL;
        }
      }

      optcursor = NULL;
    }
  } else {
    /* If getopt() encounters an option character that is not contained in
       optstring, it shall return the question-mark ( '?' ) character. */
    optchar = '?';
  }

  if (optcursor == NULL || *++optcursor == '\0')
    ++optind;

  return optchar;

no_more_optchars:
  optcursor = NULL;
  return -1;
}

#endif