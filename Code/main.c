ENTRY(_start)
MEMORY {
  iram (rx) : ORIGIN = 0x40100000, LENGTH = 0x8000
  dram (rw) : ORIGIN = 0x3FFE8000, LENGTH = 0x14000
}
SECTIONS {
  .text : ALIGN(4) { *(.literal .text .literal.* .text.*) . = ALIGN(4); } > iram
  .rodata : ALIGN(4) { *(.rodata .rodata.*) . = ALIGN(4); } > dram
  .data   : ALIGN(4) { *(.data .data.*)     . = ALIGN(4); } > dram
  .bss (NOLOAD) : ALIGN(4) {
    _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .;
  } > dram
  _stack_top = ORIGIN(dram) + LENGTH(dram);
  /DISCARD/ : { *(.comment) *(.xtensa.info) }
}

.section .text, "ax"
    .align 4
    .global _start
_start:
    movi    a1, _stack_top
    j       startup

#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))

/* ---------- config ---------- */
#define CPU_HZ    80000000UL     /* ESP8266 default; verify with a scope if timing is off */
#define MPU_ADDR  0x68
#define MODE_RATE 0
#define DIR_SIGN  1
#define MIN_PWM   130
#define PWM_PERIOD_US 500        /* 2 kHz software PWM */
#define DEBUG     1

#define SDA  4
#define SCL  5
#define RPWM 12
#define LPWM 13
#define MUX_SDA  0x60000800 + 0x3C   /* GPIO4, func 0 */
#define MUX_SCL  0x60000800 + 0x40   /* GPIO5, func 0 */
#define MUX_RPWM 0x60000800 + 0x04   /* GPIO12 (MTDI), func 3 */
#define MUX_LPWM 0x60000800 + 0x08   /* GPIO13 (MTCK), func 3 */

/* ---------- registers ---------- */
#define GPIO_BASE        0x60000300
#define GPIO_OUT         (GPIO_BASE + 0x00)
#define GPIO_OUT_W1TS    (GPIO_BASE + 0x04)
#define GPIO_OUT_W1TC    (GPIO_BASE + 0x08)
#define GPIO_ENABLE_W1TS (GPIO_BASE + 0x10)
#define GPIO_ENABLE_W1TC (GPIO_BASE + 0x14)
#define GPIO_IN          (GPIO_BASE + 0x18)
#define WDT_CNTL         0x60000900
#define UART0_FIFO       0x60000000
#define UART0_STATUS     0x6000001C

/* ---------- watchdog off (defensive) ---------- */
static void wdt_off(void) { REG(WDT_CNTL) &= ~1u; }

/* ---------- timing (CPU cycle counter, same as ESP32) ---------- */
static inline uint32_t ccount(void) {
  uint32_t c; __asm__ volatile("rsr %0, ccount" : "=a"(c)); return c;
}
static void delay_us(uint32_t us) {
  uint32_t t = ccount(), n = us * (CPU_HZ / 1000000UL);
  while ((ccount() - t) < n) {}
}
static void delay_ms(uint32_t ms) { while (ms--) delay_us(1000); }

/* ---------- IO_MUX function select (bits 4,5,8 of the pin's mux reg) ---------- */
static void iomux_func(uint32_t reg, uint32_t func) {
  uint32_t v = REG(reg);
  v &= ~((1u << 4) | (1u << 5) | (1u << 8));
  v |= ((func & 1) << 4) | (((func >> 1) & 1) << 5) | (((func >> 2) & 1) << 8);
  REG(reg) = v;
}
static void gpio_out_init(int pin, uint32_t mux_reg, uint32_t func) {
  iomux_func(mux_reg, func);
  REG(GPIO_ENABLE_W1TS) = 1u << pin;
}

/* ---------- UART0 debug (ROM leaves it at 115200) ---------- */
static void uart_putc(char c) {
  while (((REG(UART0_STATUS) >> 16) & 0xFF) >= 126) {}
  REG(UART0_FIFO) = (uint32_t)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_putint(int32_t v) {
  char b[12]; int n = 0;
  if (v < 0) { uart_putc('-'); v = -v; }
  do { b[n++] = '0' + (v % 10); v /= 10; } while (v);
  while (n) uart_putc(b[--n]);
}

/* ---------- motor: software PWM, runs for ~total_us and returns ---------- */
static void motor_run(int c, uint32_t total_us) {
  if (c > 255) c = 255;
  if (c < -255) c = -255;
  int pin   = (c >= 0) ? RPWM : LPWM;
  int other = (c >= 0) ? LPWM : RPWM;
  REG(GPIO_OUT_W1TC) = 1u << other;
  uint32_t high_us = (uint32_t)((c < 0 ? -c : c) * PWM_PERIOD_US) / 255;
  uint32_t cycles = total_us / PWM_PERIOD_US;
  if (cycles == 0) cycles = 1;
  for (uint32_t i = 0; i < cycles; i++) {
    if (high_us) { REG(GPIO_OUT_W1TS) = 1u << pin; delay_us(high_us); }
    if (PWM_PERIOD_US > high_us) { REG(GPIO_OUT_W1TC) = 1u << pin; delay_us(PWM_PERIOD_US - high_us); }
  }
}

/* ---------- bit-banged I2C ---------- */
#define HALF_US 6
static inline void sda_lo(void) { REG(GPIO_ENABLE_W1TS) = 1u << SDA; }
static inline void sda_hi(void) { REG(GPIO_ENABLE_W1TC) = 1u << SDA; }
static inline void scl_lo(void) { REG(GPIO_ENABLE_W1TS) = 1u << SCL; }
static inline int  sda_rd(void) { return (REG(GPIO_IN) >> SDA) & 1; }
static inline int  scl_rd(void) { return (REG(GPIO_IN) >> SCL) & 1; }
static void scl_hi(void) {
  REG(GPIO_ENABLE_W1TC) = 1u << SCL;
  for (int n = 0; n < 2000 && !scl_rd(); n++) {}
}
static void i2c_delay(void) { delay_us(HALF_US); }

static void i2c_start(void) {
  sda_hi(); scl_hi(); i2c_delay();
  sda_lo(); i2c_delay();
  scl_lo(); i2c_delay();
}
static void i2c_stop(void) {
  sda_lo(); i2c_delay();
  scl_hi(); i2c_delay();
  sda_hi(); i2c_delay();
}
static int i2c_wbyte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) sda_hi(); else sda_lo();
    i2c_delay();
    scl_hi(); i2c_delay();
    scl_lo();
  }
  sda_hi(); i2c_delay();
  scl_hi(); i2c_delay();
  int ack = !sda_rd();
  scl_lo(); i2c_delay();
  return ack;
}
static uint8_t i2c_rbyte(int ack) {
  uint8_t b = 0;
  sda_hi();
  for (int i = 0; i < 8; i++) {
    i2c_delay();
    scl_hi(); i2c_delay();
    b = (uint8_t)((b << 1) | sda_rd());
    scl_lo();
  }
  if (ack) sda_lo(); else sda_hi();
  i2c_delay();
  scl_hi(); i2c_delay();
  scl_lo(); i2c_delay();
  sda_hi();
  return b;
}
static void i2c_init(void) {
  iomux_func(MUX_SDA, 0); REG(MUX_SDA) |= (1u << 7);   /* GPIO func + pull-up */
  iomux_func(MUX_SCL, 0); REG(MUX_SCL) |= (1u << 7);
  REG(GPIO_OUT_W1TC) = (1u << SDA) | (1u << SCL);       /* output value 0 */
  sda_hi(); scl_hi();
  for (int i = 0; i < 9; i++) { scl_lo(); i2c_delay(); scl_hi(); i2c_delay(); }
  i2c_stop();
}

/* ---------- MPU6050 ---------- */
static int mpu_write(uint8_t reg, uint8_t val) {
  i2c_start();
  int ok = i2c_wbyte(MPU_ADDR << 1) && i2c_wbyte(reg) && i2c_wbyte(val);
  i2c_stop();
  return ok;
}
static int mpu_read(int16_t *ay, int16_t *az, int16_t *gx) {
  uint8_t b[10];
  i2c_start();
  if (!i2c_wbyte(MPU_ADDR << 1)) { i2c_stop(); return 0; }
  if (!i2c_wbyte(0x3B))          { i2c_stop(); return 0; }
  i2c_start();
  if (!i2c_wbyte((MPU_ADDR << 1) | 1)) { i2c_stop(); return 0; }
  for (int i = 0; i < 10; i++) b[i] = i2c_rbyte(i < 9);
  i2c_stop();
  *ay = (int16_t)((b[2] << 8) | b[3]);
  *az = (int16_t)((b[4] << 8) | b[5]);
  *gx = (int16_t)((b[8] << 8) | b[9]);
  return 1;
}

/* ---------- fixed-point math (centi-degrees) ---------- */
static int32_t atan2_cdeg(int32_t y, int32_t x) {
  if (x == 0 && y == 0) return 0;
  int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  int32_t mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
  int32_t z = (mn << 12) / mx;
  int32_t t = (z * (4096 - z)) >> 12;
  int32_t k = 1402 + ((380 * z) >> 12);
  int32_t a = ((4500 * z) >> 12) + ((t * k) >> 12);
  if (ay > ax) a = 9000 - a;
  if (x < 0)   a = 18000 - a;
  if (y < 0)   a = -a;
  return a;
}
static int32_t wrap(int32_t a) {
  if (a > 18000) a -= 36000; else if (a < -18000) a += 36000;
  return a;
}
static int shape(int32_t x, int32_t dead, int32_t full) {
  int32_t a = x < 0 ? -x : x;
  if (a < dead) return 0;
  if (a > full) a = full;
  int32_t v = MIN_PWM + ((a - dead) * (255 - MIN_PWM)) / (full - dead);
  return x > 0 ? v : -v;
}

/* ---------- app ---------- */
void app_main(void) {
  gpio_out_init(RPWM, MUX_RPWM, 3);   /* func 3 = GPIO on MTDI/MTCK pins */
  gpio_out_init(LPWM, MUX_LPWM, 3);
  motor_run(0, 1);

  i2c_init();
  delay_ms(100);
  mpu_write(0x6B, 0x00);
  delay_ms(100);
  mpu_write(0x1A, 0x03);
  mpu_write(0x1B, 0x00);
  mpu_write(0x1C, 0x00);

  delay_ms(1500);
  int32_t sy = 0, sz = 0, sg = 0, n = 0;
  for (int i = 0; i < 300; i++) {
    int16_t ay, az, gx;
    if (mpu_read(&ay, &az, &gx)) { sy += ay; sz += az; sg += gx; n++; }
    delay_ms(3);
  }
  int32_t gbias = 0, neutral = 0;
  if (n) { gbias = sg / n; neutral = atan2_cdeg(sy / n, sz / n); }

  int32_t roll = neutral;
  uint32_t tPrev = ccount(), tPrint = tPrev;

  for (;;) {
    int16_t ay, az, gx;
    if (!mpu_read(&ay, &az, &gx)) {
      motor_run(0, 10000);
#if DEBUG
      uart_puts("MPU read failed\r\n");
#endif
      continue;
    }

    uint32_t now = ccount();
    int32_t dt_us = (int32_t)((now - tPrev) / (CPU_HZ / 1000000UL));
    tPrev = now;
    if (dt_us > 20000) dt_us = 20000;

    int32_t rate = ((int32_t)gx - gbias) * 100 / 131;
    int32_t acc  = atan2_cdeg(ay, az);

    roll += rate * dt_us / 1000000;
    roll += wrap(acc - roll) / 50;
    roll = wrap(roll);
    int32_t rel = wrap(roll - neutral);

#if MODE_RATE
    int c = shape(rate, 3000, 20000);
#else
    int c = shape(rel, 2000, 6000);
#endif
    c *= DIR_SIGN;

#if DEBUG
    if ((now - tPrint) >= CPU_HZ / 10) {
      tPrint = now;
      uart_puts("rate="); uart_putint(rate);
      uart_puts(" rel=");  uart_putint(rel);
      uart_puts(" cmd=");  uart_putint(c);
      uart_puts("\r\n");
    }
#endif

    motor_run(c, 4000);   /* PWM runs for ~4 ms, replaces the old delay_ms(4) */
  }
}

/* ---------- startup ---------- */
extern uint32_t _sbss, _ebss;
void startup(void) {
  wdt_off();
  for (uint32_t *p = &_sbss; p < &_ebss; p++) *p = 0;
  app_main();
  for (;;) {}
}
