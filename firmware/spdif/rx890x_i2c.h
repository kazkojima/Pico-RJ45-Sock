/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef __RX890X_I2C_H__
#define __RX890X_I2C_H__

#include "system.h"

#if RTC_RX890X_EN
#include "hardware/gpio.h"

void rx890x_init(void);
bool rx890x_get_time(uint64_t *uxtime);
bool rx890x_set_time(uint64_t uxtime);
void rx890x_set_update_irq(gpio_irq_callback_t callback);
#endif // RTC_RX890X_EN
#endif
