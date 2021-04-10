//
// FC Cartridge bus simulator "tuna"
//
// Copyrighte (C) 2021 Norix (NX labs)
//
// License: GPL2 (see gpl-2.0.txt)
//
#ifndef	__BUS_SIMULATION_H__
#define	__BUS_SIMULATION_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// prototypes
void	bussim_process(void);

// for kazzo
typedef struct {
	uint16_t	address;
	uint8_t		data;
} KAZZO_FLASH_ORDER;

typedef enum {
	OK, NG
} KAZZO_COMPARE_STATUS;

// flash
#define	KAZZO_FLASH_PROGRAM_ORDER	3

KAZZO_COMPARE_STATUS	kazzo_cpu_compare(uint16_t address, uint16_t length, const uint8_t *data);
KAZZO_COMPARE_STATUS	kazzo_ppu_compare(uint16_t address, uint16_t length, const uint8_t *data);

void	kazzo_cpu_read(uint16_t address, uint16_t length, uint8_t *data);
void	kazzo_cpu_write_6502(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_cpu_write_flash(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_cpu_write_flash_order(const KAZZO_FLASH_ORDER* t);
void	kazzo_ppu_read(uint16_t address, uint16_t length, uint8_t *data);
void	kazzo_ppu_write(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_ppu_write_flash(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_ppu_write_order(const KAZZO_FLASH_ORDER* t);

uint8_t	kazzo_vram_connection_get(void);

#ifdef __cplusplus
}
#endif

#endif
