#include <libpynq.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************************
 *  I2C MASTER SIMPLE - STRING ECHO
 *
 *
 *  I2c Master Simple pingpong test
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
#define PACKET_INTERVAL_MSEC 500

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
      printf("  Slave waited to fucking long\n");
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

// Send string and verify echoed reply
static void send_and_check(const char *message) {
  char reply[MAX_TEXT_BYTES + 1] = {0};

  printf("Sending : \"%s\"\n", message);

  if (!send_string(message)) {
    printf("  Could not send. Is slave working?\n\n");
    return;
  }
  if (!receive_string(reply)) {
    printf("  Could not read the reply.\n\n");
    return;
  }

  printf("Received: \"%s\"\n", reply);

  if (strncmp(message, reply, MAX_TEXT_BYTES - 1) == 0) {
    printf("  OK: the slave sent back the same string (%zu bytes).\n\n",
           strlen(reply));
  } else {
    printf("  NO OK: the reply is different.\n\n");
  }
  fflush(stdout);
}

static const char *static_messages[] = {
    "Hello sending test message from master",
    "test test test test test etst testjiposfadgkl;hjasredgyh8u;9oppsgdfknjl.sdgfahjkl;asdfop;'jweq4rt8up9rw234 q890p7234iuoprfsga7890-2354rqhjikafsejoi;sadfghuiolsdaf,.mzcxvl;/kr3we89u023489-02314r890-89-043tuoip234rtqhjkldsv7u8902345hjoikfesa;mnlsdvajkl;34tpu098l;nkdasfv",
    "https://www.youtube.com/watch?v=5_bHuCwKmkI",
    "im bored",
    "i wanna use python",
    " ______________________________________\n< Stay away from flying saucers today. >\n --------------------------------------\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||"
};

#define STATIC_MESSAGE_COUNT (sizeof(static_messages) / sizeof(static_messages[0]))

int main(void) {
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);
  signal(SIGHUP, handle_sigint);

  pynq_init();

  switchbox_set_pin(PIN_SCL, SWB_IIC0_SCL);
  switchbox_set_pin(PIN_SDA, SWB_IIC0_SDA);

  iic_init(IIC0);
  iic_reset(IIC0);

  printf("I2C Simple Master started, talking to slave 0x%02X\n\n",
         SLAVE_ADDRESS);
  fflush(stdout);

  // Send test packets
  printf("Sending %zu test messages:\n\n", STATIC_MESSAGE_COUNT);

  for (size_t i = 0; i < STATIC_MESSAGE_COUNT && running; i++) {
    send_and_check(static_messages[i]);
    if (!running) {
      break;
    }
    sleep_msec(PACKET_INTERVAL_MSEC);
  }

  printf("\nfucking off...\n");
  fflush(stdout);
  iic_destroy(IIC0);
  pynq_destroy();
  return EXIT_SUCCESS;
}
