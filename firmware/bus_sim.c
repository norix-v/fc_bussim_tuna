//
// FC Cartridge bus simulator "tuna"
//
// Copyrighte (C) 2021 Norix (NX labs)
//
// License: GPL2 (see gpl-2.0.txt)
//
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "bus_sim.h"
#include "bus_sim.pio.h"

// pin definitions
// output pins
#define	PI_PHI2_BIT		28
#define	PI_PHI2			(1<<PI_PHI2_BIT)
#define	PI_ROMSEL_BIT	27
#define	PI_ROMSEL		(1<<PI_ROMSEL_BIT)
#define	PI_RW_BIT		26
#define	PI_RW			(1<<PI_RW_BIT)
#define	PI_WR_BIT		21
#define	PI_WR			(1<<PI_WR_BIT)
#define	PI_RD_BIT		20
#define	PI_RD			(1<<PI_RD_BIT)

#define	PI_ADDR_L_BIT	17
#define	PI_ADDR_L		(1<<PI_ADDR_L_BIT)
#define	PI_ADDR_H_BIT	16
#define	PI_ADDR_H		(1<<PI_ADDR_H_BIT)

// inout pins
#define	PI_CART_ENABLE_BIT	4
#define	PI_CART_ENABLE	(1<<PI_CART_ENABLE_BIT)
#define	PI_IRQ_BIT		22
#define	PI_IRQ			(1<<PI_IRQ_BIT)
#define	PI_VRAMCS_BIT	19
#define	PI_VRAMCS		(1<<PI_VRAMCS_BIT)
#define	PI_VRAMA10_BIT	18
#define	PI_VRAMA10		(1<<PI_VRAMA10_BIT)

// bi-directional pins
#define	PI_D7_BIT		13
#define	PI_D6_BIT		12
#define	PI_D5_BIT		11
#define	PI_D4_BIT		10
#define	PI_D3_BIT		9
#define	PI_D2_BIT		8
#define	PI_D1_BIT		7
#define	PI_D0_BIT		6
#define	PI_DATA_SHIFT	PI_D0_BIT
#define	PI_DATA_MASK	(0x00FF<<PI_DATA_SHIFT)

#define	PI_ACCESS_BIT	2
#define	PI_ACCESS		(1<<PI_ACCESS_BIT)

#define	PI_OUT_PINS		(PI_PHI2|PI_ROMSEL|PI_RW|PI_WR|PI_RD|PI_ADDR_L|PI_ADDR_H|PI_ACCESS)
#define	PI_INP_PINS		(PI_CART_ENABLE|PI_IRQ|PI_VRAMCS|PI_VRAMA10)
#define	PI_BIDIR_PINS	(PI_DATA_MASK)

// bussim access types
typedef enum {
	BUSSIM_IDLE = 0,
	BUSSIM_ADDR_ONLY,			// address set only
	BUSSIM_PRG_FLASH,			// always use ROMSEL
	BUSSIM_PRG_WRITE_7FFF,		// $0000-$7FFF
	BUSSIM_PRG_WRITE_8000,		// $8000-$FFFF
	BUSSIM_PRG_READ_7FFF,		// $0000-$7FFF
	BUSSIM_PRG_READ_8000,		// $8000-$FFFF
	BUSSIM_CHR_WRITE,
	BUSSIM_CHR_READ,
	BUSSIM_COMMAND_NUM
} BUSSIM_CMD_TYPE;

// bussim address
//const uint16_t	BUSSIM_ADDRESS_CLOSE = 0x3000;	// A13N=0
const uint16_t	BUSSIM_ADDRESS_CLOSE = 0x8000;	// A13N=0

// commands

#pragma pack(1)
typedef union
{
	struct {
		uint16_t	addr;
		uint8_t		data;
		uint8_t		cmd;
	} pack;
	struct {
		uint16_t	addr;
		uint8_t		data;
		uint8_t		dirs;
	} pio;
	struct {
		uint16_t	instr0;
		uint16_t	instr1;
	} instr;
	uint32_t	data;
} BUSSIM_CMD;
#pragma pack()

// PIO command buffer
static	BUSSIM_CMD	cmd_nop[3];
static	BUSSIM_CMD	cmd_prg_flash[3];
static	BUSSIM_CMD	cmd_prg_write_7FFF[3];
static	BUSSIM_CMD	cmd_prg_write_8000[3];
static	BUSSIM_CMD	cmd_prg_read_7FFF[3];
static	BUSSIM_CMD	cmd_prg_read_8000[3];
static	BUSSIM_CMD	cmd_chr_write[3];
static	BUSSIM_CMD	cmd_chr_read[3];

// command select table
static BUSSIM_CMD* const	cmd_type_table[] = {
	cmd_nop,					// BUSSIM_IDLE
	cmd_nop,					// BUSSIM_ADDR_ONLY
	cmd_prg_flash,				// BUSSIM_PRG_FLASH
	cmd_prg_write_7FFF,			// BUSSIM_PRG_WRITE_7FFF
	cmd_prg_write_8000,			// BUSSIM_PRG_WRITE_8000
	cmd_prg_read_7FFF,			// BUSSIM_PRG_READ_7FFF
	cmd_prg_read_8000,			// BUSSIM_PRG_READ_8000
	cmd_chr_write,				// BUSSIM_CHR_WRITE
	cmd_chr_read				// BUSSIM_CHR_READ
};

// output address make
static void	bussim_access_lamp(const bool enable)
{
	if (enable) {
		gpio_set_mask(PI_ACCESS);
	} else {
		gpio_clr_mask(PI_ACCESS);
	}
}

// output address make
static inline uint16_t	bussim_address_make(const uint16_t addr)
{
	// A13n = /A13
	return	(addr & 0x7FFF) | ((~addr & 0x2000)<<(15-13));
}

//
// bus simulation uninitialize
//
void	bussim_uninitialize(void)
{
	// PIO reset
	pio_enable_sm_mask_in_sync(pio0, 0);
	pio_clear_instruction_memory (pio0);

	// cpu bus control pins to 'L'
	gpio_init_mask(PI_OUT_PINS);
	gpio_put_masked(PI_OUT_PINS, PI_RD|PI_WR);
	gpio_set_dir_out_masked(PI_OUT_PINS);
}

//
// bus simulation initialize
//
void	bussim_initialize(void)
{
	// port setup
	gpio_pull_up(PI_CART_ENABLE_BIT);
	gpio_pull_up(PI_IRQ_BIT);
	gpio_pull_up(PI_VRAMCS_BIT);
	gpio_pull_up(PI_VRAMA10_BIT);

	for (uint i = PI_D0_BIT; i <= PI_D7_BIT; i++) {
		gpio_pull_up(i);
	}

	gpio_init_mask(PI_INP_PINS);
	gpio_set_dir_in_masked(PI_INP_PINS);

	// PIO setup
	pio_sm_config	c;
	uint	sm, offset;

	// PIO reset
	pio_enable_sm_mask_in_sync(pio0, 0);
	pio_clear_instruction_memory (pio0);

	// state machine 0 (main process)
	sm = 0;
	// out pins
	for (uint i = PI_D0_BIT; i <= PI_D7_BIT; i++) {
		pio_gpio_init(pio0, i);
	}
	// set pins
	pio_gpio_init(pio0, PI_RD_BIT);
	pio_gpio_init(pio0, PI_WR_BIT);
	pio_sm_set_consecutive_pindirs(pio0, sm, PI_RD_BIT, 2, true);
	// side set pins
	pio_gpio_init(pio0, PI_ADDR_H_BIT);
	pio_gpio_init(pio0, PI_ADDR_L_BIT);
	pio_sm_set_consecutive_pindirs(pio0, sm, PI_ADDR_H_BIT, 2, true);
	// state machine config
	offset = pio_add_program(pio0, &bussim_pio_proc_program);
	c = bussim_pio_proc_program_get_default_config(offset);
	sm_config_set_out_pins(&c, PI_D0_BIT, 8);
	sm_config_set_in_pins(&c, PI_D0_BIT);
	sm_config_set_set_pins(&c, PI_RD_BIT, 2);
	sm_config_set_sideset_pins(&c, PI_ADDR_H_BIT);
	sm_config_set_in_shift(&c, true, true, 32);		// in fifo auto push
	sm_config_set_out_shift(&c, true, false, 32);	// out fifo manual pull
	pio_sm_init(pio0, sm, offset, &c);

	// state machine 1 (sub process)
	sm = 1;
	// set pins
	pio_gpio_init(pio0, PI_RW_BIT);
	pio_sm_set_consecutive_pindirs(pio0, sm, PI_RW_BIT, 1, true);
	// state machine config
	offset = pio_add_program(pio0, &bussim_pio_cpurw_program);
	c = bussim_pio_cpurw_program_get_default_config(offset);
	sm_config_set_sideset_pins(&c, PI_RW_BIT);
	pio_sm_init(pio0, sm, offset, &c);

	// state machine 2 (sub process)
	sm = 2;
	// set pins
	pio_gpio_init(pio0, PI_ROMSEL_BIT);
	pio_sm_set_consecutive_pindirs(pio0, sm, PI_ROMSEL_BIT, 1, true);
	// state machine config
	offset = pio_add_program(pio0, &bussim_pio_romsel_program);
	c = bussim_pio_romsel_program_get_default_config(offset);
	sm_config_set_sideset_pins(&c, PI_ROMSEL_BIT);
	pio_sm_init(pio0, sm, offset, &c);

	// state machine 3 (sub process)
	sm = 3;
	// set pins
	pio_gpio_init(pio0, PI_PHI2_BIT);
	pio_sm_set_consecutive_pindirs(pio0, sm, PI_PHI2_BIT, 1, true);
	// state machine config
	offset = pio_add_program(pio0, &bussim_pio_phi2_program);
	c = bussim_pio_phi2_program_get_default_config(offset);
	sm_config_set_sideset_pins(&c, PI_PHI2_BIT);
	pio_sm_init(pio0, sm, offset, &c);

	// PIO inst setups
	// nop command
	cmd_nop[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_nop[0].pio.data = 0x00;
	cmd_nop[0].pio.dirs = 0x00;
	cmd_nop[1].instr.instr0 = bussim_pio_codes_program_instructions[0];		// R/W
	cmd_nop[1].instr.instr1 = bussim_pio_codes_program_instructions[3];		// PHI2
	cmd_nop[2].instr.instr0 = bussim_pio_codes_program_instructions[0];		// /ROMSEL
	cmd_nop[2].instr.instr1 = bussim_pio_codes_program_instructions[0];		// /RD,/WR

	// PRG flash
	cmd_prg_flash[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_prg_flash[0].pio.data = 0x00;
	cmd_prg_flash[0].pio.dirs = 0xFF;
	cmd_prg_flash[1].instr.instr0 = bussim_pio_codes_program_instructions[1];	// R/W
	cmd_prg_flash[1].instr.instr1 = bussim_pio_codes_program_instructions[0];	// PHI2
	cmd_prg_flash[2].instr.instr0 = bussim_pio_codes_program_instructions[2];	// /ROMSEL
	cmd_prg_flash[2].instr.instr1 = bussim_pio_codes_program_instructions[0];	// /RD,/WR

	// PRG write $0000-$7FFF
	cmd_prg_write_7FFF[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_prg_write_7FFF[0].pio.data = 0x00;
	cmd_prg_write_7FFF[0].pio.dirs = 0xFF;
	cmd_prg_write_7FFF[1].instr.instr0 = bussim_pio_codes_program_instructions[1];	// R/W
	cmd_prg_write_7FFF[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_prg_write_7FFF[2].instr.instr0 = bussim_pio_codes_program_instructions[0];	// /ROMSEL
	cmd_prg_write_7FFF[2].instr.instr1 = bussim_pio_codes_program_instructions[0];	// /RD,/WR

	// PRG write $8000-$FFFF
	cmd_prg_write_8000[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_prg_write_8000[0].pio.data = 0x00;
	cmd_prg_write_8000[0].pio.dirs = 0xFF;
	cmd_prg_write_8000[1].instr.instr0 = bussim_pio_codes_program_instructions[1];	// R/W
	cmd_prg_write_8000[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_prg_write_8000[2].instr.instr0 = bussim_pio_codes_program_instructions[2];	// /ROMSEL
	cmd_prg_write_8000[2].instr.instr1 = bussim_pio_codes_program_instructions[0];	// /RD,/WR

	// PRG read $0000-$7FFF
	cmd_prg_read_7FFF[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_prg_read_7FFF[0].pio.data = 0x00;
	cmd_prg_read_7FFF[0].pio.dirs = 0x00;
	cmd_prg_read_7FFF[1].instr.instr0 = bussim_pio_codes_program_instructions[0];	// R/W
	cmd_prg_read_7FFF[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_prg_read_7FFF[2].instr.instr0 = bussim_pio_codes_program_instructions[0];	// /ROMSEL
	cmd_prg_read_7FFF[2].instr.instr1 = bussim_pio_codes_program_instructions[0];	// /RD,/WR

	// PRG read $8000-$FFFF
	cmd_prg_read_8000[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_prg_read_8000[0].pio.data = 0x00;
	cmd_prg_read_8000[0].pio.dirs = 0x00;
	cmd_prg_read_8000[1].instr.instr0 = bussim_pio_codes_program_instructions[0];	// R/W
	cmd_prg_read_8000[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_prg_read_8000[2].instr.instr0 = bussim_pio_codes_program_instructions[2];	// /ROMSEL
	cmd_prg_read_8000[2].instr.instr1 = bussim_pio_codes_program_instructions[0];	// /RD,/WR

	// CHR write
	cmd_chr_write[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_chr_write[0].pio.data = 0x00;
	cmd_chr_write[0].pio.dirs = 0xFF;
	cmd_chr_write[1].instr.instr0 = bussim_pio_codes_program_instructions[0];	// R/W
	cmd_chr_write[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_chr_write[2].instr.instr0 = bussim_pio_codes_program_instructions[0];	// /ROMSEL
	cmd_chr_write[2].instr.instr1 = bussim_pio_codes_program_instructions[4];	// /RD,/WR

	// CHR read
	cmd_chr_read[0].pio.addr = BUSSIM_ADDRESS_CLOSE;
	cmd_chr_read[0].pio.data = 0x00;
	cmd_chr_read[0].pio.dirs = 0x00;
	cmd_chr_read[1].instr.instr0 = bussim_pio_codes_program_instructions[0];	// R/W
	cmd_chr_read[1].instr.instr1 = bussim_pio_codes_program_instructions[3];	// PHI2
	cmd_chr_read[2].instr.instr0 = bussim_pio_codes_program_instructions[0];	// /ROMSEL
	cmd_chr_read[2].instr.instr1 = bussim_pio_codes_program_instructions[5];	// /RD,/WR
}

//
// bus simulation process
//
// Note: the function needs to run in core1.
//
void	__not_in_flash_func(bussim_process)(void)
{
BUSSIM_CMD	cmd, cmdprev;
uint32_t*	cmd_table;

	// 1st time sync
	multicore_fifo_push_blocking(0x2A032C02);

_coldboot:
	// transition to power-off state
	bussim_uninitialize();

	// wait for cartridge power-on
	while (gpio_get(PI_CART_ENABLE_BIT)) {
		tight_loop_contents();
		sleep_ms(1);
	}

	// transition to power-on state
	bussim_initialize();

	// command setup
	cmd.pack.addr = BUSSIM_ADDRESS_CLOSE;
	cmd.pack.data = 0x00;
	cmd.pack.cmd  = BUSSIM_IDLE;
	cmdprev.data = 0;

	cmd_table = (uint32_t*)cmd_type_table[BUSSIM_IDLE];

	// enable state machine
	pio_sm_clear_fifos(pio0, 0);
	pio_enable_sm_mask_in_sync(pio0, 0xF);

	// bussim loop
	while (1) {
		// send command
		pio0->txf[0] = cmd_table[0];
		pio0->txf[0] = cmd_table[1];
		pio0->txf[0] = cmd_table[2];

		if (cmdprev.pack.cmd != BUSSIM_IDLE) {
			// send command response
			sio_hw->fifo_wr = cmdprev.data;
			__sev();
		}

		cmdprev.data = cmd.data;
		cmd.data = 0;
		if (multicore_fifo_rvalid()) {
			// get command request
			__wfe();
			cmd.data = sio_hw->fifo_rd;

			if (cmd.pack.cmd != BUSSIM_IDLE) {
				BUSSIM_CMD*	c = cmd_type_table[cmd.pack.cmd];
				cmd_table = (uint32_t*)c;

				// setup command params
				c[0].pio.addr = cmd.pack.addr;
				c[0].pio.data = cmd.pack.data;
			}
		} else {
			cmd_table = (uint32_t*)cmd_type_table[cmd.pack.cmd];

			// cartridge power off
			if (gpio_get(PI_CART_ENABLE_BIT)) {
				// wait command complete
				(void)pio_sm_get_blocking(pio0, 0);
				break;
			}
		}

		// wait command complete
		uint32_t data = pio_sm_get_blocking(pio0, 0);
		cmdprev.pack.data = (uint8_t)data;
	}

	// cold boot
	goto	_coldboot;
}

//
// bus access functions
//
// Note: functions need to be called from core0
//
uint8_t	bussim_cpu_read(uint16_t addr)
{
BUSSIM_CMD	cmd, ret;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return	(uint8_t)rand();
	}

	if (addr < 0x8000) {
		cmd.pack.cmd = BUSSIM_PRG_READ_7FFF;
	} else {
		cmd.pack.cmd = BUSSIM_PRG_READ_8000;
	}
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = 0x55;	// dummy data

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed
	ret.data = multicore_fifo_pop_blocking();
	bussim_access_lamp(false);

	return	ret.pack.data;
}

void	bussim_cpu_write(uint16_t addr, uint8_t data)
{
BUSSIM_CMD	cmd;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return;
	}

	if (addr < 0x8000) {
		cmd.pack.cmd = BUSSIM_PRG_WRITE_7FFF;
	} else {
		cmd.pack.cmd = BUSSIM_PRG_WRITE_8000;
	}
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = data;

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed
	(void)multicore_fifo_pop_blocking();
	bussim_access_lamp(false);
}

void	bussim_cpu_write_clkfix(uint16_t addr, uint8_t data)
{
BUSSIM_CMD	cmd;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return;
	}

	cmd.pack.cmd = BUSSIM_PRG_FLASH;
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = data;

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed
	(void)multicore_fifo_pop_blocking();
	bussim_access_lamp(false);
}

uint8_t	bussim_ppu_read(uint16_t addr)
{
BUSSIM_CMD	cmd, ret;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return	(uint8_t)rand();
	}

	cmd.pack.cmd = BUSSIM_CHR_READ;
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = 0xAA;	// dummy data

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed
	ret.data = multicore_fifo_pop_blocking();
	bussim_access_lamp(false);

	return	ret.pack.data;
}

void	bussim_ppu_write(uint16_t addr, uint8_t data)
{
BUSSIM_CMD	cmd;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return;
	}

	cmd.pack.cmd = BUSSIM_CHR_WRITE;
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = data;

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed
	(void)multicore_fifo_pop_blocking();
	bussim_access_lamp(false);
}

void	bussim_set_address(const uint16_t addr)
{
BUSSIM_CMD	cmd;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return;
	}

	cmd.pack.cmd = BUSSIM_ADDR_ONLY;
	cmd.pack.addr = bussim_address_make(addr);
	cmd.pack.data = 0;

	// access request
	bussim_access_lamp(true);
	multicore_fifo_push_blocking(cmd.data);

	// wait access completed (VRAMA10 read)
	(void)multicore_fifo_pop_blocking();
	bussim_access_lamp(false);
}

uint8_t	bussim_test_vram_connection(void)
{
uint16_t addr;
uint8_t	ret, mask;

	// cartridge power off
	if (gpio_get(PI_CART_ENABLE_BIT)) {
		return	0xFF;
	}

	ret = 0;
	mask = 0x01;
	addr = 0x2000;
	for (int i = 0; i < 4; i++) {
		bussim_set_address(addr);
		if (gpio_get(PI_VRAMA10_BIT)) {
			ret |= mask;
		}
		mask <<= 1;
		addr += 1<<10;
	}

	// set nop address
	bussim_set_address(BUSSIM_ADDRESS_CLOSE);

	return	ret;
}

//
// function for kazzo
//
// compare cpu data
KAZZO_COMPARE_STATUS	kazzo_cpu_compare(uint16_t address, uint16_t length, const uint8_t *data)
{
	while (length != 0) {
		uint8_t	readdata = bussim_cpu_read(address);
		if (*data != readdata) {
			return	NG;
		}
		++address;
		++data;
		--length;
	}

	return	OK;
}


// compare ppu data
KAZZO_COMPARE_STATUS	kazzo_ppu_compare(uint16_t address, uint16_t length, const uint8_t *data)
{
	while (length != 0) {
		uint8_t	readdata = bussim_ppu_read(address);
		if (*data != readdata) {
			return	NG;
		}
		++address;
		++data;
		--length;
	}

	return	OK;
}

// cpu read data
void	kazzo_cpu_read(uint16_t address, uint16_t length, uint8_t *data)
{
	while (length != 0) {
		*data = bussim_cpu_read(address);
		++data;
		++address;
		--length;
	}
}

// normal cpu write data
void	kazzo_cpu_write_6502(uint16_t address, uint16_t length, const uint8_t *data)
{
	while (length != 0) {
		bussim_cpu_write(address, *data);
		++data;
		++address;
		--length;
	}
}

// cpu flash memory write
void	kazzo_cpu_write_flash(uint16_t address, uint16_t length, const uint8_t *data)
{
	while (length != 0) {
		if (*data != 0xFF) {	// speed up
			bussim_cpu_write_clkfix(address, *data);
		}
		++data;
		++address;
		--length;
	}
}

// cpu flash memory command
void	kazzo_cpu_write_flash_order(const KAZZO_FLASH_ORDER* t)
{
	int	length = KAZZO_FLASH_PROGRAM_ORDER;
	while (length != 0) {
		bussim_cpu_write_clkfix(t->address, t->data);
		++t;
		--length;
	}
}

// ppu read data
void	kazzo_ppu_read(uint16_t address, uint16_t length, uint8_t *data)
{
	while (length != 0) {
		*data = bussim_ppu_read(address);
		++data;
		++address;
		--length;
	}
}

// ppu flash memory write
void	kazzo_ppu_write(uint16_t address, uint16_t length, const uint8_t *data)
{
	while(length != 0){
		bussim_ppu_write(address, *data);
		++data;
		++address;
		--length;
	}
}

// ppu flash memory write
void	kazzo_ppu_write_flash(uint16_t address, uint16_t length, const uint8_t *data)
{
	while(length != 0){
		if (*data != 0xFF) {	// speed up
			bussim_ppu_write(address, *data);
		}
		++data;
		++address;
		--length;
	}
}

// ppu flash memory command
void	kazzo_ppu_write_order(const KAZZO_FLASH_ORDER* t)
{
	int	length = KAZZO_FLASH_PROGRAM_ORDER;
	while (length != 0) {
		bussim_ppu_write(t->address, t->data);
		++t;
		--length;
	}
}

// VRAM connection
uint8_t	kazzo_vram_connection_get(void)
{
	return	bussim_test_vram_connection();
}

