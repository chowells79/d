#include <stdio.h>
#include <unistd.h>

#include <netinet/in.h>

#include <mad.h>

int create_listening_socket(const char *, in_port_t);

int main() {
  int sock = create_listening_socket("127.0.0.1", 0x3333);

  struct sockaddr_in remote_addr;
  int size;
  int fd = accept(sock, (struct sockaddr *) &remote_addr, &size);
  close(fd);
  shutdown(sock, SHUT_RDWR);
  return 0;
}

int create_listening_socket(const char *host_quad, in_port_t port) {
  int enable = 1;
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(enable));

  struct in_addr addr;
  inet_aton(host_quad, &addr);

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr = addr;

  bind(sock, (struct sockaddr *) &saddr, sizeof(saddr));
  listen(sock, 5);

  return sock;
}
