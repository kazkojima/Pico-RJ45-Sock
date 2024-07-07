#ifndef __ETH_H__
#define __ETH_H__

#include <stdint.h>

void eth_init(void);
uint32_t eth_main(void);
void eth_tx_data(uint32_t *buf, uint32_t count);
bool eth_arp_resolve(uint32_t ip, uint64_t *ethp) ;
void eth_set_udp_rx_callback(void (*f)(void *));

#endif //__ETH_H__
