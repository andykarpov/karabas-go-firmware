#include "at_cmd.h"
#include <string.h>

static const uint8_t prefix_len = sizeof(KG_AT_PREFIX) - 1;

static char upper(char c) {
  return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

void kg_at_init(kg_at_parser_t *p) {
  memset(p, 0, sizeof(*p));
}

uint8_t kg_at_flush(kg_at_parser_t *p) {
  p->out_len = 0;
  if (!p->done) {
    memcpy(p->out, p->buf, p->len);
    p->out_len = p->len;
  }
  p->len = 0;
  p->done = false;
  return p->out_len;
}

kg_at_result_t kg_at_feed(kg_at_parser_t *p, char c) {
  bool eat_lf = p->eat_lf;
  p->eat_lf = false;
  if (p->done) {
    p->len = 0;
    p->done = false;
  }
  p->out_len = 0;

  if (eat_lf && c == '\n') return KG_AT_HOLD;

  if (p->len < prefix_len) {
    if (upper(c) == KG_AT_PREFIX[p->len]) {
      p->buf[p->len++] = c;
      return KG_AT_HOLD;
    }
  } else if (c == '\r' || c == '\n') {
    p->buf[p->len] = '\0';
    p->done = true;
    p->eat_lf = (c == '\r');
    return KG_AT_COMMAND;
  } else if (p->len < sizeof(p->buf) - 1) {
    p->buf[p->len++] = c;
    return KG_AT_HOLD;
  }

  // not a command line: forward what was held, then this byte (or hold it if it may start one)
  kg_at_flush(p);
  if (upper(c) == KG_AT_PREFIX[0]) {
    p->buf[p->len++] = c;
  } else {
    p->out[p->out_len++] = c;
  }
  return p->out_len > 0 ? KG_AT_PASS : KG_AT_HOLD;
}

const char *kg_at_arg(const kg_at_parser_t *p) {
  return p->done ? p->buf + prefix_len : "";
}
