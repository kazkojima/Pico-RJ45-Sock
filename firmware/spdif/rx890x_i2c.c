/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "rx890x_i2c.h"

#if RTC_RX890X_EN
#include <stdio.h>

#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define DEF_SYS_RTC_INT_PIN  (7)

#define DEF_SYS_I2C_SDA_PIN  (8)
#define DEF_SYS_I2C_SCL_PIN  (9)

#define RX890X_INT_PIN  (7)

#define BIT(x) (1<<(x))

// RX890X Register definitions
#define RX890X_REG_SEC			0x00
#define RX890X_REG_MIN			0x01
#define RX890X_REG_HOUR		0x02
#define RX890X_REG_WDAY		0x03
#define RX890X_REG_MDAY		0x04
#define RX890X_REG_MONTH	0x05
#define RX890X_REG_YEAR		0x06
#define RX890X_REG_RAM		0x07

#define RX890X_REG_ALMIN		0x08
#define RX890X_REG_ALHOUR	0x09
#define RX890X_REG_ALWDAY	0x0A
#define RX890X_REG_EXT			0x0D
#define RX890X_REG_FLAG		0x0E
#define RX890X_REG_CTRL		0x0F
#define RX890X_REG_TEMP		0x17
#define RX890X_REG_BKUP		0x18
#define RX890X_REG_TCNT0	0x1B
#define RX890X_REG_TCNT1	0x1C

#define RX890X_REG_OSCOFS	0x2C

#define RX890X_EXT_WADA		BIT(6)

#define RX890X_FLAG_V1F		BIT(0)
#define RX890X_FLAG_V2F		BIT(1)
#define RX890X_FLAG_AF			BIT(3)
#define RX890X_FLAG_TF			BIT(4)
#define RX890X_FLAG_UF			BIT(5)

#define RX890X_CTRL_RESET	BIT(0)

#define RX890X_CTRL_EIE			BIT(2)
#define RX890X_CTRL_AIE			BIT(3)
#define RX890X_CTRL_TIE			BIT(4)
#define RX890X_CTRL_UIE		BIT(5)

#define RX890X_CTRL_CSEL		0xc0

#define RX890X_FLAG_SWOFF	BIT(2)
#define RX890X_FLAG_VDETOFF		BIT(3)

#define RX890X_ADDR				0x32

static int rx890x_read_regs(int regnum, uint length, uint8_t *values)
{
    uint8_t val = regnum;
    i2c_write_blocking(i2c0, RX890X_ADDR, &val, 1, true);
    i2c_read_blocking(i2c0, RX890X_ADDR, values, length, false);
    printf("rx8901: read reg %d -> %02x\r\n", length, values[0]);
     return 0;
}

static void rx890x_reset(void)
{
    uint8_t ctrl;
    uint8_t buf[2];
    rx890x_read_regs(RX890X_REG_CTRL, 1, &ctrl);
    ctrl |= RX890X_CTRL_RESET;
    buf[0] = RX890X_REG_CTRL;
    buf[1] = ctrl;
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);
}

void rx890x_init(void)
{
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(DEF_SYS_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DEF_SYS_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DEF_SYS_I2C_SDA_PIN);
    gpio_pull_up(DEF_SYS_I2C_SCL_PIN);

    sleep_ms(10);

    uint8_t buf[2];
    buf[0] = RX890X_REG_EXT;
    buf[1] = (2<<2); // FD=2
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);
#if 0
    buf[0] = RX890X_REG_FLAG;
    buf[1] = 0x00;
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);
#endif
    buf[0] = RX890X_REG_CTRL;
    buf[1] = (1 << 6)|(1<<5); // CSEL=1, UIE=1
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);
}

// days for Jan 1 1970
#define EPOC_TIME 719163
#define bcd2bin(x) (((x) & 0x0f) + ((x) >> 4) * 10)
#define bin2bcd(x) ((((x) / 10) << 4) + (x) % 10)

bool rx890x_get_time(uint64_t *uxtime)
{
    uint8_t date[7];
    rx890x_read_regs(RX890X_REG_FLAG, 1, date);
    printf("rx8901: flag %02x\r\n", date[0]);
    rx890x_read_regs(RX890X_REG_SEC, 7, date);
    printf("rx8901: read %02x %02x %02x %02x %02x %02x %02x\r\n", date[0], date[1], date[2], date[3], date[4], date[5], date[6]);
    if (uxtime) {
        uint second = bcd2bin(date[RX890X_REG_SEC] & 0x7f);
        uint minute = bcd2bin(date[RX890X_REG_MIN] & 0x7f);
        uint hour = bcd2bin(date[RX890X_REG_HOUR] & 0x3f);
        uint day = bcd2bin(date[RX890X_REG_MDAY] & 0x3f);
        uint month = bcd2bin(date[RX890X_REG_MONTH] & 0x1f);
        uint year  = bcd2bin(date[RX890X_REG_YEAR]) + 2000;
        if (month == 1 || month == 2) {
            month += 12;
            year -= 1;
        }
        *uxtime = ((365 * year + (year / 4) - (year / 100) + (year / 400) + (306 * (month + 1) / 10) - 428 + day) -  EPOC_TIME) * 86400 + (hour * 3600) + (minute * 60) + second;
    }

    return true;
}

bool rx890x_set_time(uint64_t uxtime)
{
    uint8_t ctrl;
    uint8_t buf[2];

    rx890x_read_regs(RX890X_REG_CTRL, 1, &ctrl);
    ctrl |= RX890X_CTRL_RESET;
    buf[0] = RX890X_REG_CTRL;
    buf[1] = ctrl;
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);

    uint8_t current_val[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 7; i++) {
        buf[0] = RX890X_REG_SEC + i;
        buf[1] = current_val[i];
        i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);
    }

    rx890x_read_regs(RX890X_REG_CTRL, 1, &ctrl);
    ctrl &= ~RX890X_CTRL_RESET;
    buf[0] = RX890X_REG_CTRL;
    buf[1] = ctrl;
    i2c_write_blocking(i2c0, RX890X_ADDR, buf, 2, false);

    return true;
}

void rx890x_set_update_irq(gpio_irq_callback_t callback)
{
    gpio_set_irq_enabled_with_callback(DEF_SYS_RTC_INT_PIN, GPIO_IRQ_EDGE_FALL, true, callback);
}
#endif // RTC_RX890X_EN
