#include <libpynq.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fortune.h"

/******************************************************************************
 *  I2C MASTER - STRING ECHO
 *
 *  Register layout (must match slave):
 *      [0]          NEW_MESSAGE flag : set to 1 when text is in INBOX
 *      [1]..[127]   INBOX text       : string sent to slave (up to 508 bytes)
 *      [128]        REPLY_READY flag : set to 1 by slave when reply is ready
 *      [129]..[255] OUTBOX text      : reply string from slave
 ******************************************************************************/

#define SLAVE_ADDRESS 0x70

#define PIN_SCL IO_PMODA3
#define PIN_SDA IO_PMODA4

#define REG_NEW_MESSAGE  0
#define REG_INBOX_FIRST  1
#define REG_REPLY_READY  128
#define REG_OUTBOX_FIRST 129

#define TEXT_REGISTERS 127
#define MAX_TEXT_BYTES (TEXT_REGISTERS * 4)

#define REPLY_TIMEOUT_MSEC   1000
#define NUMBER_OF_PACKETS    10
#define PACKET_INTERVAL_MSEC 500
#define MAX_FORTUNE_BYTES    300

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
  (void) sig;
  running = 0;
}

// Write a 32-bit value to a slave register
static bool write_number(uint8_t mailbox, uint32_t number) {
  bool failed = iic_write_register(IIC0, SLAVE_ADDRESS, mailbox,
                                   (uint8_t *) &number, 4);
  return !failed;
}

// Read a 32-bit value from a slave register
static bool read_number(uint8_t mailbox, uint32_t *number) {
  bool failed = iic_read_register(IIC0, SLAVE_ADDRESS, mailbox,
                                  (uint8_t *) number, 4);
  return !failed;
}

// Write string to slave INBOX registers (4 bytes per register) and set NEW_MESSAGE flag
static bool send_string(const char *text) {
  char buffer[MAX_TEXT_BYTES] = {0};
  snprintf(buffer, sizeof(buffer), "%s", text);

  int pieces = (int) (strlen(buffer) / 4) + 1;
  if (pieces > TEXT_REGISTERS) {
    pieces = TEXT_REGISTERS;
  }

  if (!write_number(REG_REPLY_READY, 0)) {
    return false;
  }

  for (int i = 0; i < pieces; i++) {
    uint8_t mailbox = REG_INBOX_FIRST + i;
    char *piece = &buffer[i * 4];
    bool failed = iic_write_register(IIC0, SLAVE_ADDRESS, mailbox,
                                     (uint8_t *) piece, 4);
    if (failed) {
      return false;
    }
  }

  return write_number(REG_NEW_MESSAGE, 1);
}

// Poll until REPLY_READY flag is set, then read response from OUTBOX registers
static bool receive_string(char *text) {
  uint32_t reply_ready = 0;
  int waited_msec = 0;
  while (reply_ready != 1 && running) {
    if (!read_number(REG_REPLY_READY, &reply_ready)) {
      return false;
    }
    if (waited_msec >= REPLY_TIMEOUT_MSEC) {
      printf("  The slave did not answer in time.\n");
      return false;
    }
    sleep_msec(1);
    waited_msec++;
  }

  if (!running) {
    return false;
  }

  for (int i = 0; i < TEXT_REGISTERS; i++) {
    uint8_t mailbox = REG_OUTBOX_FIRST + i;
    char *piece = &text[i * 4];
    bool failed = iic_read_register(IIC0, SLAVE_ADDRESS, mailbox,
                                    (uint8_t *) piece, 4);
    if (failed) {
      return false;
    }
    if (memchr(piece, '\0', 4) != NULL) {
      break;
    }
  }
  text[MAX_TEXT_BYTES] = '\0';
  return true;
}

// Current Unix timestamp in milliseconds
static uint64_t timestamp_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (uint64_t) now.tv_sec * 1000 + (uint64_t) now.tv_nsec / 1000000;
}

// Send string and verify echoed reply
static void send_and_check(const char *message) {
  char reply[MAX_TEXT_BYTES + 1] = {0};

  printf("Sending : \"%s\"\n", message);

  if (!send_string(message)) {
    printf("  Could not send. Is the slave running and wired correctly?\n\n");
    return;
  }
  if (!receive_string(reply)) {
    printf("  Could not read the reply.\n\n");
    return;
  }

  uint64_t recv_timestamp = timestamp_ms();

  printf("Received: \"%s\"\n", reply);
  printf("  Received timestamp : %llu ms\n", (unsigned long long) recv_timestamp);

  if (strncmp(message, reply, MAX_TEXT_BYTES - 1) == 0) {
    printf("  OK: the slave sent back the same string (%zu bytes).\n\n",
           strlen(reply));
  } else {
    printf("  NOT OK: the reply is different.\n\n");
  }
  fflush(stdout);
}

// Escape special characters for JSON payloads
static void json_escape(const char *text, char *out, size_t out_size) {
  size_t j = 0;
  for (size_t i = 0; text[i] != '\0'; i++) {
    if (j + 3 > out_size) {
      break;
    }
    char c = text[i];
    if (c == '"' || c == '\\') {
      out[j++] = '\\';
      out[j++] = c;
    } else if (c == '\n') {
      out[j++] = '\\';
      out[j++] = 'n';
    } else if (c == '\t') {
      out[j++] = '\\';
      out[j++] = 't';
    } else if (c == '\r') {
      // skip
    } else {
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

// Format packet JSON with timestamp and fortune payload
static void make_packet(int number, char *packet, size_t packet_size) {
  char fortune[MAX_FORTUNE_BYTES] = {0};
  if (!fortune_get_random(fortune, sizeof(fortune))) {
    snprintf(fortune, sizeof(fortune),
             "Deliver yesterday, code today, think tomorrow.");
  }

  char payload[MAX_FORTUNE_BYTES] = {0};
  json_escape(fortune, payload, sizeof(payload));

  unsigned long long now = (unsigned long long) timestamp_ms();

  snprintf(packet, packet_size,
           "[IIC0] Packet #%d: {\"ping\": {\"id\": \"%llu\", "
           "\"timestamp_ms\": %llu, \"payload\": \"%s\"}, "
           "\"coordinates\": {\"X\": 109.82, \"Y\": 15.66}}",
           number, now, now, payload);
}

int main(void) {
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);
  signal(SIGHUP, handle_sigint);

  pynq_init();
  fortune_init(NULL);

  switchbox_set_pin(PIN_SCL, SWB_IIC0_SCL);
  switchbox_set_pin(PIN_SDA, SWB_IIC0_SDA);

  iic_init(IIC0);
  iic_reset(IIC0);

  printf("I2C master started, talking to the slave at address 0x%02X\n\n",
         SLAVE_ADDRESS);
  fflush(stdout);

  // Send test packets
  printf("Sending %d packets with %d ms in between:\n\n", NUMBER_OF_PACKETS,
         PACKET_INTERVAL_MSEC);

  for (int number = 1; number <= NUMBER_OF_PACKETS && running; number++) {
    char packet[MAX_TEXT_BYTES] = {0};
    make_packet(number, packet, sizeof(packet));
    send_and_check(packet);
    if (!running) {
      break;
    }
    sleep_msec(PACKET_INTERVAL_MSEC);
  }

  // Interactive console loop
  if (running) {
    printf("Now type your own messages to send to the slave.\n");
    printf("Press Enter on an empty line or type 'q' to stop (or Ctrl+C to quit).\n");
    fflush(stdout);

    char line[MAX_TEXT_BYTES + 2];
    while (running && fgets(line, sizeof(line), stdin) != NULL) {
      line[strcspn(line, "\r\n")] = '\0';
      if (line[0] == '\0' || strcmp(line, "q") == 0 || strcmp(line, "Q") == 0) {
        break;
      }
      send_and_check(line);
    }
  }

  printf("\nShutting down...\n");
  fflush(stdout);
  iic_destroy(IIC0);
  fortune_cleanup();
  pynq_destroy();
  return EXIT_SUCCESS;
}
