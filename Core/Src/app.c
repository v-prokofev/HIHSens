/*
 * app.c - HMP556: two Honeywell sensors -> UART
 *
 * Output line:
 * HMP556 ID yyyy:mm:dd hh:mm:ss.xxx hum1 t1 hum2 t2 cpu_temp [s1_ok s2_ok s1_rx s2_rx uart_rx clk_ok]
 */
#include "app.h"
#include "honeywell.h"
#include "uart.h"
#include "vpmenu.h"
#include "vpflash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEV_NAME      "HMP556"
#define FW_VERSION    "0.1"
#ifndef GIT_COMMIT
#define GIT_COMMIT    "unknown"
#endif

/* ---------------- settings in flash ---------------- */
#define SETTINGS_ADDR   0x0800FC00u        /* last page of 64K */
#define SETTINGS_MAGIC  0x484D5035u        /* "HMP5" */

typedef struct {
	uint32_t magic;
	uint32_t id;
	uint32_t baud;
	uint32_t freq_mhz;      /* output frequency, mHz: 10..10000 */
	uint32_t debug;
	uint32_t crc;
} settings_t;

static settings_t cfg;

static uint32_t cfg_crc(const settings_t *s)
{
	const uint32_t *w = (const uint32_t *)s;
	uint32_t c = 0x12345678u;
	for (unsigned i = 0; i < sizeof(settings_t) / 4 - 1; i++) c = (c ^ w[i]) * 16777619u;
	return c;
}

static void cfg_defaults(void)
{
	cfg.magic = SETTINGS_MAGIC;
	cfg.id = 1;
	cfg.baud = 115200;
	cfg.freq_mhz = 1000;
	cfg.debug = 0;
}

static void cfg_load(void)
{
	memcpy(&cfg, (const void *)SETTINGS_ADDR, sizeof(cfg));
	if (cfg.magic != SETTINGS_MAGIC || cfg.crc != cfg_crc(&cfg)) cfg_defaults();
}

static int cfg_save(void)
{
	cfg.magic = SETTINGS_MAGIC;
	cfg.crc = cfg_crc(&cfg);
	return Flash_Write_Data(SETTINGS_ADDR, (uint32_t *)&cfg, sizeof(cfg) / 4) == 0 ? 0 : -1;
}

/* ---------------- clock (ms since 2000-01-01) ---------------- */
static uint64_t clk_base_ms;     /* wall time = clk_base_ms + now_ms() (mod 2^64) */
static uint64_t clk_ext_ms;      /* extends 32-bit HAL tick */
static uint32_t clk_last_tick;
static uint8_t  clk_valid;

static uint64_t now_ms(void)
{
	uint32_t t = HAL_GetTick();
	clk_ext_ms += (uint32_t)(t - clk_last_tick);
	clk_last_tick = t;
	return clk_ext_ms;
}

static uint64_t clock_ms(void)
{
	return clk_base_ms + now_ms();
}

/* days since 2000-01-01 <-> civil date (H. Hinnant algorithms, shifted epoch) */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
	y -= m <= 2;
	int32_t era = (y >= 0 ? y : y - 399) / 400;
	uint32_t yoe = (uint32_t)(y - era * 400);
	uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int32_t)doe - 719468 - 10957;
}

static void civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d)
{
	z += 719468 + 10957;
	int32_t era = (z >= 0 ? z : z - 146096) / 146097;
	uint32_t doe = (uint32_t)(z - era * 146097);
	uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	uint32_t mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (int32_t)yoe + era * 400 + (*m <= 2);
}

/* ---------------- CPU temperature (ADC1 ch16, Vrefint ch17) ---------------- */
static void adc_init(void)
{
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;   /* 72/6 = 12 MHz */
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
	(void)RCC->APB2ENR;
	ADC1->SMPR1 |= (7u << 18) | (7u << 21);          /* 239.5 cycles for ch16, ch17 */
	ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | (7u << 17); /* SWSTART */
	HAL_Delay(2);
	ADC1->CR2 |= ADC_CR2_RSTCAL; while (ADC1->CR2 & ADC_CR2_RSTCAL) {}
	ADC1->CR2 |= ADC_CR2_CAL;    while (ADC1->CR2 & ADC_CR2_CAL) {}
}

static uint16_t adc_read(uint32_t ch)
{
	ADC1->SQR3 = ch;
	ADC1->SR &= ~ADC_SR_EOC;
	ADC1->CR2 |= ADC_CR2_ADON | ADC_CR2_SWSTART;
	uint32_t timeout = 50000;
	while (!(ADC1->SR & ADC_SR_EOC)) {
		if (--timeout == 0) return 0;
	}
	return (uint16_t)ADC1->DR;
}

static float cpu_temp(void)
{
	uint16_t vref = adc_read(17);
	uint16_t ts   = adc_read(16);
	if (vref == 0) return -1.0f;
	float vdd = 1.20f * 4095.0f / (float)vref;
	float v   = (float)ts * vdd / 4095.0f;
	return (1.43f - v) / 0.0043f + 25.0f;
}

/* ---------------- state ---------------- */
typedef struct {
	hw_dev_t  dev;
	hw_data_t data;
	uint8_t   ok;
	uint32_t  rx_cnt;
	uint32_t  retry_tick;
} sensor_t;

static sensor_t s[2];
static uint32_t uart_rx_cnt;
static char line[96];
static uint8_t line_len;
static char out[256];

static void puts_ln(const char *str) { uart_send(str); uart_send("\r\n"); }

static char *fmt_fixed(char *p, float v, int dec)
{
	if (v != v) { /* NaN */
		*p++ = '-'; *p++ = '1';
		return p;
	}
	if (v < 0) { *p++ = '-'; v = -v; }
	if (v > 99999.0f) v = 99999.0f;
	uint32_t mul = (dec == 1) ? 10 : 100;
	uint32_t x = (uint32_t)(v * mul + 0.5f);
	return p + sprintf(p, dec == 1 ? "%lu.%01lu" : "%lu.%02lu",
			(unsigned long)(x / mul), (unsigned long)(x % mul));
}

static void sensor_poll(sensor_t *x)
{
	if (!x->dev.pending) {
		if (HAL_GetTick() - x->retry_tick < 200 && !x->ok) return;
		if (hw_request(&x->dev) != HW_OK) { x->ok = 0; x->retry_tick = HAL_GetTick(); }
		return;
	}
	hw_data_t d;
	switch (hw_read(&x->dev, &d)) {
	case HW_OK:    x->data = d; x->ok = 1; x->rx_cnt++; break;
	case HW_STALE: break;
	case HW_BUSY:  break;
	default:       x->ok = 0; x->retry_tick = HAL_GetTick(); break;
	}
}

static void print_line(void)
{
	uint64_t ms = clock_ms();
	uint32_t msd = (uint32_t)(ms % 86400000ull);
	int32_t y; uint32_t mo, d;
	civil_from_days((int32_t)(ms / 86400000ull), &y, &mo, &d);

	char *p = out;
	p += sprintf(p, "%s %03lu %04ld:%02lu:%02lu %02lu:%02lu:%02lu.%03lu",
			DEV_NAME, (unsigned long)cfg.id, (long)y, (unsigned long)mo, (unsigned long)d,
			(unsigned long)(msd / 3600000), (unsigned long)(msd / 60000 % 60),
			(unsigned long)(msd / 1000 % 60), (unsigned long)(msd % 1000));
	for (int i = 0; i < 2; i++) {
		if (s[i].ok) {
			*p++ = ' '; p = fmt_fixed(p, s[i].data.hum, 2);
			*p++ = ' '; p = fmt_fixed(p, s[i].data.temp, 2);
		} else {
			p += sprintf(p, " -1 -1");
		}
	}
	*p++ = ' '; p = fmt_fixed(p, cpu_temp(), 1);
	if (cfg.debug) {
		p += sprintf(p, " [%u %u %lu %lu %lu %u]", s[0].ok, s[1].ok,
				(unsigned long)s[0].rx_cnt, (unsigned long)s[1].rx_cnt,
				(unsigned long)uart_rx_cnt, clk_valid);
	}
	strcpy(p, "\r\n");
	uart_send(out);
}

/* ---------------- menu handlers ---------------- */
static int cmd_help(float a, float b)
{
	puts_ln("commands:");
	puts_ln("  help");
	puts_ln("  info");
	puts_ln("  scan                 I2C bus address scan");
	puts_ln("  set_id ID            0..999");
	puts_ln("  set_br BAUDRATE      1200..921600");
	puts_ln("  freq HZ              0.01..10");
	puts_ln("  debug 0|1");
	puts_ln("  set_time yyyy-mm-dd hh:mm:ss[.xxx]");
	return 0;
}

static void i2c_scan_bus(I2C_HandleTypeDef *hi2c, const char *name)
{
	char msg[96];
	int scl_pin = (hi2c->Instance == I2C1) ? 6 : 10;
	int sda_pin = (hi2c->Instance == I2C1) ? 7 : 11;
	uint8_t scl_lvl = (GPIOB->IDR >> scl_pin) & 1;
	uint8_t sda_lvl = (GPIOB->IDR >> sda_pin) & 1;

	sprintf(msg, "%s (PB%d/PB%d) lines: SCL=%u SDA=%u | scan:",
			name, scl_pin, sda_pin, scl_lvl, sda_lvl);
	uart_send(msg);

	i2c_bus_unlock(hi2c);

	int found = 0;
	for (uint16_t addr = 1; addr < 128; addr++) {
		HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(hi2c, (addr << 1), 1, 10);
		if (st == HAL_OK) {
			sprintf(msg, " 0x%02X", addr);
			uart_send(msg);
			found++;
		} else if (st == HAL_TIMEOUT || st == HAL_BUSY) {
			/* Reset bus if locked up by errata or line disturbance */
			i2c_bus_unlock(hi2c);
		}
	}
	if (!found) {
		uart_send(" none found");
	}
	puts_ln("");
}

static int cmd_scan(float a, float b)
{
	i2c_scan_bus(s[0].dev.hi2c, "I2C1");
	i2c_scan_bus(s[1].dev.hi2c, "I2C2");
	return 0;
}

static int cmd_info(float a, float b)
{
	puts_ln("VNIIM Humidity sensor (" DEV_NAME ") ver " FW_VERSION
			" build " __DATE__ " " __TIME__ " commit " GIT_COMMIT);
	return 0;
}

static int cmd_set_id(float a, float b)
{
	if (vpmenu_param_count() < 1 || a < 0 || a > 999) return -1;
	cfg.id = (uint32_t)(a + 0.5f);
	return cfg_save();
}

static uint32_t pending_baud;

static int cmd_set_br(float a, float b)
{
	if (vpmenu_param_count() < 1 || a < 1200 || a > 921600) return -1;
	cfg.baud = (uint32_t)(a + 0.5f);
	if (cfg_save()) return -1;
	pending_baud = cfg.baud;    /* applied after "OK" is sent */
	return 0;
}

static int cmd_freq(float a, float b)
{
	if (vpmenu_param_count() < 1 || a < 0.0099f || a > 10.0f) return -1;
	cfg.freq_mhz = (uint32_t)(a * 1000.0f + 0.5f);
	return cfg_save();
}

static int cmd_debug(float a, float b)
{
	if (vpmenu_param_count() < 1) return -1;
	cfg.debug = a != 0;
	return cfg_save();
}

static int cmd_set_time(float a, float b)
{
	/* parse integers straight from the line: Y M D h m s [ms] */
	long v[7] = {0};
	int n = 0;
	char *p = line + strlen("set_time");
	while (*p && n < 7) {
		if (*p >= '0' && *p <= '9') v[n++] = strtol(p, &p, 10);
		else p++;
	}
	if (n < 6 || v[0] < 2000 || v[0] > 2099 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31 ||
		v[3] > 23 || v[4] > 59 || v[5] > 59 || v[6] > 999) return -1;

	uint64_t ms = (uint64_t)days_from_civil(v[0], v[1], v[2]) * 86400000ull +
			((uint64_t)v[3] * 3600 + v[4] * 60 + v[5]) * 1000 + v[6];
	clk_base_ms = ms - now_ms();
	clk_valid = 1;
	return 0;
}

static void handle_line(void)
{
	uart_rx_cnt++;
	int r = vpmenu_parse_str();
	if (r == 0) {
		puts_ln("OK");
	} else {
		puts_ln("ERR: unknown command or invalid arguments. Type 'help' for available commands");
	}
	if (pending_baud) { uart_set_baud(pending_baud); pending_baud = 0; }
}

/* ---------------- public ---------------- */
/* 4.7k I2C pull-ups are soldered to neighbour pins on the headers:
 * PB0, PB1, PB5, PB8, PB9, PB12 are driven high (push-pull 2 MHz, 3.3V)
 * so that any neighbor pin used for pull-up is powered. Call before I2C init. */
void app_pullups_init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
	(void)RCC->APB2ENR;

	/* Drive pins HIGH first */
	GPIOB->BSRR = (1u << 0) | (1u << 1) | (1u << 5) | (1u << 8) | (1u << 9) | (1u << 12);

	/* PB0 (bits 0..3), PB1 (bits 4..7), PB5 (bits 20..23) as Out PP 2 MHz (0x2) */
	GPIOB->CRL = (GPIOB->CRL & ~((0xFu << 0) | (0xFu << 4) | (0xFu << 20)))
	           | (0x2u << 0) | (0x2u << 4) | (0x2u << 20);

	/* PB8 (bits 0..3), PB9 (bits 4..7), PB12 (bits 16..19) as Out PP 2 MHz (0x2) */
	GPIOB->CRH = (GPIOB->CRH & ~((0xFu << 0) | (0xFu << 4) | (0xFu << 16)))
	           | (0x2u << 0) | (0x2u << 4) | (0x2u << 16);
}

void app_clock_init(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clk = {0};

	osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	osc.HSEState = RCC_HSE_ON;
	osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
	osc.PLL.PLLState = RCC_PLL_ON;
	osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	osc.PLL.PLLMUL = RCC_PLL_MUL9;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK) return;     /* no crystal: stay on HSI 8 MHz */

	clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
	clk.APB1CLKDivider = RCC_HCLK_DIV2;
	clk.APB2CLKDivider = RCC_HCLK_DIV1;
	HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

void app_init(I2C_HandleTypeDef *i2c_s1, I2C_HandleTypeDef *i2c_s2)
{
	/* LED PC13, push-pull 2 MHz */
	RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
	(void)RCC->APB2ENR;
	GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);

	clk_last_tick = HAL_GetTick();
	clk_ext_ms = 0;
	clk_base_ms = 0;            /* 2000-01-01 00:00:00.000 */
	clk_valid = 0;

	cfg_load();
	uart_init(cfg.baud);
	adc_init();

	hw_init(&s[0].dev, i2c_s1);
	hw_init(&s[1].dev, i2c_s2);

	i2c_bus_unlock(i2c_s1);
	i2c_bus_unlock(i2c_s2);

	vpmenu_init((uint8_t *)line, sizeof(line));
	vpmenu_register_handler("help", cmd_help);
	vpmenu_register_handler("info", cmd_info);
	vpmenu_register_handler("scan", cmd_scan);
	vpmenu_register_handler("set_id", cmd_set_id);
	vpmenu_register_handler("set_br", cmd_set_br);
	vpmenu_register_handler("freq", cmd_freq);
	vpmenu_register_handler("debug", cmd_debug);
	vpmenu_register_handler("set_time", cmd_set_time);

	cmd_info(0, 0);
	cmd_scan(0, 0);
}

void app_loop(void)
{
	static uint32_t led_tick;
	static uint64_t next_out;

	uint32_t t = HAL_GetTick();
	if (t - led_tick >= 500) { led_tick = t; GPIOC->ODR ^= (1u << 13); }

	sensor_poll(&s[0]);
	sensor_poll(&s[1]);

	int c;
	while ((c = uart_getc()) >= 0) {
		if (c == '\r' || c == '\n') {
			if (line_len) { line[line_len] = 0; handle_line(); line_len = 0; }
		} else if (line_len < sizeof(line) - 1) {
			line[line_len++] = (char)c;
		}
	}

	uint64_t now = now_ms();
	uint32_t freq = (cfg.freq_mhz >= 10 && cfg.freq_mhz <= 10000) ? cfg.freq_mhz : 1000;
	uint32_t period = 1000000u / freq;
	if (now >= next_out) {
		next_out = (now - next_out > period) ? now + period : next_out + period;
		print_line();
	}
}
