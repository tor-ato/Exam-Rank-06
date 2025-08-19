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

#define MAX_CLIENTS 1024
std::map<std::string, std::string> database;
std::string filepath;

void load_db()
{
  std::ifstream file(filepath);
  std::string line;
  while (std::getline(file, line))
  {
    std::istringstream ss(line);
    std::string key, value;
    ss >> key >> value;
    database[key] = value;
  }
}

void save_db()
{
  std::ofstream file(filepath);
  for (std::map<std::string, std::string>::iterator it = database.begin(); it != database.end(); ++it)
    file << it->first << " " << it->second << std::endl;
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

  Socket(int port) : _sockfd(socket(AF_INET, SOCK_STREAM, 0))
  {
    if (_sockfd == -1)
      throw std::runtime_error("Socket creation failed");
    memset(&_servaddr, 0, sizeof(_servaddr));
    _servaddr.sin_family = AF_INET;
    _servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    _servaddr.sin_port = htons(port);
    _port = port;
  }

  ~Socket() {
    if (_sockfd != -1)
      close(_sockfd);
  }

  void bindAndListen() {
    if (bind(_sockfd, (struct sockaddr *)&_servaddr, sizeof(_servaddr)) < 0)
      throw std::runtime_error("Socket listen failed");
    if (listen(_sockfd, 5) < 0)
      throw std::runtime_error("Socket listen failed");
  }

  int acceptClient(struct sockaddr_in &clientAddr) {
    socklen_t clientLen = sizeof(clientAddr);
    int clientSockFd = accept(_sockfd, (struct sockaddr *)&clientAddr, &clientLen);
    if (clientSockFd < 0)
      throw std::runtime_error("Failed to accept connection");
    return clientSockFd;
  }

  std::string pullMessage() {
    return ("Totally not pulled message");
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

public:
  Server(int port) : _listeningSocket(port) {}

  int run() {
    try {
      _listeningSocket.bindAndListen();
      // Ready to accept connections. Logic for acception connection would go here.
      std::cout << "ready" << std::endl;

      fd_set activefds, readfds, writefds;
      FD_ZERO(&activefds);
      FD_SET(_listeningSocket._sockfd, &activefds);
      int maxfd = _listeningSocket._sockfd;
      int connfd;
      int next_idx = 0;

      while (1) {
        readfds = activefds;
        writefds = activefds;
        if (select(maxfd + 1, &readfds, &writefds, NULL, NULL) < 0) {
          std::cerr << "select failed" << std::endl;
          continue;
        }
        if (FD_ISSET(_listeningSocket._sockfd, &readfds)) {
          connfd = accept(_listeningSocket._sockfd, NULL, NULL);
          if (connfd < 0) {
            std::cerr << "accept failed" << std::endl;
            continue;
          }
          FD_SET(connfd, &activefds);
          if (connfd > maxfd)
            maxfd = connfd;
          clients[next_idx].fd = connfd;
          clients[next_idx].isConnected = Client::CONNECTED;
          next_idx++;
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
          if (
              clients[i].isConnected == Client::DISCONNECTED ||
              !FD_ISSET(clients[i].fd, &readfds) ||
              !FD_ISSET(clients[i].fd, &writefds))
            continue;
          ssize_t read_bytes = recv(clients[i].fd, clients[i].buffer, sizeof(clients[i].buffer) - 1, 0);
          if (read_bytes < 0) {
            clients[i].isConnected = Client::DISCONNECTED;
            FD_CLR(clients[i].fd, &activefds);
            close(clients[i].fd);
            clients[i].fd = -1;
          }
          else {
            clients[i].buffer[read_bytes] = '\0';
            std::istringstream ss(std::string(clients[i].buffer));
            std::string cmd, key, value;
            ss >> cmd;
            std::string response;
            if (cmd == "GET") {
              ss >> key;
              value = database[key];
              if (value.empty())
                response = "1\n";
              else
                response = "0 " + value + "\n";
            }
            else if (cmd == "POST") {
              ss >> key >> value;
              database[key] = value;
              response = "0\n";
            }
            else if (cmd == "DELETE") {
              ss >> key;
              if (database.erase(key))
                response = "0\n";
              else
                response = "1\n";
            }
            else
              response = "2\n";
            send(clients[i].fd, response.c_str(), response.size(), 0);
          }
        }
      }
      return 0;
    }
    catch (const std::exception &e) {
      std::cerr << "Error during server run: " << e.what() << std::endl;
      return 1; // Return an error code if server fail to start
    }
  }
};

int main(int argc, char **argv)
{
  if (argc < 3) {
    std::cerr << "Wrong number of arguments" << std::endl;
    return 1;
  }
  int port = atoi(argv[1]);
  filepath = argv[2];
  load_db();
  signal(SIGINT, signalHandler);
  Server server(port);
  return server.run();
}
