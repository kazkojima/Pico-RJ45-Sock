/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
// Pico-10BASE-T S/PDIF Sample
//   Original is:
/********************************************************
 * Title    : Pico-10BASE-T VBAN Sample
 * Date     : 2024/03/15
 * Design   : kingyo
 ********************************************************/
#include <stdio.h>
#include <string.h>
#include <math.h>
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
#define DATA_PAYLOAD_SIZE    (96*4)
#define HEADER_WSIZE 4

#if (CH_BOTH && DATA_PAYLOAD_SIZE != SAMPLE_RATE_PER_MS*4*2)
#error "DATA_PAYLOAD_SIZE should be SAMPLE_RATE_PER_MS*4*2 for stereo"
#endif
#if (!CH_BOTH && DATA_PAYLOAD_SIZE != SAMPLE_RATE_PER_MS*4)
#error "DATA_PAYLOAD_SIZE should be SAMPLE_RATE_PER_MS*4 for mono"
#endif

static uint32_t tx_buf_arp[DEF_ARP_BUF_SIZE+1] = {0};
static uint32_t tx_buf_udp[ETHER_BUF_SIZE(MAX_UDP_PAYLOAD_SIZE)+1] = {0};

#define DAC_ZERO 1
// signal endian is little endian
typedef struct { uint8_t b[4]; } se32_t;
se32_t sample_buffer[HEADER_WSIZE + 192*2];
int n_samples = 48*2; // Dummy
uint32_t sample_rate;
volatile static bool spdif_setup_flg = false;
volatile static bool spdif_cancel_flg = false;

inline void set_se32(se32_t *s, uint32_t x)
{
    s->b[0] = x >> 0;
    s->b[1] = x >> 8;
    s->b[2] = x >> 16;
    s->b[3] = x >> 24;
}

inline void set_be32(se32_t *s, uint32_t x)
{
    s->b[0] = x >> 24;
    s->b[1] = x >> 16;
    s->b[2] = x >> 8;
    s->b[3] = x >> 0;
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
    spdif_setup_flg = true;
    spdif_cancel_flg = false;
    n_samples = (samp_freq/1000) * 2;
    sample_rate = samp_freq;
}

void on_lost_stable_func()
{
    // callback function should be returned as quick as possible
    spdif_cancel_flg = true;
    //gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, 0);
    //gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, 0);
}

// NTP test
static uint8_t ntp_buffer[48];
static uint32_t ntp_epoch_sec = 0;
static volatile uint64_t local_epoch_64 = 0;
static uint32_t ntp_time_us = 0;
static uint32_t rtc_last_us = 0;
static uint32_t rtc_time_offset = 0;

static uint32_t subsec_us_to_32(uint32_t us)
{
    return (uint32_t)floor((us % 1000000)*4294.967295);
}

static void get_local_time(uint32_t *secp, uint32_t *subsecp)
{
    uint32_t adj32 = subsec_us_to_32(time_us_32() - rtc_last_us);
    uint64_t ep64 = local_epoch_64 + adj32;
    if (secp)
        *secp = (ep64 >> 32) & 0xFFFFFFFF;
    if (subsecp)
        *subsecp = ep64 & 0xFFFFFFFF;
}

void udp_rx_test(void *p)
{
    uint16_t *p16 = (uint16_t *)p;
    //printf("sp:%d dp:%d sl:%d csum %04x\r\n", p16[0], p16[3], p16[2], p16[5]);
    if (p16[0] != 123)
        return;
    //printf("guess NTP sp:%d dp:%d sl:%d csum %04x\r\n", p16[0], p16[3], p16[2], p16[5]);
    int len = p16[2] - 8;
    if (len <= 0)
        return;
    if(len != 48)
        return;
    uint8_t *p8 = p + 8;
#if 0
    for (int i = 0; i < len; i++) {
        printf("%02x ", p8[(i+2) ^ 3]);
        if (i % 8 == 7)
            printf("\r\n");
    }
    printf("\r\n");
#endif
    uint32_t sec = (p8[(40+2)^3] << 24)|(p8[(41+2)^3] << 16)|(p8[(42+2)^3] << 8)|(p8[(43+2)^3] << 0);
    uint32_t subsec = (p8[(44+2)^3] << 24)|(p8[(45+2)^3] << 16)|(p8[(46+2)^3] << 8)|(p8[(47+2)^3] << 0);
    sec -= 2208988800; // minus NTP time sec at 1/1/1970 00:00:00
    //printf("[NTP reply] unix epoch %d subsec %08x\r\n", sec, subsec);
    ntp_epoch_sec = sec;
    ntp_time_us = time_us_32();
    local_epoch_64 = (((uint64_t)sec) << 32) | subsec;
}


#if RTC_RX890X_EN
void rtc_1ps_callback(uint gpio, uint32_t events) {
    //printf("RTC updated %d %x\r\n", gpio, events);
    rtc_last_us = time_us_32();
    if (local_epoch_64) {
        if (rtc_time_offset == 0) {
            // Do it only at the first tick of 1ps.
            // 0x100000000 - (us erapsed from NTP epoch set).
            rtc_time_offset = ntp_time_us - rtc_last_us;
        }
        local_epoch_64 += (uint32_t) rtc_time_offset;
    }
}
#endif

// VITA 49.0
// typedef struct {
//    unsigned type:4, c:1, t:1, rev:2, tsi:2, tsf:2, count:4, size:16;
// } vhbit_t;
// typedef union {
//      uint32_t vh;
//      vhbit_t vhbit;
//  } vheader_t;
// vheader_t vheader;
const uint32_t vheader = (0<<28)|(0<<27)|(0<<26)|(0<<24)|(1<<22)|(1<<20)|(0<<16)|(4 + SAMPLE_RATE_PER_MS);
#define VITA49_HEADER_SIZE (HEADER_WSIZE * sizeof(uint32_t))
#define UDP_PAYLOAD_SIZE (VITA49_HEADER_SIZE+DATA_PAYLOAD_SIZE)

int main() {
    bool rtc_valid;

    eth_set_udp_rx_callback(udp_rx_test);

    stdio_init_all();
    hw_init();
    eth_init();
#if  RTC_RX890X_EN
    rx890x_set_update_irq(rtc_1ps_callback);
    rtc_valid = rx890x_init();
#endif

    printf("[BOOT]\r\n");

    hw_start_led_blink();

#if  RTC_RX890X_EN
    uint32_t rtc_epoch = 0;
    if (rtc_valid) {
        rx890x_get_time(&rtc_epoch);
        printf("[RTC] get time %d\r\n", rtc_epoch);
    } else {
        printf("[RTC] data loss detected\r\n");
    }
#endif

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

    printf("[NTP request]\r\n");
    memset(ntp_buffer, 0x02, sizeof(ntp_buffer));
    ntp_buffer[0] = 0x63;

    eth_main();
    //spdif_rx_read(&sample_buffer[HEADER_WSIZE], n_samples);
    udp_packet_gen_10base(tx_buf_udp, (uint8_t *)ntp_buffer, sizeof(ntp_buffer), udp_dst_ip, 123, udp_dst_eth);
    eth_tx_data(tx_buf_udp, ETHER_BUF_SIZE(sizeof(ntp_buffer))+1);

    time = time_us_32();
    while(1) {
        eth_main();
        if (ntp_epoch_sec) {
            printf("[NTP reply epoch] %d\r\n", ntp_epoch_sec);
            break;
        }
        if (time_us_32() - time > 100000) {
            break;
        }
    }
    if (!rtc_valid && ntp_epoch_sec) {
        rx890x_set_time(ntp_epoch_sec);
        rx890x_clear_flag();
    }

#if 1
    // Initialize S/PDIF status LEDs
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

    spdif_rx_set_callback_on_stable(on_stable_func);
    spdif_rx_set_callback_on_lost_stable(on_lost_stable_func);
    spdif_rx_start(&config);

    int spdif_wait = 20; // 2s
    time = time_us_32();
    while (!spdif_setup_flg) {
        eth_main();
        if (time_us_32() - time > 100000) {
            if (--spdif_wait < 0)
                break;
            uint state = spdif_rx_get_state();
            gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, state & 1);
            gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, (state >> 1) & 1);
            time = time_us_32();
       }
    }
    if (!spdif_setup_flg) {
        printf("[S/PDIF] no stable signal detected\r\n");
        spdif_rx_end();
    } else {
        //printf("rate %d\r\n", sample_rate);
        //printf("state %d\n", spdif_rx_get_state());
        uint state = spdif_rx_get_state();
        gpio_put(DEF_SYS_HWPIN_SPDIF_ST0, state & 1);
        gpio_put(DEF_SYS_HWPIN_SPDIF_ST1, (state >> 1) & 1);
    }
#endif

    uint32_t ts_sec, ts_subsec;
    time = time_us_32();

    // send data packets
    while (1) {
        eth_main();
        spdif_rx_read(&sample_buffer[HEADER_WSIZE], n_samples);
        //printf("n %d\n", n_samples);
        //printf("data %08x\n", sample_buffer[0]);
        get_local_time(&ts_sec, &ts_subsec);
        if (0&&time_us_32() - time > 100000) {
            printf("sec %d subsec %08x\n",  ts_sec, ts_subsec);
           time = time_us_32();
           uint8_t *p=(uint8_t *)&sample_buffer[HEADER_WSIZE];
           for (int i=0; i < 24;i++) {
               printf("%02x: %02x %02x %02x %02x  %02x %02x %02x %02x ", i, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
               p += 8;
               printf("%02x %02x %02x %02x  %02x %02x %02x %02x\n", i, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
               p += 8;
           }
        }
        if (spdif_setup_flg) {
            set_be32(&sample_buffer[0], vheader);
            set_be32(&sample_buffer[1], ts_sec);
            set_be32(&sample_buffer[2], ts_subsec);
            set_be32(&sample_buffer[3], 0);
            udp_packet_gen_10base(tx_buf_udp, ((uint8_t *)sample_buffer), UDP_PAYLOAD_SIZE, udp_dst_ip, DEF_UDP_DST_PORTNUM, udp_dst_eth);
            eth_tx_data(tx_buf_udp, ETHER_BUF_SIZE(UDP_PAYLOAD_SIZE)+1);
        }
    }

    return 0;
}
