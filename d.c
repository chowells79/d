#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "mad.h"

int main() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  struct in_addr addr;
  addr.s_addr = inet_addr("127.0.0.1");

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = 0x3333;
  saddr.sin_addr = addr;

  bind(sock, (struct sockaddr *) &saddr, sizeof(saddr));
  listen(sock, 5);

  struct sockaddr_in remote_addr;
  int size;
  int fd = accept(sock, (struct sockaddr *) &remote_addr, &size);
  shutdown(sock, SHUT_RDWR);
  return 0;
}
