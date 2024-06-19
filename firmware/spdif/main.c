// Pico-10BASE-T S/PDIF Sample
//   Originally this is: 
/********************************************************
* Title    : Pico-10BASE-T VBAN Sample
* Date     : 2024/03/15
* Design   : kingyo
********************************************************/
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hwinit.h"
#include "udp.h"
#include "eth.h"
#include "system.h"

#include "spdif_rx.h"

#define DAC_ZERO 1
uint32_t tx_buf_udp[DEF_UDP_BUF_SIZE+1] = {0};
typedef struct { uint8_t b[4]; } se32_t;
se32_t sample_buffer[192];
int n_samples = 48*2; // Dummy
uint32_t sample_rate;
volatile static bool udp_setup_flg = false;
volatile static bool udp_cancel_flg = false;

inline void set_se32(se32_t *s, uint32_t x)
{
  s->b[0] = x >> 0;
  s->b[1] = x >> 8;
  s->b[2] = x >> 16;
  s->b[3] = x >> 24;
}

void spdif_rx_read(se32_t *samples, size_t sample_count)
{
  static bool mute_flag = true;
  static int soft_start;
  const int SOFT_START_COUNT = 8*1000; // 8s

  uint32_t fifo_count = spdif_rx_get_fifo_count();
  if (spdif_rx_get_state() == SPDIF_RX_STATE_STABLE) {
    if (mute_flag && fifo_count >= sample_count) {
      mute_flag = false;
      soft_start = SOFT_START_COUNT;
    }
  } else {
    mute_flag = true;
  }

  //printf("mute %d fifo %ld sc %d\n", mute_flag, fifo_count, sample_count);

  if (mute_flag || soft_start > 0) {
    for (int i = 0; i < sample_count / 2; i++) {
      set_se32(&samples[i], DAC_ZERO);
    }
    if (soft_start > 0) {
      soft_start--;
      // to avoid fifo overflow
      uint32_t* trash;
      spdif_rx_read_fifo(&trash, fifo_count);
    }
  } else {
    uint32_t total_count = sample_count;
    int i = 0;
    uint32_t read_count = 0;
    uint32_t* buff;
    while (read_count < total_count) {
      uint32_t get_count = spdif_rx_read_fifo(&buff, total_count - read_count);
      for (int j = 0; j < get_count / 2; j++) {
	//set_se32(&samples[2*i+0], ((buff[j*2+0] & 0x0ffffff0) << 4));
	set_se32(&samples[i], ((buff[j*2+1] & 0x0ffffff0) << 4));
	i++;
      }
      read_count += get_count;
    }
  }
}

void on_stable_func(spdif_rx_samp_freq_t samp_freq)
{
  // callback function should be returned as quick as possible
  udp_setup_flg = true;
  udp_cancel_flg = false;
  n_samples = (samp_freq/1000) * 2;
  sample_rate = samp_freq;
}

void on_lost_stable_func()
{
  // callback function should be returned as quick as possible
  udp_cancel_flg = true;
}

int main() {

    stdio_init_all();
    hw_init();

     spdif_rx_config_t config = {
      .data_pin = DEF_SYS_HWPIN_SPDIF,
      .pio_sm = PICO_SPDIF_RX_PIO,
      .dma_channel0 = 0,
      .dma_channel1 = 1,
      .alarm = 0,
      .flags = SPDIF_RX_FLAGS_ALL
    };

    spdif_rx_start(&config);
    spdif_rx_set_callback_on_stable(on_stable_func);
    spdif_rx_set_callback_on_lost_stable(on_lost_stable_func);

    eth_init();

    printf("[BOOT]\r\n");

    hw_start_led_blink();

#if 0
    while (!udp_setup_flg) {
      sleep_ms(10);
    }
#endif

    while (1) {
        eth_main();
	spdif_rx_read(sample_buffer, n_samples);
	udp_packet_gen_10base(tx_buf_udp, (uint8_t *)sample_buffer);
	eth_tx_data(tx_buf_udp, DEF_UDP_BUF_SIZE+1);
    }

    return 0;
}
