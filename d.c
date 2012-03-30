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
  int driver_id;
  ao_device *out;
  ao_sample_format format;
};

enum mad_flow ignore_err(void *data,
                         struct mad_stream *stream,
                         struct mad_frame *frame) {
  // like I care...
  return MAD_FLOW_CONTINUE;
}

ao_sample_format make_sample_format(struct mad_pcm *pcm) {
  ao_sample_format res;
  memset((void *)&res, 0, sizeof res);

  res.bits = 24;
  res.rate = pcm->samplerate;
  res.channels = 2;
  res.byte_format = AO_FMT_LITTLE;
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

inline signed int scale(mad_fixed_t sample)
{
  /* add noise! */
  sample += rand() % (1L << (MAD_F_FRACBITS - 24));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 24);
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
  int bufsize = 3 * 1024;
  if (buf == NULL) {
    if ((buf =(char *)malloc(bufsize)) == NULL) {
      perror("malloc");
      exit(1);
    }
  }

  unsigned int nchannels = pcm->channels;
  unsigned int samples_left = pcm->length;
  char *point = buf;
  mad_fixed_t *left_ch   = pcm->samples[0];
  mad_fixed_t *right_ch  = pcm->samples[1];
  while (samples_left--) {
    int l = scale(*left_ch++);
    *point++ = l & 0xFF;
    *point++ = (l & 0xFF00) >> 8;
    *point++ = (l & 0xFF0000) >> 16;

    int r = nchannels == 2 ? scale(*right_ch++) : l;
    *point++ = r & 0xFF;
    *point++ = (r & 0xFF00) >> 8;
    *point++ = (r & 0xFF0000) >> 16;

    if (point == buf + bufsize) {
      ao_play(state->out, buf, (uint_32)bufsize);
      point = buf;
    }
  }
  if (point > buf) {
    ao_play(state->out, buf, (uint_32)(point - buf));
  }

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
  state.driver_id = driver_id;
  state.out = NULL;

  struct mad_decoder decoder;
  mad_decoder_init(&decoder, &state, get_input, 0, 0, play_output, ignore_err, 0);
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
