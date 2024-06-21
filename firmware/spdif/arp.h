#ifndef __ARP_H__
#define __ARP_H__

#include <stdint.h>

// -------------------
// Preamble     7
// SFD          1
// Ether        14
// ARP Packet   46 (28 + padding 18)
// FCS          4
// -------------------
//              72
#define DEF_ARP_BUF_SIZE        (72)

#define DEF_ARPOPC_REQUEST	(0x0001)        // ARP Opcode : Request
#define DEF_ARPOPC_REPLY	(0x0002)        // ARP Opcode : Reply

void arp_init(void);
void arp_packet_gen_10base(uint32_t *buf, uint64_t dst_mac, uint32_t sender_ip, uint16_t opcode);

#endif //__ARP_H__
