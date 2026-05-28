/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef FLASH_H
#define FLASH_H

typedef enum
{
    CONFIG_STANDARD     = 0,
    CONFIG_LEGACY       = 1,

    NUM_CONFIG_TYPES    = 3
} CONFIG_TYPE_T;

int flash_read_non_volatile_variables(CONFIG_TYPE_T config_type);
int flash_write_non_volatile_variables(void);
int flash_dump(void);
void flash_get_program_size(void);
void flash_get_config_size(void);
void *flash_get_config_location(CONFIG_TYPE_T config_type);

#endif