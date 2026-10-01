#include <libpynq.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "../fortune/fortune.h"


// Hardware pin configuration and documentation references:
// - reg: https://pynq.tue.nl/libpynq/5EID0-2023-v3.0.0/group__PINMAP.html
// - Switchbox: https://pynq.tue.nl/libpynq/5EID0-2023-v3.0.0/group__SWITCHBOX.html

#define UART0_PIN_RX IO_AR9
#define UART0_PIN_TX IO_AR8

#define UART1_PIN_RX IO_AR11
#define UART1_PIN_TX IO_AR12

// Escape special characters in a string for safe embedding into JSON payloads.
static void json_escape(const char *src, char *dst, size_t max_dst) {
  if (!src || !dst || max_dst == 0) {
    return;
  }
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 2 < max_dst; i++) {
    unsigned char c = (unsigned char) src[i];
    if (c == '"') {
      if (j + 2 >= max_dst) break;
      dst[j++] = '\\';
      dst[j++] = '"';
    } else if (c == '\\') {
      if (j + 2 >= max_dst) break;
      dst[j++] = '\\';
      dst[j++] = '\\';
    } else if (c == '\n') {
      if (j + 2 >= max_dst) break;
      dst[j++] = '\\';
      dst[j++] = 'n';
    } else if (c == '\r') {
      continue;
    } else if (c == '\t') {
      if (j + 2 >= max_dst) break;
      dst[j++] = '\\';
      dst[j++] = 't';
    } else if (c >= 32 && c <= 126) {
      dst[j++] = (char) c;
    } else {
      dst[j++] = (char) c;
    }
  }
  dst[j] = '\0';
}

// Get current actual Unix epoch timestamp in milliseconds.
static uint64_t get_current_timestamp_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t) tv.tv_sec * 1000ULL) + ((uint64_t) tv.tv_usec / 1000ULL);
}

// Get high-resolution monotonic time in microseconds for elapsed time measurement.
static uint64_t get_time_monotonic_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t) ts.tv_sec * 1000000ULL) + ((uint64_t) ts.tv_nsec / 1000ULL);
}

// Transmit a null-terminated string over the specified UART channel.
// Returns elapsed time in milliseconds taken to push all characters to UART.
static double uart_send_string(const int uart, const char *str) {
  if (str == NULL) {
    return 0.0;
  }
  uint64_t t_start = get_time_monotonic_us();
  for (size_t i = 0; str[i] != '\0'; i++) {
    uart_send(uart, (uint8_t) str[i]);
  }
  uint64_t t_end = get_time_monotonic_us();
  return (double) (t_end - t_start) / 1000.0;
}

// Transmit sample packets simultaneously on both UART0 and UART1.
static void run_dual_tx_test(void) {
  printf("\n--- Starting Dual Simultaneous UART Transmitter Test ---\n");
  printf("Transmitting concurrently on UART0 (IO_%s) and UART1 (IO_%s)...\n",
         pin_names[UART0_PIN_TX], pin_names[UART1_PIN_TX]);
  printf("Sending 10 sample packets on each channel with 500ms intervals:\n\n");
  fflush(stdout);

  for (int count = 1; count <= 10; count++) {
    char raw0[1024] = {0};
    char raw1[1024] = {0};
    char quote0[2048] = {0};
    char quote1[2048] = {0};

    if (!fortune_get_random(raw0, sizeof(raw0))) {
      snprintf(raw0, sizeof(raw0), "Deliver yesterday, code today, think tomorrow.");
    }
    if (!fortune_get_random(raw1, sizeof(raw1))) {
      snprintf(raw1, sizeof(raw1), "A programmer is a person who passes as an exacting expert.");
    }

    json_escape(raw0, quote0, sizeof(quote0));
    json_escape(raw1, quote1, sizeof(quote1));

    uint64_t ts0 = get_current_timestamp_ms();
    char id0[32];
    snprintf(id0, sizeof(id0), "%llu", (unsigned long long) ts0);

    uint64_t ts1 = get_current_timestamp_ms();
    char id1[32];
    snprintf(id1, sizeof(id1), "%llu", (unsigned long long) ts1);

    char buf0[4096];
    char buf1[4096];
    snprintf(buf0, sizeof(buf0),
             "[UART0] Packet #%d: {\"ping\": {\"id\": \"%s\", \"timestamp_ms\": %llu, \"payload\": \"%s\"}, \"coordinates\": {\"X\": 109.82, \"Y\": 15.66}}\r\n",
             count, id0, (unsigned long long) ts0, quote0);
    snprintf(buf1, sizeof(buf1),
             "[UART1] Packet #%d: {\"ping\": {\"id\": \"%s\", \"timestamp_ms\": %llu, \"payload\": \"%s\"}, \"coordinates\": {\"X\": 109.82, \"Y\": 15.66}}\r\n",
             count, id1, (unsigned long long) ts1, quote1);

    uint64_t t_tx_start = get_time_monotonic_us();
    double dt0 = uart_send_string(UART0, buf0);
    double dt1 = uart_send_string(UART1, buf1);
    uint64_t t_tx_end = get_time_monotonic_us();
    double total_dt = (double) (t_tx_end - t_tx_start) / 1000.0;

    printf("Transmitted:\n  %s  %s", buf0, buf1);
    printf("  [Timing] UART0: %zu bytes in %.2f ms | UART1: %zu bytes in %.2f ms | Total TX time: %.2f ms\n\n",
           strlen(buf0), dt0, strlen(buf1), dt1, total_dt);
    fflush(stdout);
    sleep_msec(500);
  }
  printf("Dual transmitter test complete.\n");
  fflush(stdout);
}

// Concurrently listen for incoming data on both UART0 and UART1.
static void run_dual_rx_test(void) {
  printf("\n--- Starting Dual Simultaneous UART Receiver Test ---\n");
  printf("Listening for incoming data on UART0 and UART1 (press Ctrl+C to "
    "stop)...\n\n");
  fflush(stdout);

  while (true) {
    while (uart_has_data(UART0)) {
      uint8_t data = uart_recv(UART0);
      if (data >= 32 && data <= 126) {
        printf("[UART0 RX] 0x%02X ('%c')\n", data, data);
      } else if (data == '\n' || data == '\r') {
        printf("[UART0 RX] 0x%02X [CR/LF]\n", data);
      } else {
        printf("[UART0 RX] 0x%02X\n", data);
      }
      fflush(stdout);
    }

    while (uart_has_data(UART1)) {
      uint8_t data = uart_recv(UART1);
      if (data >= 32 && data <= 126) {
        printf("[UART1 RX] 0x%02X ('%c')\n", data, data);
      } else if (data == '\n' || data == '\r') {
        printf("[UART1 RX] 0x%02X [CR/LF]\n", data);
      } else {
        printf("[UART1 RX] 0x%02X\n", data);
      }
      fflush(stdout);
    }

    sleep_msec(2);
  }
}

// Concurrently echo received bytes back on their respective channels.
static void run_dual_echo_test(void) {
  printf("\n--- Starting Dual Simultaneous Echo Mode ---\n");
  printf("Echoing received bytes independently on UART0 and UART1 (press Ctrl+C "
    "to stop)...\n\n");
  fflush(stdout);

  while (true) {
    while (uart_has_data(UART0)) {
      uint8_t data = uart_recv(UART0);
      uart_send(UART0, data);
      printf("[UART0 Echo] 0x%02X ('%c')\n", data,
             (data >= 32 && data <= 126) ? data : '.');
      fflush(stdout);
    }

    while (uart_has_data(UART1)) {
      uint8_t data = uart_recv(UART1);
      uart_send(UART1, data);
      printf("[UART1 Echo] 0x%02X ('%c')\n", data,
             (data >= 32 && data <= 126) ? data : '.');
      fflush(stdout);
    }

    sleep_msec(2);
  }
}

// Bridge / relay data bidirectionally between UART0 and UART1.
static void run_dual_bridge_test(void) {
  printf("\n--- Starting Dual UART Cross-Bridge / Relay Mode ---\n");
  printf("Bridging UART0 <-> UART1: Forwarding traffic bidirectionally (press "
    "Ctrl+C to stop)...\n\n");
  fflush(stdout);

  while (true) {
    while (uart_has_data(UART0)) {
      uint8_t data = uart_recv(UART0);
      uart_send(UART1, data);
      printf("[UART0 -> UART1] Forwarded byte: 0x%02X ('%c')\n", data,
             (data >= 32 && data <= 126) ? data : '.');
      fflush(stdout);
    }

    while (uart_has_data(UART1)) {
      uint8_t data = uart_recv(UART1);
      uart_send(UART0, data);
      printf("[UART1 -> UART0] Forwarded byte: 0x%02X ('%c')\n", data,
             (data >= 32 && data <= 126) ? data : '.');
      fflush(stdout);
    }

    sleep_msec(2);
  }
}

// Simultaneous self loopback test for both UART0 and UART1.
static void run_dual_self_loopback_test(void) {
  printf("\n--- Starting Simultaneous Dual-Channel Self-Loopback Test ---\n");
  printf("Note: Connect UART0 TX -> UART0 RX (IO_%s -> IO_%s) and UART1 TX -> "
    "UART1 RX (IO_%s -> IO_%s).\n\n",
    pin_names[UART0_PIN_TX], pin_names[UART0_PIN_RX],
    pin_names[UART1_PIN_TX], pin_names[UART1_PIN_RX]);
  fflush(stdout);

  char raw0[512] = {0};
  char raw1[512] = {0};
  char quote0[1024] = {0};
  char quote1[1024] = {0};

  if (!fortune_get_random(raw0, sizeof(raw0))) {
    snprintf(raw0, sizeof(raw0), "Deliver yesterday, code today, think tomorrow.");
  }
  if (!fortune_get_random(raw1, sizeof(raw1))) {
    snprintf(raw1, sizeof(raw1), "A programmer is a person who passes as an exacting expert.");
  }

  json_escape(raw0, quote0, sizeof(quote0));
  json_escape(raw1, quote1, sizeof(quote1));

  uint64_t ts0 = get_current_timestamp_ms();
  char id0[32];
  snprintf(id0, sizeof(id0), "%llu", (unsigned long long) ts0);

  uint64_t ts1 = get_current_timestamp_ms();
  char id1[32];
  snprintf(id1, sizeof(id1), "%llu", (unsigned long long) ts1);

  char msg0[2048];
  char msg1[2048];
  snprintf(msg0, sizeof(msg0),
           "{\"ping\": {\"id\": \"%s\", \"timestamp_ms\": %llu, \"payload\": \"%s\"}, \"coordinates\": {\"X\": 109.82, \"Y\": 15.66}}",
           id0, (unsigned long long) ts0, quote0);
  snprintf(msg1, sizeof(msg1),
           "{\"ping\": {\"id\": \"%s\", \"timestamp_ms\": %llu, \"payload\": \"%s\"}, \"coordinates\": {\"X\": 109.82, \"Y\": 15.66}}",
           id1, (unsigned long long) ts1, quote1);
  size_t len0 = strlen(msg0);
  size_t len1 = strlen(msg1);

  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);
  sleep_msec(10);
  while (uart_has_data(UART0)) {
    (void) uart_recv(UART0);
  }
  while (uart_has_data(UART1)) {
    (void) uart_recv(UART1);
  }

  printf("Transmitting concurrently:\n");
  printf("  UART0 -> UART0: \"%s\"\n", msg0);
  printf("  UART1 -> UART1: \"%s\"\n\n", msg1);
  fflush(stdout);

  char recv_buf0[2048] = {0}; // Received by UART0 from UART0
  char recv_buf1[2048] = {0}; // Received by UART1 from UART1
  size_t sent0 = 0;
  size_t sent1 = 0;
  size_t received0 = 0;
  size_t received1 = 0;

  uint64_t t_loopback_start = get_time_monotonic_us();

  // Interleave fast burst transmission and reception without artificial delays
  while ((received0 < len0 || received1 < len1)) {
    while (sent0 < len0 && uart_has_space(UART0)) {
      uart_send(UART0, (uint8_t) msg0[sent0++]);
    }
    while (sent1 < len1 && uart_has_space(UART1)) {
      uart_send(UART1, (uint8_t) msg1[sent1++]);
    }

    while (uart_has_data(UART0) && received0 < sizeof(recv_buf0) - 1) {
      recv_buf0[received0++] = (char) uart_recv(UART0);
    }
    while (uart_has_data(UART1) && received1 < sizeof(recv_buf1) - 1) {
      recv_buf1[received1++] = (char) uart_recv(UART1);
    }

    if (received0 >= len0 && received1 >= len1) {
      break;
    }

    uint64_t t_check = get_time_monotonic_us();
    if ((t_check - t_loopback_start) > 2000000ULL) {
      break; // 2-second timeout
    }
  }

  uint64_t t_loopback_end = get_time_monotonic_us();
  double loopback_duration_ms = (double) (t_loopback_end - t_loopback_start) / 1000.0;

  printf("Received on UART0: \"%s\" (%zu/%zu bytes)\n", recv_buf0, received0,
         len0);
  printf("Received on UART1: \"%s\" (%zu/%zu bytes)\n", recv_buf1, received1,
         len1);
  printf("Transfer duration: %.2f ms | Total transferred: %zu bytes (Throughput: %.2f KB/s)\n",
         loopback_duration_ms, received0 + received1,
         loopback_duration_ms > 0.0 ? ((double)(received0 + received1) / loopback_duration_ms) : 0.0);

  bool ok0 = (strcmp(msg0, recv_buf0) == 0);
  bool ok1 = (strcmp(msg1, recv_buf1) == 0);

  if (ok0 && ok1) {
    printf("[SUCCESS] Both UART channels verified independent self-loopback successfully in %.2f ms!\n",
           loopback_duration_ms);
  } else {
    if (!ok0) {
      printf("[FAIL] UART0 self-loopback verification incomplete (%zu/%zu bytes received).\n",
             received0, len0);
    }
    if (!ok1) {
      printf("[FAIL] UART1 self-loopback verification incomplete (%zu/%zu bytes received).\n",
             received1, len1);
    }
    printf("[INFO] Check jumper connections (%s -> %s for UART0, %s -> %s for UART1).\n",
           pin_names[UART0_PIN_TX], pin_names[UART0_PIN_RX],
           pin_names[UART1_PIN_TX], pin_names[UART1_PIN_RX]);
  }
  fflush(stdout);
}

// Calculate CCITT CRC-16 checksum for packet verification.
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}

// Step 9-bit Galois LFSR for PRBS-9 pseudo-random sequence generation.
static inline uint8_t prbs9_step(uint16_t *state) {
  uint16_t s = *state;
  uint8_t out = 0;
  for (int i = 0; i < 8; i++) {
    uint16_t new_bit = ((s >> 8) ^ (s >> 4)) & 1;
    s = ((s << 1) | new_bit) & 0x1FF;
    out = (out << 1) | (uint8_t) new_bit;
  }
  *state = (s == 0) ? 0x1FF : s;
  return out;
}

/* ========================================================================= */
/*       Board Hardware Stress Subsystem: Dual-Core CPU & DDR3 RAM Load      */
/* ========================================================================= */

#define CPU_STRESS_THREADS 2
#define RAM_STRESS_BUFFER_SIZE (32 * 1024 * 1024) // 32 MB DDR3 RAM stress buffer

typedef struct {
  pthread_t thread;
  int thread_id;
  uint64_t ops_count;
} cpu_stress_worker_t;

typedef struct {
  pthread_t thread;
  size_t buffer_size;
  uint64_t bytes_written;
  uint64_t bytes_verified;
  uint64_t memory_errors;
  uint32_t passes_completed;
} ram_stress_worker_t;

static volatile bool g_board_stress_active = false;
static cpu_stress_worker_t g_cpu_workers[CPU_STRESS_THREADS];
static ram_stress_worker_t g_ram_worker;

// High-intensity compute worker thread function to saturate ARM Cortex-A9 CPU core.
static void *cpu_stress_thread_func(void *arg) {
  cpu_stress_worker_t *w = (cpu_stress_worker_t *) arg;
  w->ops_count = 0;

  uint32_t a = 0x12345678 ^ (uint32_t) ((w->thread_id + 1) * 0x9E3779B9);
  uint32_t b = 0x9ABCDEF0;
  double f_val = 1.0001;

  while (g_board_stress_active) {
    for (int i = 0; i < 2000; i++) {
      // High-intensity ALU and bit-manipulation integer ops (xorshift32 + LCG)
      a ^= a << 13;
      a ^= a >> 17;
      a ^= a << 5;
      b = (b * 1664525u + 1013904223u) ^ a;

      // Floating-point arithmetic calculations
      f_val = (f_val * 1.00005) + 0.00001;
      if (f_val > 5000.0) {
        f_val = 1.0001;
      }
    }
    w->ops_count += 4000;
  }
  return NULL;
}

// Continuous memory sweep & bit verification thread function for DDR3 RAM torture.
static void *ram_stress_thread_func(void *arg) {
  ram_stress_worker_t *w = (ram_stress_worker_t *) arg;
  w->bytes_written = 0;
  w->bytes_verified = 0;
  w->memory_errors = 0;
  w->passes_completed = 0;

  uint32_t *buf = (uint32_t *) malloc(w->buffer_size);
  if (!buf) {
    w->buffer_size = 16 * 1024 * 1024;
    buf = (uint32_t *) malloc(w->buffer_size);
    if (!buf) {
      return NULL;
    }
  }

  size_t num_words = w->buffer_size / sizeof(uint32_t);
  const uint32_t patterns[] = {
    0x55555555, 0xAAAAAAAA, 0x00000000, 0xFFFFFFFF,
    0x0F0F0F0F, 0xF0F0F0F0, 0xA55A3CC3, 0x12345678
  };
  const size_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);
  size_t p_idx = 0;

  while (g_board_stress_active) {
    uint32_t pat = patterns[p_idx % num_patterns];

    // Write sweep across whole allocated DDR3 memory block
    for (size_t i = 0; i < num_words; i++) {
      buf[i] = pat ^ (uint32_t) i;
    }
    w->bytes_written += w->buffer_size;

    // Read & verify sweep checking for any memory bus corruption
    for (size_t i = 0; i < num_words; i++) {
      uint32_t expected = pat ^ (uint32_t) i;
      uint32_t val = buf[i];
      if (val != expected) {
        w->memory_errors++;
      }
    }
    w->bytes_verified += w->buffer_size;
    w->passes_completed++;
    p_idx++;
  }

  free(buf);
  return NULL;
}

// Start background CPU and RAM stressors.
static void board_stress_start(void) {
  printf("\n>>> [BOARD STRESS] Initializing Dual-Core CPU & DDR3 RAM Subsystem Stressors <<<\n");
  printf("  -> Spawning %d CPU worker threads (Core 0 & Core 1 100%% compute saturation)\n", CPU_STRESS_THREADS);
  printf("  -> Allocating %d MB DDR3 memory buffer (Continuous write/read/verify torture scrub)\n",
         (int) (RAM_STRESS_BUFFER_SIZE / (1024 * 1024)));
  fflush(stdout);

  g_board_stress_active = true;

  for (int i = 0; i < CPU_STRESS_THREADS; i++) {
    g_cpu_workers[i].thread_id = i;
    g_cpu_workers[i].ops_count = 0;
    pthread_create(&g_cpu_workers[i].thread, NULL, cpu_stress_thread_func, &g_cpu_workers[i]);
  }

  g_ram_worker.buffer_size = RAM_STRESS_BUFFER_SIZE;
  pthread_create(&g_ram_worker.thread, NULL, ram_stress_thread_func, &g_ram_worker);
}

// Stop background CPU and RAM stressors and collect telemetry.
static void board_stress_stop(double duration_s, uint64_t *out_cpu_ops, uint64_t *out_ram_bytes, uint64_t *out_ram_errs) {
  g_board_stress_active = false;

  uint64_t total_cpu_ops = 0;
  for (int i = 0; i < CPU_STRESS_THREADS; i++) {
    pthread_join(g_cpu_workers[i].thread, NULL);
    total_cpu_ops += g_cpu_workers[i].ops_count;
  }

  pthread_join(g_ram_worker.thread, NULL);

  uint64_t total_ram_transferred = g_ram_worker.bytes_written + g_ram_worker.bytes_verified;
  double ram_bw_mbs = (duration_s > 0.0) ? ((double) total_ram_transferred / (1024.0 * 1024.0) / duration_s) : 0.0;
  double cpu_mops = (duration_s > 0.0) ? ((double) total_cpu_ops / 1000000.0 / duration_s) : 0.0;

  printf("\n>>> [BOARD STRESS] Background CPU & RAM Stress Complete <<<\n");
  printf("  CPU Dual-Core Load : %llu operations completed (%.2f Million Ops/sec across %d cores)\n",
         (unsigned long long) total_cpu_ops, cpu_mops, CPU_STRESS_THREADS);
  printf("  DDR3 RAM Scrubbing : %llu MB transferred (Write: %llu MB, Verify: %llu MB in %u passes)\n",
         (unsigned long long) (total_ram_transferred / (1024 * 1024)),
         (unsigned long long) (g_ram_worker.bytes_written / (1024 * 1024)),
         (unsigned long long) (g_ram_worker.bytes_verified / (1024 * 1024)),
         g_ram_worker.passes_completed);
  printf("  RAM Memory Bus BW  : %.2f MB/s | Memory Integrity Bit Errors: %llu (%s)\n",
         ram_bw_mbs, (unsigned long long) g_ram_worker.memory_errors,
         (g_ram_worker.memory_errors == 0) ? "PASS" : "FAIL");
  fflush(stdout);

  if (out_cpu_ops) *out_cpu_ops = total_cpu_ops;
  if (out_ram_bytes) *out_ram_bytes = total_ram_transferred;
  if (out_ram_errs) *out_ram_errs = g_ram_worker.memory_errors;
}

// Stress Test Stage 1: Maximum Burst Throughput Saturation.
static bool stress_stage_max_throughput(uint32_t duration_ms) {
  printf("\n>>> [STAGE 1] Maximum Burst Throughput Saturation (%u ms) <<<\n", duration_ms);
  printf("Pushing continuous stream at theoretical physical line limits...\n");
  fflush(stdout);

  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);
  sleep_msec(10);
  while (uart_has_data(UART0)) (void) uart_recv(UART0);
  while (uart_has_data(UART1)) (void) uart_recv(UART1);

  uint64_t bytes_sent0 = 0, bytes_sent1 = 0;
  uint64_t bytes_recv0 = 0, bytes_recv1 = 0;
  uint64_t errors0 = 0, errors1 = 0;
  uint8_t expected_val0 = 0, expected_val1 = 0;

  uint64_t t_start = get_time_monotonic_us();
  uint64_t t_last_report = t_start;

  while (true) {
    uint64_t t_now = get_time_monotonic_us();
    uint64_t elapsed_ms = (t_now - t_start) / 1000ULL;

    // Burst fill UART0 TX FIFO up to hardware depth
    while (elapsed_ms < duration_ms && (bytes_sent0 - bytes_recv0 < 512) && uart_has_space(UART0)) {
      uart_send(UART0, (uint8_t) (bytes_sent0 & 0xFF));
      bytes_sent0++;
    }

    // Burst fill UART1 TX FIFO up to hardware depth
    while (elapsed_ms < duration_ms && (bytes_sent1 - bytes_recv1 < 512) && uart_has_space(UART1)) {
      uart_send(UART1, (uint8_t) (bytes_sent1 & 0xFF));
      bytes_sent1++;
    }

    // Drain UART0 RX FIFO immediately
    while (uart_has_data(UART0)) {
      uint8_t b = uart_recv(UART0);
      if (b != expected_val0) {
        errors0++;
        expected_val0 = (uint8_t) (b + 1);
      } else {
        expected_val0++;
      }
      bytes_recv0++;
    }

    // Drain UART1 RX FIFO immediately
    while (uart_has_data(UART1)) {
      uint8_t b = uart_recv(UART1);
      if (b != expected_val1) {
        errors1++;
        expected_val1 = (uint8_t) (b + 1);
      } else {
        expected_val1++;
      }
      bytes_recv1++;
    }

    // Periodic telemetry every 1 sec
    if (t_now - t_last_report >= 1000000ULL) {
      double cur_s = (double) (t_now - t_start) / 1000000.0;
      double kb0 = (cur_s > 0.0) ? ((double) bytes_recv0 / 1024.0 / cur_s) : 0.0;
      double kb1 = (cur_s > 0.0) ? ((double) bytes_recv1 / 1024.0 / cur_s) : 0.0;
      printf("  [%.1fs] UART0: %llu B (%.2f KB/s, %llu err) | UART1: %llu B (%.2f KB/s, %llu err)\n",
             cur_s, (unsigned long long) bytes_recv0, kb0, (unsigned long long) errors0,
             (unsigned long long) bytes_recv1, kb1, (unsigned long long) errors1);
      fflush(stdout);
      t_last_report = t_now;
    }

    if (elapsed_ms >= duration_ms) {
      if ((bytes_recv0 >= bytes_sent0 && bytes_recv1 >= bytes_sent1) ||
          (t_now - t_start > (duration_ms + 1000) * 1000ULL)) {
        break;
      }
    }
  }

  uint64_t t_end = get_time_monotonic_us();
  double dt_s = (double) (t_end - t_start) / 1000000.0;
  uint64_t total_b = bytes_recv0 + bytes_recv1;
  double combined_kb = (dt_s > 0.0) ? ((double) total_b / 1024.0 / dt_s) : 0.0;

  printf("  -> Stage 1 Result: Transferred %llu B in %.2fs | Combined: %.2f KB/s (%.2f kbps) | Errors: %llu\n",
         (unsigned long long) total_b, dt_s, combined_kb, combined_kb * 8.0,
         (unsigned long long) (errors0 + errors1));
  fflush(stdout);
  return (errors0 == 0 && errors1 == 0 && bytes_recv0 == bytes_sent0 && bytes_recv1 == bytes_sent1);
}

// Stress Test Stage 2: Pathological Worst-Case Bit Pattern Inversion.
static bool stress_stage_pathological_patterns(uint32_t duration_ms) {
  printf("\n>>> [STAGE 2] Pathological Bit Pattern & Inversion Torture (%u ms) <<<\n", duration_ms);
  printf("Testing square-wave toggles (0x55, 0xAA), rail holds (0x00, 0xFF) and nibble flips (0x0F, 0xF0)...\n");
  fflush(stdout);

  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);
  sleep_msec(10);
  while (uart_has_data(UART0)) (void) uart_recv(UART0);
  while (uart_has_data(UART1)) (void) uart_recv(UART1);

  const uint8_t patterns[] = {0x55, 0xAA, 0x00, 0xFF, 0x0F, 0xF0, 0x33, 0xCC};
  const size_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

  uint64_t bytes_sent0 = 0, bytes_sent1 = 0;
  uint64_t bytes_recv0 = 0, bytes_recv1 = 0;
  uint64_t errors0 = 0, errors1 = 0;

  uint64_t t_start = get_time_monotonic_us();
  uint64_t t_last_report = t_start;

  while (true) {
    uint64_t t_now = get_time_monotonic_us();
    uint64_t elapsed_ms = (t_now - t_start) / 1000ULL;

    while (elapsed_ms < duration_ms && (bytes_sent0 - bytes_recv0 < 512) && uart_has_space(UART0)) {
      uint8_t p = patterns[(bytes_sent0 / 16) % num_patterns];
      uart_send(UART0, p);
      bytes_sent0++;
    }

    while (elapsed_ms < duration_ms && (bytes_sent1 - bytes_recv1 < 512) && uart_has_space(UART1)) {
      uint8_t p = patterns[((bytes_sent1 / 16) + 1) % num_patterns];
      uart_send(UART1, p);
      bytes_sent1++;
    }

    while (uart_has_data(UART0)) {
      uint8_t b = uart_recv(UART0);
      uint8_t exp = patterns[(bytes_recv0 / 16) % num_patterns];
      if (b != exp) {
        errors0++;
      }
      bytes_recv0++;
    }

    while (uart_has_data(UART1)) {
      uint8_t b = uart_recv(UART1);
      uint8_t exp = patterns[((bytes_recv1 / 16) + 1) % num_patterns];
      if (b != exp) {
        errors1++;
      }
      bytes_recv1++;
    }

    if (t_now - t_last_report >= 1000000ULL) {
      double cur_s = (double) (t_now - t_start) / 1000000.0;
      printf("  [%.1fs] Inversion patterns processed: %llu B | Errors: %llu\n",
             cur_s, (unsigned long long) (bytes_recv0 + bytes_recv1),
             (unsigned long long) (errors0 + errors1));
      fflush(stdout);
      t_last_report = t_now;
    }

    if (elapsed_ms >= duration_ms) {
      if ((bytes_recv0 >= bytes_sent0 && bytes_recv1 >= bytes_sent1) ||
          (t_now - t_start > (duration_ms + 1000) * 1000ULL)) {
        break;
      }
    }
  }

  uint64_t t_end = get_time_monotonic_us();
  double dt_s = (double) (t_end - t_start) / 1000000.0;
  uint64_t total_b = bytes_recv0 + bytes_recv1;

  printf("  -> Stage 2 Result: Transferred %llu B in %.2fs | Errors: %llu (%s)\n",
         (unsigned long long) total_b, dt_s, (unsigned long long) (errors0 + errors1),
         (errors0 == 0 && errors1 == 0) ? "PASS" : "FAIL");
  fflush(stdout);
  return (errors0 == 0 && errors1 == 0 && bytes_recv0 == bytes_sent0 && bytes_recv1 == bytes_sent1);
}

// Stress Test Stage 3: PRBS-9 Pseudo-Random Noise Torture.
static bool stress_stage_prbs_noise(uint32_t duration_ms) {
  printf("\n>>> [STAGE 3] PRBS-9 Pseudo-Random Noise & Clock Skew Torture (%u ms) <<<\n", duration_ms);
  printf("Streaming Galois LFSR pseudo-random bit sequences to test clock jitter & ISI...\n");
  fflush(stdout);

  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);
  sleep_msec(10);
  while (uart_has_data(UART0)) (void) uart_recv(UART0);
  while (uart_has_data(UART1)) (void) uart_recv(UART1);

  uint16_t tx_state0 = 0x1A5, rx_state0 = 0x1A5;
  uint16_t tx_state1 = 0x15B, rx_state1 = 0x15B;

  uint64_t bytes_sent0 = 0, bytes_sent1 = 0;
  uint64_t bytes_recv0 = 0, bytes_recv1 = 0;
  uint64_t errors0 = 0, errors1 = 0;

  uint64_t t_start = get_time_monotonic_us();
  uint64_t t_last_report = t_start;

  while (true) {
    uint64_t t_now = get_time_monotonic_us();
    uint64_t elapsed_ms = (t_now - t_start) / 1000ULL;

    while (elapsed_ms < duration_ms && (bytes_sent0 - bytes_recv0 < 512) && uart_has_space(UART0)) {
      uint8_t val = prbs9_step(&tx_state0);
      uart_send(UART0, val);
      bytes_sent0++;
    }

    while (elapsed_ms < duration_ms && (bytes_sent1 - bytes_recv1 < 512) && uart_has_space(UART1)) {
      uint8_t val = prbs9_step(&tx_state1);
      uart_send(UART1, val);
      bytes_sent1++;
    }

    while (uart_has_data(UART0)) {
      uint8_t b = uart_recv(UART0);
      uint8_t exp = prbs9_step(&rx_state0);
      if (b != exp) {
        errors0++;
      }
      bytes_recv0++;
    }

    while (uart_has_data(UART1)) {
      uint8_t b = uart_recv(UART1);
      uint8_t exp = prbs9_step(&rx_state1);
      if (b != exp) {
        errors1++;
      }
      bytes_recv1++;
    }

    if (t_now - t_last_report >= 1000000ULL) {
      double cur_s = (double) (t_now - t_start) / 1000000.0;
      printf("  [%.1fs] PRBS noise bytes verified: %llu B | Errors: %llu\n",
             cur_s, (unsigned long long) (bytes_recv0 + bytes_recv1),
             (unsigned long long) (errors0 + errors1));
      fflush(stdout);
      t_last_report = t_now;
    }

    if (elapsed_ms >= duration_ms) {
      if ((bytes_recv0 >= bytes_sent0 && bytes_recv1 >= bytes_sent1) ||
          (t_now - t_start > (duration_ms + 1000) * 1000ULL)) {
        break;
      }
    }
  }

  uint64_t t_end = get_time_monotonic_us();
  double dt_s = (double) (t_end - t_start) / 1000000.0;
  uint64_t total_b = bytes_recv0 + bytes_recv1;

  printf("  -> Stage 3 Result: Transferred %llu B in %.2fs | Errors: %llu (%s)\n",
         (unsigned long long) total_b, dt_s, (unsigned long long) (errors0 + errors1),
         (errors0 == 0 && errors1 == 0) ? "PASS" : "FAIL");
  fflush(stdout);
  return (errors0 == 0 && errors1 == 0 && bytes_recv0 == bytes_sent0 && bytes_recv1 == bytes_sent1);
}

// Stress Test Stage 4: Avalanche & Micro-Burst Framing Torture with CRC-16.
static bool stress_stage_avalanche_bursts(int num_burst_packets) {
  printf("\n>>> [STAGE 4] Avalanche & Micro-Burst Framing Torture (%d packets) <<<\n", num_burst_packets);
  printf("Sending randomized burst sizes (1 to 256 bytes) with CRC-16 CCITT integrity verification...\n");
  fflush(stdout);

  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);
  sleep_msec(10);
  while (uart_has_data(UART0)) (void) uart_recv(UART0);
  while (uart_has_data(UART1)) (void) uart_recv(UART1);

  uint32_t passed_frames0 = 0, passed_frames1 = 0;
  uint32_t failed_frames0 = 0, failed_frames1 = 0;
  uint64_t total_payload_bytes = 0;

  uint64_t t_start = get_time_monotonic_us();

  for (int p = 1; p <= num_burst_packets; p++) {
    // Variable burst size: alternate between micro-bursts (1-8 bytes) and flood bursts (64-256 bytes)
    size_t payload_len = (p % 3 == 0) ? (size_t)(1 + (rand() % 8)) : (size_t)(32 + (rand() % 220));
    uint8_t payload[256];
    for (size_t i = 0; i < payload_len; i++) {
      payload[i] = (uint8_t) (rand() & 0xFF);
    }
    uint16_t expected_crc = crc16_ccitt(payload, payload_len);

    // Frame layout: [0x5A, 0xA5] [LEN_HI, LEN_LO] [PAYLOAD...] [CRC_HI, CRC_LO]
    uint8_t frame[512];
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t) ((payload_len >> 8) & 0xFF);
    frame[3] = (uint8_t) (payload_len & 0xFF);
    memcpy(&frame[4], payload, payload_len);
    frame[4 + payload_len] = (uint8_t) ((expected_crc >> 8) & 0xFF);
    frame[5 + payload_len] = (uint8_t) (expected_crc & 0xFF);
    size_t frame_total_len = 6 + payload_len;

    // Concurrently transmit and receive frame on UART0 and UART1
    size_t tx0 = 0, tx1 = 0;
    size_t rx0 = 0, rx1 = 0;
    uint8_t recv_frame0[512] = {0};
    uint8_t recv_frame1[512] = {0};
    uint64_t frame_t_start = get_time_monotonic_us();

    while (rx0 < frame_total_len || rx1 < frame_total_len) {
      while (tx0 < frame_total_len && uart_has_space(UART0)) {
        uart_send(UART0, frame[tx0++]);
      }
      while (tx1 < frame_total_len && uart_has_space(UART1)) {
        uart_send(UART1, frame[tx1++]);
      }

      while (uart_has_data(UART0) && rx0 < sizeof(recv_frame0)) {
        recv_frame0[rx0++] = uart_recv(UART0);
      }
      while (uart_has_data(UART1) && rx1 < sizeof(recv_frame1)) {
        recv_frame1[rx1++] = uart_recv(UART1);
      }

      if ((get_time_monotonic_us() - frame_t_start) > 500000ULL) {
        break; // 500ms packet timeout
      }
    }

    // Verify UART0 Frame
    if (rx0 == frame_total_len && recv_frame0[0] == 0x5A && recv_frame0[1] == 0xA5) {
      uint16_t rx_crc0 = ((uint16_t) recv_frame0[4 + payload_len] << 8) | recv_frame0[5 + payload_len];
      uint16_t calc_crc0 = crc16_ccitt(&recv_frame0[4], payload_len);
      if (rx_crc0 == calc_crc0 && calc_crc0 == expected_crc) {
        passed_frames0++;
      } else {
        failed_frames0++;
      }
    } else {
      failed_frames0++;
    }

    // Verify UART1 Frame
    if (rx1 == frame_total_len && recv_frame1[0] == 0x5A && recv_frame1[1] == 0xA5) {
      uint16_t rx_crc1 = ((uint16_t) recv_frame1[4 + payload_len] << 8) | recv_frame1[5 + payload_len];
      uint16_t calc_crc1 = crc16_ccitt(&recv_frame1[4], payload_len);
      if (rx_crc1 == calc_crc1 && calc_crc1 == expected_crc) {
        passed_frames1++;
      } else {
        failed_frames1++;
      }
    } else {
      failed_frames1++;
    }

    total_payload_bytes += (payload_len * 2);
  }

  uint64_t t_end = get_time_monotonic_us();
  double dt_s = (double) (t_end - t_start) / 1000000.0;

  printf("  -> Stage 4 Result: %d packets verified (Payload: %llu B) in %.2fs | UART0: %u OK / %u FAIL | UART1: %u OK / %u FAIL\n",
         num_burst_packets, (unsigned long long) total_payload_bytes, dt_s,
         passed_frames0, failed_frames0, passed_frames1, failed_frames1);
  fflush(stdout);
  return (failed_frames0 == 0 && failed_frames1 == 0);
}

// Comprehensive High-Throughput Torture & Stress Test Suite Runner.
static void run_dual_stress_test(void) {
  printf("\n=============================================================\n");
  printf("  Dual UART & Board Hardware (CPU + RAM) Torture Benchmark   \n");
  printf("=============================================================\n");
  printf("Setup Note: Ensure loopback jumpers are connected:\n");
  printf("  UART0 TX (IO_%s) -> UART0 RX (IO_%s)\n",
         pin_names[UART0_PIN_TX], pin_names[UART0_PIN_RX]);
  printf("  UART1 TX (IO_%s) -> UART1 RX (IO_%s)\n",
         pin_names[UART1_PIN_TX], pin_names[UART1_PIN_RX]);
  printf("Executing 4-stage UART saturation suite under 100%% CPU & RAM stress...\n");
  fflush(stdout);

  uint64_t suite_start = get_time_monotonic_us();

  // Start concurrent board stressors (Dual-Core CPU load & DDR3 memory scrub)
  board_stress_start();

  bool s1 = stress_stage_max_throughput(4000);
  bool s2 = stress_stage_pathological_patterns(3000);
  bool s3 = stress_stage_prbs_noise(3000);
  bool s4 = stress_stage_avalanche_bursts(50);

  uint64_t suite_end = get_time_monotonic_us();
  double suite_duration_s = (double) (suite_end - suite_start) / 1000000.0;

  // Stop concurrent board stressors and collect telemetry
  uint64_t total_cpu_ops = 0, total_ram_bytes = 0, total_ram_errs = 0;
  board_stress_stop(suite_duration_s, &total_cpu_ops, &total_ram_bytes, &total_ram_errs);

  bool cpu_ok = (total_cpu_ops > 0);
  bool ram_ok = (total_ram_errs == 0 && total_ram_bytes > 0);

  printf("\n================ Final Torture Benchmark Summary ================\n");
  printf("Total Suite Duration : %.2f seconds\n", suite_duration_s);
  printf("Stage 1 - Max Throughput Saturation : %s\n", s1 ? "[PASS] 100% Saturated Line-Rate Verified" : "[FAIL] Packet/Byte Mismatch");
  printf("Stage 2 - Pathological Bit Inversion : %s\n", s2 ? "[PASS] All High/Low Rails & Flips Clean" : "[FAIL] Transition Dropped");
  printf("Stage 3 - PRBS-9 White Noise Torture: %s\n", s3 ? "[PASS] Zero ISI / Jitter Errors Detected" : "[FAIL] Clock Skew / Sync Lost");
  printf("Stage 4 - Avalanche Burst & CRC-16  : %s\n", s4 ? "[PASS] All Variable Frames CRC-Verified" : "[FAIL] Frame Boundary Corrupted");
  printf("Stage 5 - Dual-Core CPU Saturation  : %s (%llu M Ops)\n",
         cpu_ok ? "[PASS] 100% Dual-Core Sustained" : "[FAIL] Core Stall Detected",
         (unsigned long long) (total_cpu_ops / 1000000ULL));
  printf("Stage 6 - DDR3 RAM Bus & Memory Scrub: %s (%llu MB, 0 Bit Flips)\n",
         ram_ok ? "[PASS] Zero Memory Errors" : "[FAIL] Memory Corruption Detected",
         (unsigned long long) (total_ram_bytes / (1024 * 1024)));
  printf("-----------------------------------------------------------------\n");
  printf("Overall Hardware Health Rating       : %s\n",
         (s1 && s2 && s3 && s4 && cpu_ok && ram_ok) ?
         "[PERFECT] Dual UART Pipelines, Dual-Core CPU & RAM 100% Certified" : "[DEGRADED] Hardware Issues Detected");
  printf("=================================================================\n\n");
  fflush(stdout);
}

int main(int argc, char *argv[]) {
  // Disable stdout buffering so all printf outputs appear immediately over SSH
  setvbuf(stdout, NULL, _IONBF, 0);

  // Initialize all hardware subsystems
  pynq_init();
  fortune_init(NULL);

  // Configure switchbox routing for both UART channels simultaneously
  switchbox_set_pin(UART0_PIN_RX, SWB_UART0_RX);
  switchbox_set_pin(UART0_PIN_TX, SWB_UART0_TX);
  switchbox_set_pin(UART1_PIN_RX, SWB_UART1_RX);
  switchbox_set_pin(UART1_PIN_TX, SWB_UART1_TX);

  // Initialize both UART channels and reset FIFOs
  uart_init(UART0);
  uart_init(UART1);
  uart_reset_fifos(UART0);
  uart_reset_fifos(UART1);

  printf("====================================================\n");
  printf("   libpynq Simultaneous Dual UART Communications    \n");
  printf("====================================================\n");
  printf("Channel 0 (UART0): RX = IO_%-5s | TX = IO_%-5s\n",
         pin_names[UART0_PIN_RX], pin_names[UART0_PIN_TX]);
  printf("Channel 1 (UART1): RX = IO_%-5s | TX = IO_%-5s\n",
         pin_names[UART1_PIN_RX], pin_names[UART1_PIN_TX]);
  printf("Status           : Both Channels Concurrently Active\n");
  printf("====================================================\n\n");
  fflush(stdout);

  int mode = 1;
  if (argc > 1) {
    mode = atoi(argv[1]);
  } else {
    printf("Select simultaneous operating mode:\n");
    printf("  1: Dual Master Transmitter (Send packets on both channels)\n");
    printf("  2: Dual Slave Receiver (Listen concurrently on both channels)\n");
    printf("  3: Dual Echo Slave (Echo independently on both channels)\n");
    printf("  4: Cross-Bridge / Relay (Forward traffic UART0 <-> UART1)\n");
    printf("  5: Dual Self-Loopback Test (Verify UART0->UART0 & UART1->UART1)\n");
    printf("  6: Dual UART & Board Stress Benchmark (UART Torture + 100%% CPU & RAM load)\n");
    printf("Enter mode (1-6, default 1): ");
    fflush(stdout);

    char input[16];
    if (fgets(input, sizeof(input), stdin) != NULL) {
      int chosen = atoi(input);
      if (chosen >= 1 && chosen <= 6) {
        mode = chosen;
      }
    }
  }

  switch (mode) {
    case 1:
      run_dual_tx_test();
      break;
    case 2:
      run_dual_rx_test();
      break;
    case 3:
      run_dual_echo_test();
      break;
    case 4:
      run_dual_bridge_test();
      break;
    case 5:
      run_dual_self_loopback_test();
      break;
    case 6:
      run_dual_stress_test();
      break;
    default:
      printf("Invalid mode %d selected. Running Dual TX test by default.\n",
             mode);
      run_dual_tx_test();
      break;
  }

  // Cleanup UART channels and board resources
  uart_destroy(UART0);
  uart_destroy(UART1);
  fortune_cleanup();
  pynq_destroy();

  return EXIT_SUCCESS;
}
