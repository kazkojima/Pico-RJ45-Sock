#ifndef __SYSTEM_H__
#define __SYSTEM_H__

// Compile switch
#define UART_EBG_EN             (0)     // 有効にするとちょい重たい
#define FCS_DMA_EN              (0)     // FCSの計算にDMAを使用する
#define DEF_10BASET_FULL_EN     (1)     // Enable 10BASE-T Full Duplex


// RasPico Network settings
#define DEF_SYS_PICO_MAC        (0x123456789ABC)

#define DEF_SYS_PICO_IP1        (10)
#define DEF_SYS_PICO_IP2        (253)
#define DEF_SYS_PICO_IP3        (253)
#define DEF_SYS_PICO_IP4        (132)


// For UDP
#define DEF_SYS_UDP_DST_MAC     (0x6C4B909F321F)

#define DEF_SYS_UDP_DST_IP1     (10)
#define DEF_SYS_UDP_DST_IP2     (253)
#define DEF_SYS_UDP_DST_IP3     (253)
#define DEF_SYS_UDP_DST_IP4     (8)

// H/W PIN
#define DEF_SYS_HWPIN_DCDC_PS   (23)

// S/PDIF PIN
#define DEF_SYS_HWPIN_SPDIF          	(22)
#define DEF_SYS_HWPIN_SPDIF_ST0  (21)
#define DEF_SYS_HWPIN_SPDIF_ST1  (20)

// Compile switch for optional RTC
#define RTC_RX890X_EN	(1)
#endif //__SYSTEM_H__
