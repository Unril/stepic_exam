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
#include <sys/stat.h>
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

static ofstream *log_;

namespace HttpServer {

static const string notFoundContent = "<html>"
                                      "<head><title>Not Found</title></head>"
                                      "<body><h1>404 Not Found</h1></body>"
                                      "</html>";

static const string notFound = "HTTP/1.0 404 Not Found\r\nContent-Length: " +
                               to_string(notFoundContent.size()) +
                               "\r\nContent-Type: text/html\r\n\r\n" +
                               notFoundContent;

static const string badRequestContent =
    "<html>"
    "<head><title>Bad Request</title></head>"
    "<body><h1>400 Bad Request</h1></body>"
    "</html>";

static const string badRequest =
    "HTTP/1.0 400 Bad Request\r\nContent-Length: " +
    to_string(badRequestContent.size()) +
    "\r\nContent-Type: text/html\r\n\r\n" + badRequestContent;

enum class Result {
  Ok,
  NotFound,
  BadRequest,
  Error,
};

class Session : public enable_shared_from_this<Session> {
public:
  static const int max_length = 1024;

  explicit Session(tcp::socket socket, string dir)
      : socket_{move(socket)}, dir_{dir} {}

  void operator()() {
    string content;
    Result r = run(content);

    switch (r) {
    case Result::Ok: {
      stringstream resp;
      resp << "HTTP/1.0 200 OK\r\nContent-Length: " << content.size()
           << "\r\nContent-type: text/html\r\n\r\n"
           << content;
      write(socket_, asio::buffer(resp.str()));
    } break;
    case Result::NotFound:
      write(socket_, asio::buffer(notFound));
      break;
    case Result::Error:
      break;
    case Result::BadRequest:
      write(socket_, asio::buffer(badRequest));
      break;
    default:
      break;
    }

    asio::error_code ignored_ec;
    socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
  }

  Result handleRequest(string uri, string &content) const {
    int p = uri.find('?');
    if (p != string::npos) {
      uri = uri.substr(0, p);
    }

    // Decode url to path.
    string request_path;
    if (!urlDecode(uri, request_path)) {
      return Result::BadRequest;
    }

    // Request path must be absolute and not contain "..".
    if (request_path.empty() || request_path[0] != '/' ||
        request_path.find("..") != string::npos) {
      return Result::BadRequest;
    }

    // If path ends in slash (i.e. is a directory) then add "index.html".
    if (request_path[request_path.size() - 1] == '/') {
      request_path += "index.html";
    }

    // Open the file to send back.
    string full_path = dir_ + request_path;
    ifstream is(full_path.c_str(), ios::in | ios::binary);
    if (!is) {
      return Result::NotFound;
    }

    // Fill out the reply to be sent to the client.
    content.clear();
    char buf[512];
    while (is.read(buf, sizeof(buf)).gcount() > 0)
      content.append(buf, is.gcount());
    return Result::Ok;
  }

  static bool urlDecode(const string &in, string &out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
      if (in[i] == '%') {
        if (i + 3 <= in.size()) {
          int value = 0;
          istringstream is(in.substr(i + 1, 2));
          if (is >> hex >> value) {
            out += static_cast<char>(value);
            i += 2;
          } else {
            return false;
          }
        } else {
          return false;
        }
      } else if (in[i] == '+') {
        out += ' ';
      } else {
        out += in[i];
      }
    }
    return true;
  }

  Result run(string &content) {
    try {
      char data[max_length];
      asio::error_code error;
      size_t length = socket_.read_some(asio::buffer(data, max_length), error);
      string dataStr(data, length);

      if (error == asio::error::eof)
        return Result::BadRequest;
      if (error)
        throw asio::system_error(error);

      if (log_)
        *log_ << "Data " << dataStr << endl;

      stringstream ss(dataStr);
      string method;
      string path;
      ss >> method >> path;

      if (method == "GET") {
        return handleRequest(path, content);
      }
      return Result::BadRequest;
    } catch (const exception &e) {
      cerr << "Session exception: " << e.what() << "\n";
      return Result::Error;
    }
  }

  tcp::socket socket_;
  string dir_;
};

void server(asio::io_service &service, asio::ip::address ip, int port,
            string dir) {
  if (dir.back() == '/')
    dir = dir.substr(0, dir.size() - 1);

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
  pid_t pid, sid;
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then
     we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* Change the file mode mask */
  umask(0);

  /* Open any logs here */
  log_ = new ofsteram("/home/box/log.txt");

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    /* Log any failures here */
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory */
  if ((chdir("/")) < 0) {
    /* Log any failures here */
    exit(EXIT_FAILURE);
  }

  /* Close out the standard file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
#endif

  if (log_)
    *log_ << "Open " << ip << " " << port << " " << dir << endl;

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
