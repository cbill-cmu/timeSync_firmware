#include "gps.h"
#include "uart.h"
#include "debug_uart.h"
#include <stddef.h>

/* ---- USART1 RX: ISR fills a ring buffer, gps_service() drains it ---- */

#define GPS_RX_BUF_SIZE 256U
static volatile uint8_t rx_buf[GPS_RX_BUF_SIZE];
static volatile uint32_t rx_head = 0; /* written only by the ISR */
static volatile uint32_t rx_tail = 0; /* written only by gps_service() */
static volatile uint32_t bytes_seen = 0;
static volatile uint32_t uart_errors = 0;
static volatile uint32_t rx_dropped = 0;

void USART1_IRQHandler(void) {
  uint32_t isr = USART1->ISR;

  if (isr & USART_ISR_RXNE) {
    uint8_t c = (uint8_t)(USART1->RDR & 0xFFU); /* read clears RXNE */
    bytes_seen++;
    uint32_t next_head = (rx_head + 1U) % GPS_RX_BUF_SIZE;
    if (next_head != rx_tail) {
      rx_buf[rx_head] = c;
      rx_head = next_head;
    } else {
      rx_dropped++;
    }
  }

  if (isr & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
    uart_errors++;
    USART1->ICR = USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE;
  }
}

void gps_init(uint32_t baud) {
  /* PA9 = USART1_TX, PA10 = USART1_RX, both AF7. */
  GPIOA->MODER &= ~(GPIO_MODER_MODE9_Msk | GPIO_MODER_MODE10_Msk);
  GPIOA->MODER |= (0x2U << GPIO_MODER_MODE9_Pos) | (0x2U << GPIO_MODER_MODE10_Pos);
  GPIOA->AFR[1] &= ~(GPIO_AFRH_AFSEL9_Msk | GPIO_AFRH_AFSEL10_Msk);
  GPIOA->AFR[1] |= (7U << GPIO_AFRH_AFSEL9_Pos) | (7U << GPIO_AFRH_AFSEL10_Pos);

  uart_init(USART1, baud);
  USART1->CR1 |= USART_CR1_RXNEIE;
  NVIC_EnableIRQ(USART1_IRQn);
}

/* ---- NMEA 0183 sentence assembly + checksum + field parsing ----
 *
 * All of this runs in gps_service(), called from the main loop -- not from
 * the ISR -- to keep USART1_IRQHandler doing only the minimal ring-buffer
 * push, per the ISR-discipline note in system-design.md. */

#define LINE_MAX 96U /* NMEA sentences are <=82 chars incl '$'..CRLF; leaves margin */
static char line_buf[LINE_MAX];
static uint32_t line_len = 0;
static int line_active = 0; /* have we seen '$' and are still before CR/LF? */

static uint32_t sentences_seen = 0;
static uint32_t checksum_errors = 0;

static gps_fix_t current_fix;
static int fix_updated = 0;

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

/* Returns a pointer to the start of the Nth comma-separated field (0
 * indexed) within a NUL-terminated sentence body, or NULL if it doesn't
 * have that many fields. An empty field is a comma (or '*'/NUL) right at
 * the returned pointer. */
static const char *nth_field(const char *s, int n) {
  int field = 0;
  while (field < n) {
    while (*s && *s != ',' && *s != '*') s++;
    if (*s != ',') return NULL; /* hit '*' or NUL before enough commas */
    s++;
    field++;
  }
  return s;
}

static int field_empty(const char *f) {
  return f == NULL || *f == ',' || *f == '*' || *f == '\0';
}

static uint32_t parse_udec(const char *s, int max_digits, int *consumed) {
  uint32_t v = 0;
  int n = 0;
  while (n < max_digits && is_digit(*s)) {
    v = v * 10U + (uint32_t)(*s - '0');
    s++;
    n++;
  }
  if (consumed) *consumed = n;
  return v;
}

/* Parses an NMEA "ddmm.mmmm" (deg_digits=2, lat) or "dddmm.mmmm"
 * (deg_digits=3, lon) coordinate field into degrees * 1e7. Returns 1 on
 * success, 0 if the field doesn't look like a coordinate (e.g. empty). */
static int parse_coord_1e7(const char *f, int deg_digits, int32_t *out_1e7) {
  if (field_empty(f)) return 0;
  int consumed;
  uint32_t deg = parse_udec(f, deg_digits, &consumed);
  if (consumed != deg_digits) return 0;
  f += deg_digits;
  uint32_t min_whole = parse_udec(f, 2, &consumed);
  if (consumed != 2) return 0;
  f += 2;
  uint32_t min_frac = 0;
  uint32_t min_frac_scale = 1; /* 10^ndigits of the fractional part read */
  if (*f == '.') {
    f++;
    int frac_digits;
    min_frac = parse_udec(f, 6, &frac_digits);
    for (int i = 0; i < frac_digits; i++) min_frac_scale *= 10U;
  }
  /* minutes, scaled by 1e7: min_whole*1e7 + min_frac*1e7/min_frac_scale */
  uint64_t minutes_1e7 = (uint64_t)min_whole * 10000000ULL +
                          ((uint64_t)min_frac * 10000000ULL) / min_frac_scale;
  uint64_t degrees_1e7 = (uint64_t)deg * 10000000ULL + minutes_1e7 / 60U;
  *out_1e7 = (int32_t)degrees_1e7;
  return 1;
}

static void parse_time(const char *f, gps_fix_t *fix) {
  if (field_empty(f)) {
    fix->has_time = 0;
    return;
  }
  int c1, c2, c3;
  uint32_t hh = parse_udec(f, 2, &c1);
  uint32_t mm = parse_udec(f + 2, 2, &c2);
  uint32_t ss = parse_udec(f + 4, 2, &c3);
  if (c1 == 2 && c2 == 2 && c3 == 2) {
    fix->has_time = 1;
    fix->hh = (uint8_t)hh;
    fix->mm = (uint8_t)mm;
    fix->ss = (uint8_t)ss;
  } else {
    fix->has_time = 0;
  }
}

static void parse_date(const char *f, gps_fix_t *fix) {
  if (field_empty(f)) {
    fix->has_date = 0;
    return;
  }
  int c1, c2, c3;
  uint32_t dd = parse_udec(f, 2, &c1);
  uint32_t mo = parse_udec(f + 2, 2, &c2);
  uint32_t yy = parse_udec(f + 4, 2, &c3);
  if (c1 == 2 && c2 == 2 && c3 == 2) {
    fix->has_date = 1;
    fix->day = (uint8_t)dd;
    fix->month = (uint8_t)mo;
    fix->year = (uint16_t)(2000U + yy); /* NMEA gives a 2-digit year */
  } else {
    fix->has_date = 0;
  }
}

/* $GPRMC,hhmmss.ss,status,lat,N/S,lon,E/W,speed,course,ddmmyy,magvar,E/W,mode*cs */
static void parse_rmc(const char *body) {
  gps_fix_t fix = current_fix;
  const char *f;

  f = nth_field(body, 0);
  parse_time(f, &fix);

  f = nth_field(body, 1);
  fix.fix_valid = (f && *f == 'A');

  const char *lat_f = nth_field(body, 2);
  const char *ns_f = nth_field(body, 3);
  const char *lon_f = nth_field(body, 4);
  const char *ew_f = nth_field(body, 5);
  int32_t lat_1e7, lon_1e7;
  if (parse_coord_1e7(lat_f, 2, &lat_1e7) && parse_coord_1e7(lon_f, 3, &lon_1e7)) {
    if (!field_empty(ns_f) && *ns_f == 'S') lat_1e7 = -lat_1e7;
    if (!field_empty(ew_f) && *ew_f == 'W') lon_1e7 = -lon_1e7;
    fix.has_position = 1;
    fix.lat_1e7 = lat_1e7;
    fix.lon_1e7 = lon_1e7;
  } else {
    fix.has_position = 0;
  }

  f = nth_field(body, 8);
  parse_date(f, &fix);

  current_fix = fix;
  fix_updated = 1;
}

/* $GPGGA,hhmmss.ss,lat,N/S,lon,E/W,fixquality,numSats,hdop,alt,M,geoidsep,M,dgpsage,dgpsid*cs */
static void parse_gga(const char *body) {
  gps_fix_t fix = current_fix;
  const char *f;

  f = nth_field(body, 0);
  parse_time(f, &fix);

  const char *lat_f = nth_field(body, 1);
  const char *ns_f = nth_field(body, 2);
  const char *lon_f = nth_field(body, 3);
  const char *ew_f = nth_field(body, 4);
  int32_t lat_1e7, lon_1e7;
  if (parse_coord_1e7(lat_f, 2, &lat_1e7) && parse_coord_1e7(lon_f, 3, &lon_1e7)) {
    if (!field_empty(ns_f) && *ns_f == 'S') lat_1e7 = -lat_1e7;
    if (!field_empty(ew_f) && *ew_f == 'W') lon_1e7 = -lon_1e7;
    fix.has_position = 1;
    fix.lat_1e7 = lat_1e7;
    fix.lon_1e7 = lon_1e7;
  } else {
    fix.has_position = 0;
  }

  f = nth_field(body, 5);
  int consumed;
  uint32_t quality = field_empty(f) ? 0 : parse_udec(f, 1, &consumed);
  fix.fix_valid = quality > 0;

  f = nth_field(body, 6);
  if (!field_empty(f)) {
    uint32_t sats = parse_udec(f, 2, &consumed);
    if (consumed > 0) {
      fix.has_satellites = 1;
      fix.satellites = (uint8_t)sats;
    }
  }

  current_fix = fix;
  fix_updated = 1;
}

/* Validates "$...*hh" checksum (XOR of all bytes between '$' and '*') and,
 * if the sentence type is one we understand, dispatches to its parser.
 * line_buf is NUL-terminated by the caller before this runs. */
static void process_line(void) {
  if (line_len < 1 || line_buf[0] != '$') return;
  sentences_seen++;

  const char *star = 0;
  uint8_t checksum = 0;
  uint32_t i;
  for (i = 1; i < line_len; i++) {
    if (line_buf[i] == '*') {
      star = &line_buf[i];
      break;
    }
    checksum ^= (uint8_t)line_buf[i];
  }
  if (!star || (line_len - (uint32_t)(star - line_buf)) < 3) {
    checksum_errors++;
    return;
  }
  int hi = hex_nibble(star[1]);
  int lo = hex_nibble(star[2]);
  if (hi < 0 || lo < 0 || (uint8_t)((hi << 4) | lo) != checksum) {
    checksum_errors++;
    return;
  }

  /* line_buf[1..2] = talker ID (GP/GN/GL/...), [3..5] = sentence type,
   * [6] = the comma right after the type -- field 0 starts at [7], not
   * [6], so nth_field()'s "field 0" call actually lands on real content
   * instead of the comma itself. Match on type only so we're agnostic to
   * which talker ID this module (or a future one) uses. */
  if (line_len >= 7) {
    const char *type = &line_buf[3];
    if (type[0] == 'R' && type[1] == 'M' && type[2] == 'C') {
      parse_rmc(&line_buf[7]);
    } else if (type[0] == 'G' && type[1] == 'G' && type[2] == 'A') {
      parse_gga(&line_buf[7]);
    }
  }
}

static void assemble_byte(uint8_t c) {
  if (c == '$') {
    line_active = 1;
    line_len = 0;
    line_buf[line_len++] = (char)c;
    return;
  }
  if (!line_active) return;

  if (c == '\r' || c == '\n') {
    if (line_len > 0) {
      line_buf[line_len] = '\0';
      process_line();
    }
    line_active = 0;
    line_len = 0;
    return;
  }

  if (line_len + 1U >= LINE_MAX) {
    /* Sentence longer than we expect -- abandon it rather than overflow. */
    line_active = 0;
    line_len = 0;
    return;
  }
  line_buf[line_len++] = (char)c;
}

void gps_service(void) {
  while (rx_tail != rx_head) {
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1U) % GPS_RX_BUF_SIZE;
    debug_putc((char)c);
    assemble_byte(c);
  }
}

int gps_fix_updated(void) {
  if (fix_updated) {
    fix_updated = 0;
    return 1;
  }
  return 0;
}

void gps_get_fix(gps_fix_t *out) {
  *out = current_fix;
}

uint32_t gps_bytes_seen(void) { return bytes_seen; }
uint32_t gps_uart_errors(void) { return uart_errors; }
uint32_t gps_rx_dropped(void) { return rx_dropped; }
uint32_t gps_sentences_seen(void) { return sentences_seen; }
uint32_t gps_checksum_errors(void) { return checksum_errors; }
