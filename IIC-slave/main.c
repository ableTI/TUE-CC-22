#include <libpynq.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************************
 *  I2C (also called IIC) SLAVE - STRING ECHO EXAMPLE
 *  https://pynq.tue.nl/libpynq/5EID0-2023-v3.0.0/group__IIC.html
 *
 *  The slave has 256 numbered registrars.
 *  Every registrar holds 4 bytes.
 *
 *  Registrar layout used:
 *      [0]          NEW_MESSAGE flag : master sets it to 1 when text is in
 *      [1]..[127]   INBOX text       : the string from the master (508 bytes)
 *      [128]        REPLY_READY flag : slave sets it to 1 when reply is in
 *      [129]..[255] OUTBOX text      : the string the slave sends back
 ******************************************************************************/

// The I2C address for this slave board.
#define MY_SLAVE_ADDRESS 0x70

// Board pins used for I2C communication.
#define PIN_SCL IO_PMODA3
#define PIN_SDA IO_PMODA4

#define NUMBER_OF_REGISTERS 256

#define REG_NEW_MESSAGE  0   // 1 = master has put a new string in the INBOX
#define REG_INBOX_FIRST  1   // first mailbox of the string from master
#define REG_REPLY_READY  128 // 1 = slave has put the reply in the OUTBOX
#define REG_OUTBOX_FIRST 129 // first mailbox of the string back to master

#define TEXT_REGISTERS 127
#define MAX_TEXT_BYTES (TEXT_REGISTERS * 4) // 127 mailboxes x 4 bytes = 508

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig)
{
  (void)sig;
  running = 0;
}

int main(void)
{
  uint32_t registers[NUMBER_OF_REGISTERS] = {0};

  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);
  signal(SIGHUP, handle_sigint);

  pynq_init();
  switchbox_set_pin(PIN_SCL, SWB_IIC0_SCL);
  switchbox_set_pin(PIN_SDA, SWB_IIC0_SDA);

  iic_init(IIC0);
  iic_reset(IIC0);
  iic_set_slave_mode(IIC0, MY_SLAVE_ADDRESS, registers, NUMBER_OF_REGISTERS);

  printf("I2C slave started at 0x%02X\n", MY_SLAVE_ADDRESS);
  printf("Waiting for strings from the master... (Ctrl+C to quit)\n");
  fflush(stdout);

  while (running) {
    iic_slave_mode_handler(IIC0);

    if (registers[REG_NEW_MESSAGE] == 1) {
      char text[MAX_TEXT_BYTES + 1];
      memcpy(text, &registers[REG_INBOX_FIRST], MAX_TEXT_BYTES);
      text[MAX_TEXT_BYTES] = '\0';

      printf("Received from our amazing and generous master (%zu bytes):\n  %s\n", strlen(text), text);

      memcpy(&registers[REG_OUTBOX_FIRST], text, MAX_TEXT_BYTES);

      registers[REG_NEW_MESSAGE] = 0;
      registers[REG_REPLY_READY] = 1;

      printf("sent echo back to the holy master.\n\n");
      fflush(stdout);
    }

    sleep_msec(1);
  }

  printf("\nbye bye \n");
  fflush(stdout);
  iic_destroy(IIC0);
  pynq_destroy();
  return EXIT_SUCCESS;
}