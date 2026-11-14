/*
 * CONNSYS (Wi-Fi/Bluetooth): power, firmware, the MCU patch (PDA), BTIF, STP/WMT, connsys_sw_init;
 * Bluetooth's function on, HCI over STP channel 0, the kernel control com.apple.uart.sk.bluetooth
 * and the answers bluetoothd needs changed.
 * License: APSL 2.0
 */
#ifndef _G510_CONNSYS_H_
#define _G510_CONNSYS_H_

void g510_connsys_init(void);

#endif /* _G510_CONNSYS_H_ */