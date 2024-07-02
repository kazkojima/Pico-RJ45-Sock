/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
// Pico-10BASE-T S/PDIF Sample
//   Original is:
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
#include "arp.h"
#include "eth.h"
#include "system.h"

#include "spdif_rx.h"
#include "rx890x_i2c.h"

#define CH1_ONLY  (0)
#define CH2_ONLY  (1)
#define CH_BOTH    (0)

#define SAMPLE_RATE_PER_MS  96
#define UDP_PAYLOAD_SIZE    (96*4)

#if (CH_BOTH && UDP_PAYLOAD_SIZE != SAMPLE_RATE_PER_MS*4*2)
#error "UDP_PAYLOAD_SIZE should be SAMPLE_RATE_PER_MS*4*2 for stereo"
#endif
#if (!CH_BOTH && UDP_PAYLOAD_SIZE != SAMPLE_RATE_PER_MS*4)
#error "UDP_PAYLOAD_SIZE should be SAMPLE_RATE_PER_MS*4 for mono"
#endif

static uint32_t tx_buf_arp[DEF_ARP_BUF_SIZE+1] = {0};
static uint32_t tx_buf_udp[ETHER_BUF_SIZE(MAX_UDP_PAYLOAD_SIZE)+1] = {0};

#define DAC_ZERO 1
typedef struct { uint8_t b[4]; } se32_t;
se32_t sample_buffer[192*2];
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

const int SOFT_START_COUNT = 8*1000; // 8s

void spdif_rx_read(se32_t *samples, size_t sample_count)
{
    static bool mute_flag = true;
    static int soft_start;

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
    //printf("state %d\n", spdif_rx_get_state());

    if (mute_flag || soft_start > 0) {
        for (int i = 0; i < sample_count / 2; i++) {
            if (CH_BOTH) {
                set_se32(&samples[2*i+0], DAC_ZERO);
                set_se32(&samples[2*i+1], DAC_ZERO);
            } else {
                set_se32(&samples[i], DAC_ZERO);
            }
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
                if (CH_BOTH) {
                    set_se32(&samples[2*i+0], ((buff[j*2+0] & 0x0ffffff0) << 4));
                    set_se32(&samples[2*i+1], ((buff[j*2+1] & 0x0ffffff0) << 4));
                } else if (CH1_ONLY) {
                    set_se32(&samples[i], ((buff[j*2+0] & 0x0ffffff0) << 4));
                } else { // CH2_ONLY
                    set_se32(&samples[i], ((buff[j*2+1] & 0x0ffffff0) << 4));
                }
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
    //gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, 0);
    //gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, 0);
}

#if RTC_RX890X_EN
void rtc_1ps_callback(uint gpio, uint32_t events) {
    printf("RTC updated %d %x\r\n", gpio, events);
}
#endif

int main() {

    stdio_init_all();
    hw_init();

    printf("[BOOT]\r\n");

    gpio_init(DEF_SYS_HWPIN_SPDIF_ST0);
    gpio_set_dir(DEF_SYS_HWPIN_SPDIF_ST0, GPIO_OUT);
    gpio_init(DEF_SYS_HWPIN_SPDIF_ST1);
    gpio_set_dir(DEF_SYS_HWPIN_SPDIF_ST1, GPIO_OUT);
    gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, 0);
    gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, 0);

    spdif_rx_config_t config = {
        .data_pin = DEF_SYS_HWPIN_SPDIF,
        .pio_sm = PICO_SPDIF_RX_PIO,
        .dma_channel0 = 0,
        .dma_channel1 = 1,
        .alarm = 0,
        .flags = SPDIF_RX_FLAGS_ALL
    };

#if 0
    spdif_rx_start(&config);
    spdif_rx_set_callback_on_stable(on_stable_func);
    spdif_rx_set_callback_on_lost_stable(on_lost_stable_func);
#endif

#if RTC_RX890X_EN
    uint64_t uxtime;
    //rx890x_set_update_irq(rtc_1ps_callback);
    rx890x_init();
    rx890x_get_time(&uxtime);
    rx890x_set_time(uxtime);
    rx890x_get_time(&uxtime);
#endif

#if 0
    while (!udp_setup_flg) {
        sleep_ms(10);
        uint state = spdif_rx_get_state();
        gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, state & 1);
        gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, (state >> 1) & 1);
    }
#endif
    //printf("rate %d\r\n", sample_rate);
    //printf("state %d\n", spdif_rx_get_state());
    uint state = spdif_rx_get_state();
    gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, state & 1);
    gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, (state >> 1) & 1);

    eth_init();
    hw_start_led_blink();

    printf("[ARP request]\r\n");
    uint32_t udp_dst_ip = (DEF_SYS_UDP_DST_IP1 << 24) + (DEF_SYS_UDP_DST_IP2 << 16) + (DEF_SYS_UDP_DST_IP3 << 8) + (DEF_SYS_UDP_DST_IP4 << 0);
    arp_packet_gen_10base(tx_buf_arp, 0xFFFFFFFFFFFFUL, udp_dst_ip, DEF_ARPOPC_REQUEST);
    // ??? Send dummy
    eth_tx_data(tx_buf_arp, DEF_ARP_BUF_SIZE+1);

    eth_main();
    eth_tx_data(tx_buf_arp, DEF_ARP_BUF_SIZE+1);

    uint32_t time = time_us_32();
    int arp_retry = 5-1;
    uint64_t udp_dst_eth = 0;

    while(1) {
        eth_main();
        if (eth_arp_resolve(udp_dst_ip, &udp_dst_eth) == true) {
            printf("[ARP reply] %d.%d.%d.%d is-at ", (udp_dst_ip >> 24), (udp_dst_ip >> 16) & 0xFF, (udp_dst_ip >> 8) & 0xFF, (udp_dst_ip & 0xFF));
            printf("%02x:%02x:%02x:%02x:%02x:%02x\r\n",   (uint8_t)(udp_dst_eth >> 40), (uint8_t)(udp_dst_eth >> 32), (uint8_t)(udp_dst_eth >> 24), (uint8_t)(udp_dst_eth >> 16), (uint8_t)(udp_dst_eth >> 8), (uint8_t)(udp_dst_eth));
            break;
        }
        if (time_us_32() - time > 50000) {
            time = time_us_32();
            // Retry arp request upto 5 times
            if (--arp_retry <= 0)
                break;
            printf("[ARP request retry]\r\n");
            eth_tx_data(tx_buf_arp, DEF_ARP_BUF_SIZE+1);
        }
    }

    // last resort
    if (udp_dst_eth == 0) {
        printf("fall back to the default udp dst mac\r\n");
        udp_dst_eth = DEF_SYS_UDP_DST_MAC;
    }

    while (1) {
        eth_main();
        spdif_rx_read(sample_buffer, n_samples);
        //printf("data %08x\n", sample_buffer[0]);
        udp_packet_gen_10base(tx_buf_udp, (uint8_t *)sample_buffer, UDP_PAYLOAD_SIZE, udp_dst_eth);
        eth_tx_data(tx_buf_udp, ETHER_BUF_SIZE(UDP_PAYLOAD_SIZE)+1);
    }

    return 0;
}
