//
// FC Cartridge bus simulator "tuna"
//
// Copyrighte (C) 2021 Norix (NX labs)
//
// License: GPL2 (see gpl-2.0.txt)
//
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "bsp/board.h"

#include "usb_drv.h"
#include "bus_sim.h"
#include "flash_memory.h"

int main()
{
	stdio_init_all();
	board_init();
	tusb_init();

	printf ("FC bus simulator \"tuna\"\n");

	multicore_launch_core1(bussim_process);
	uint32_t	data = multicore_fifo_pop_blocking();
	if (data != 0x2A032C02) {
		printf ("core1 launch failed..\n");
		while (1) {
			tight_loop_contents();
		}
	}

	printf ("launch core1 fifo:%08X\n", data);

	sleep_ms(5);
	while (1) {
		tud_task();
		kazzo_flash_process();
	}

    return 0;
}
