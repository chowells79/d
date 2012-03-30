#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "ao/ao.h"
#include "mad.h"

int create_listening_socket(const char *, in_port_t);
void play_stream(int);

int main(void) {
  int sock = create_listening_socket("127.0.0.1", 13107);

  struct sockaddr_in remote_addr;
  socklen_t size;
  int fd = accept(sock, (struct sockaddr *) &remote_addr, &size);
  play_stream(fd);
  close(fd);
  shutdown(sock, SHUT_RDWR);
  return 0;
}

struct state_container {
  int fd;
  ao_device *out;
  ao_sample_format format;
};

enum mad_flow ignore_err(void *data,
                         struct mad_stream *stream,
                         struct mad_frame *frame) {
  // like I care...
  return MAD_FLOW_CONTINUE;
}

enum mad_flow play_output(void *data,
                          struct mad_header const *header,
                          struct mad_pcm *pcm) {
  // magic goes here
  return MAD_FLOW_CONTINUE;
}

enum mad_flow get_input(void *data,
                        struct mad_stream *stream) {
  struct state_container *state = (struct state_container *)data;

  size_t size = 4 * 1024;
  static void *buf = NULL;
  if (buf == NULL) {
    if ((buf = malloc(size)) == NULL) {
      perror("malloc");
      exit(1);
    }
  }

  ssize_t count = read(state->fd, buf, size);

  if (count < 0) {
    perror("read");
    exit(1);
  } else if (count == 0) {
    return MAD_FLOW_STOP;
  } else {
    mad_stream_buffer(stream, buf, count);
    return MAD_FLOW_CONTINUE;
  }
}


void play_stream(int fd) {
  ao_initialize();
  int driver_id = ao_default_driver_id();
  if (driver_id == -1) {
    perror("ao_default_driver_id");
    exit(1);
  }

  struct state_container state;
  memset((void *)&state, 0, sizeof state);
  state.fd = fd;
  state.out = NULL;

  struct mad_decoder decoder;
  mad_decoder_init(&decoder, &state, get_input, 0, 0, play_output, ignore_err, 0);

  ao_shutdown();
}

int create_listening_socket(const char *host_quad, in_port_t port) {
  int e = 1;
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1) {
    perror("socket");
    exit(1);
  }

  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&e, sizeof e) != 0) {
    perror("setsockopt");
    exit(1);
  }

  struct in_addr addr;
  memset((void *)&addr, 0, sizeof addr);
  if (inet_aton(host_quad, &addr) != 1) {
    perror("inet_aton");
    exit(1);
  }

  struct sockaddr_in saddr;
  memset((void *)&saddr, 0, sizeof saddr);
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr = addr;

  if (bind(sock, (struct sockaddr *) &saddr, sizeof saddr) != 0) {
    perror("bind");
    exit(1);
  }

  if (listen(sock, 5) != 0) {
    perror("listen");
    exit(1);
  }

  return sock;
}
