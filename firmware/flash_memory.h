//
// FC Cartridge bus simulator "tuna"
//
// Copyrighte (C) 2021 Norix (NX labs)
//
// License: GPL2 (see gpl-2.0.txt)
//
// based on kazzo AVR firmware "flashmemory.c"
// URL: http://unagi.sourceforge.jp/
//
#ifndef	__FLASH_MEMORY_H__
#define	__FLASH_MEMORY_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// prototypes
// idle
void	kazzo_flash_both_idle(void);

// cpu proc
uint8_t	kazzo_flash_cpu_status(void);
void	kazzo_flash_cpu_config(const uint8_t *data, uint16_t length);
void	kazzo_flash_cpu_program(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_flash_cpu_erase(uint16_t address);
void	kazzo_flash_cpu_device_get(uint8_t d[2]);

// ppu proc
uint8_t	kazzo_flash_ppu_status(void);
void	kazzo_flash_ppu_config(const uint8_t *data, uint16_t length);
void	kazzo_flash_ppu_program(uint16_t address, uint16_t length, const uint8_t *data);
void	kazzo_flash_ppu_erase(uint16_t address);
void	kazzo_flash_ppu_device_get(uint8_t d[2]);

// task proc
void	kazzo_flash_process(void);

#ifdef __cplusplus
}
#endif

#endif
