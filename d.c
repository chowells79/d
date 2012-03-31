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

#define LISTEN_PORT 13107

int create_listening_socket(const char *, in_port_t);
void play_stream(int);

int main(void) {
  int sock = create_listening_socket("127.0.0.1", LISTEN_PORT);

  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof remote_addr);
  socklen_t size = sizeof remote_addr;
  int fd = accept(sock, (struct sockaddr *) &remote_addr, &size);
  if (fd < 0) {
    perror("accept");
    exit(1);
  }
  play_stream(fd);
  close(fd);
  shutdown(sock, SHUT_RDWR);
  return 0;
}

struct state_container {
  int fd;
  int driver_id;
  ao_device *out;
  ao_sample_format format;
};

enum mad_flow output_err(void *data,
                         struct mad_stream *stream,
                         struct mad_frame *frame) {
  struct state_container *state = data;

  fprintf(stderr, "decoding error 0x%04x (%s)\n",
      stream->error, mad_stream_errorstr(stream));

  return MAD_FLOW_CONTINUE;
}

ao_sample_format make_sample_format(struct mad_pcm *pcm) {
  ao_sample_format res;
  memset(&res, 0, sizeof res);

  res.bits = 32;
  res.rate = pcm->samplerate;
  res.channels = 2;
  res.byte_format = AO_FMT_NATIVE;
  res.matrix = "L,R";

  return res;
}

int eq_sample_format(ao_sample_format *f1, ao_sample_format *f2) {
  return (f1->bits        == f2->bits        &&
          f1->rate        == f2->rate        &&
          f1->channels    == f2->channels    &&
          f1->byte_format == f2->byte_format &&
          strcmp(f1->matrix, f2->matrix) == 0);
}

int scale(int sample) {
    if (sample >  ((1 << 28) - 1))
        sample =  ((1 << 28) - 1);
    if (sample < -((1 << 28) - 1))
        sample = -((1 << 28) - 1);
    return sample * 8;
}

enum mad_flow play_output(void *data,
                          struct mad_header const *header,
                          struct mad_pcm *pcm) {
  struct state_container *state = (struct state_container *)data;

  ao_sample_format from_pcm = make_sample_format(pcm);
  if (!state->out || !eq_sample_format(&(state->format), &from_pcm)) {
    if (state->out) {
      ao_close(state->out);
    }
    state->out = ao_open_live(state->driver_id, &from_pcm, NULL);
  }

  static char *buf = NULL;
  static char *point = NULL;
  size_t bufsize = 4 * 1024 * 16;
  if (buf == NULL) {
    if ((buf = calloc(bufsize, 1)) == NULL) {
      perror("calloc");
      exit(1);
    }
    point = buf;
  }

  unsigned int nchannels = pcm->channels;
  unsigned int samples_left = pcm->length;
  fprintf(stderr, "Read %d samples ( x %d channels)\n", samples_left, nchannels);
  mad_fixed_t *left_ch   = pcm->samples[0];
  mad_fixed_t *right_ch  = pcm->samples[1];
  while (samples_left--) {
    int l = scale(*left_ch++);

    *(mad_fixed_t *)point = l;
    point += 4;

    int r = nchannels == 2 ? scale(*right_ch++) : l;
    *(mad_fixed_t *)point = r;
    point += 4;

    if (point == buf + bufsize) {
      ao_play(state->out, buf, bufsize);
      point = buf;
    }
  }

  return MAD_FLOW_CONTINUE;
}

enum mad_flow get_input(void *data,
                        struct mad_stream *stream) {
  struct state_container *state = (struct state_container *)data;

  size_t size = 4 * 1024;
  static unsigned char *buf = NULL;
  if (buf == NULL) {
    if ((buf = calloc(size, 1)) == NULL) {
      perror("calloc");
      exit(1);
    }
  }

  unsigned char *target = buf;
  size_t kept = 0;
  if (stream->next_frame) {
    kept = size - (stream->next_frame - buf);
    memmove(buf, stream->next_frame, kept);
    target += kept;
  }

  ssize_t count = read(state->fd, target, size - kept);
  fprintf(stderr, "kept %d bytes, read %d bytes\n", kept, count);

  if (count < 0) {
    perror("read");
    exit(1);
  } else if (count == 0) {
    return MAD_FLOW_STOP;
  } else {
    mad_stream_buffer(stream, buf, count + kept);
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
  memset(&state, 0, sizeof state);
  state.fd = fd;
  state.driver_id = driver_id;
  state.out = NULL;

  struct mad_decoder decoder;
  mad_decoder_init(&decoder, &state, get_input, 0, 0, play_output, output_err, 0);
  mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
  mad_decoder_finish(&decoder);

  if (state.out) {
    ao_close(state.out);
  }
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
  memset(&addr, 0, sizeof addr);
  if (inet_aton(host_quad, &addr) != 1) {
    perror("inet_aton");
    exit(1);
  }

  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof saddr);
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
