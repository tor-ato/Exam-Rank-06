#include <iostream>
#include <stdexcept>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <map>
#include <fstream>
#include <signal.h>
#include <cstdlib>

using namespace std;

#define MAX_CLIENTS 1024
map<string, string> database;
string filepath;

void load_db()
{
  ifstream file(filepath.c_str());
  string line;
  while (getline(file, line))
  {
    istringstream ss(line);
    string key, value;
    ss >> key >> value;
    database[key] = value;
  }
}

void save_db()
{
  ofstream file(filepath.c_str());
  for (map<string, string>::iterator it = database.begin(); it != database.end(); ++it)
    file << it->first << " " << it->second << endl;
}

void signalHandler(int)
{
  save_db();
  exit(0);
}

class Socket
{
private:
  struct sockaddr_in _servaddr;

public:
  int _sockfd;
  int _port;

  Socket(int port) : _sockfd(socket(AF_INET, SOCK_STREAM, 0)), _port(port)
  {
    if (_sockfd == -1)
      throw runtime_error("Socket creation failed");
    memset(&_servaddr, 0, sizeof(_servaddr));
    _servaddr.sin_family = AF_INET;
    _servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    _servaddr.sin_port = htons(port);
  }

  ~Socket() {
    if (_sockfd != -1)
      close(_sockfd);
  }

  void bindAndListen() {
    if (bind(_sockfd, (struct sockaddr *)&_servaddr, sizeof(_servaddr)) < 0)
      throw runtime_error("Socket bind failed");
    if (listen(_sockfd, 5) < 0)
      throw runtime_error("Socket listen failed");
  }

  int acceptClient(struct sockaddr_in &clientAddr) {
    socklen_t clientLen = sizeof(clientAddr);
    int clientSockFd = accept(_sockfd, (struct sockaddr *)&clientAddr, &clientLen);
    if (clientSockFd < 0)
      throw runtime_error("Failed to accept connection");
    return clientSockFd;
  }

  string pullMessage() {
    return "Totally not pulled message";
  }
};

class Client {
public:
  enum {
    CONNECTED = 1,
    DISCONNECTED = 0
  };

  int fd;
  char buffer[1024];
  int isConnected;

  Client() : fd(-1), isConnected(DISCONNECTED) {}
  ~Client() {
    if (fd != -1)
      close(fd);
  }
};

class Server {
private:
  Socket _listeningSocket;
  Client clients[MAX_CLIENTS];

  static string processCommand(const string &input) {
    istringstream ss(input);
    string cmd, key, value;
    ss >> cmd;

    if (cmd == "GET") {
      ss >> key;
      value = database[key];
      if (value.empty()) return "1\n";
      return "0 " + value + "\n";
    }

    if (cmd == "POST") {
      ss >> key >> value;
      database[key] = value;
      return "0\n";
    }

    if (cmd == "DELETE") {
      ss >> key;
      if (database.erase(key)) return "0\n";
      return "1\n";
    }

    return "2\n";
  }

  static void disconnectClient(Client &c, fd_set &activefds) {
    c.isConnected = Client::DISCONNECTED;
    if (c.fd != -1) {
      FD_CLR(c.fd, &activefds);
      close(c.fd);
      c.fd = -1;
    }
  }

  void handleNewConnection(fd_set &activefds, fd_set &readfds, int &maxfd, int &next_idx) {
    if (!FD_ISSET(_listeningSocket._sockfd, &readfds))
      return;

    int connfd = accept(_listeningSocket._sockfd, NULL, NULL);
    if (connfd < 0) {
      cerr << "accept failed" << endl;
      return;
    }

    FD_SET(connfd, &activefds);
    if (connfd > maxfd)
      maxfd = connfd;
    clients[next_idx].fd = connfd;
    clients[next_idx].isConnected = Client::CONNECTED;
    next_idx++;
  }

  void handleClients(fd_set &activefds, fd_set &readfds, fd_set &writefds) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
      const bool readable = (clients[i].fd != -1) && FD_ISSET(clients[i].fd, &readfds);
      const bool writable = (clients[i].fd != -1) && FD_ISSET(clients[i].fd, &writefds);

      if (clients[i].isConnected == Client::CONNECTED && readable && writable) {
        ssize_t read_bytes = recv(clients[i].fd, clients[i].buffer, sizeof(clients[i].buffer) - 1, 0);
        if (read_bytes <= 0) {
          disconnectClient(clients[i], activefds);
        } else {
          clients[i].buffer[read_bytes] = '\0';
          const string response = processCommand(string(clients[i].buffer));
          send(clients[i].fd, response.c_str(), response.size(), 0);
        }
      }
    }
  }

public:
  Server(int port) : _listeningSocket(port) {}

  int run() {
    try {
      _listeningSocket.bindAndListen();
      cout << "ready" << endl;

      fd_set activefds, readfds, writefds;
      FD_ZERO(&activefds);
      FD_SET(_listeningSocket._sockfd, &activefds);
      int maxfd = _listeningSocket._sockfd;
      int next_idx = 0;

      while (1) {
        readfds = activefds;
        writefds = activefds;

        int ready = select(maxfd + 1, &readfds, &writefds, NULL, NULL);
        if (ready < 0) {
          cerr << "select failed" << endl;
        } else {
          handleNewConnection(activefds, readfds, maxfd, next_idx);
          handleClients(activefds, readfds, writefds);
        }
      }
      return 0;
    }
    catch (const exception &e) {
      cerr << "Error during server run: " << e.what() << endl;
      return 1;
    }
  }
};

int main(int argc, char **argv)
{
  if (argc < 3) {
    cerr << "Wrong number of arguments" << endl;
    return 1;
  }
  int port = atoi(argv[1]);
  filepath = argv[2];
  load_db();
  signal(SIGINT, signalHandler);
  Server server(port);
  return server.run();
}
