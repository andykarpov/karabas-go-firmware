#pragma once

// Picks "AT+KARABAS=<arg>" lines out of the byte stream that the USB serial bridge forwards
// from the host to the core UART. Bytes that may start such a line are held back; once they
// can no longer be one, they are handed back to be forwarded, in order. The prefix is matched
// ignoring case, the line ends with CR or LF (a LF right after a CR is dropped).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_AT_PREFIX "AT+KARABAS="
#define KG_AT_MAX_ARG 80

typedef enum {
  KG_AT_HOLD,    // the byte is held back
  KG_AT_PASS,    // forward out[0..out_len) to the core
  KG_AT_COMMAND, // a command line is complete: kg_at_arg()
} kg_at_result_t;

typedef struct {
  char buf[sizeof(KG_AT_PREFIX) - 1 + KG_AT_MAX_ARG + 1]; // held bytes, then the arg
  uint8_t len;
  bool done;   // buf holds the arg of the last command
  bool eat_lf; // drop a LF that follows the CR of a command
  char out[sizeof(KG_AT_PREFIX) - 1 + KG_AT_MAX_ARG + 1];
  uint8_t out_len;
} kg_at_parser_t;

void kg_at_init(kg_at_parser_t *p);
kg_at_result_t kg_at_feed(kg_at_parser_t *p, char c);
// moves the held bytes to out (no more bytes came for a while), returns out_len
uint8_t kg_at_flush(kg_at_parser_t *p);
// the arg of the command after KG_AT_COMMAND, as typed
const char *kg_at_arg(const kg_at_parser_t *p);

// Line ends for the ESP8266, which runs a command only after CR LF, from terminals that send a
// CR (PuTTY) or a LF alone: every CR, LF or CR LF becomes CR LF, also when the CR and the LF
// come in different calls. out has room for 2 * len bytes; returns the bytes written.
typedef struct {
  bool cr; // the last byte was a CR: a LF next is dropped
} kg_crlf_t;

size_t kg_crlf(kg_crlf_t *s, const char *in, size_t len, char *out);

#ifdef __cplusplus
}
#endif
