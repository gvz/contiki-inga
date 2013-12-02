/*
 * Copyright (c) 2012, TU Braunschweig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 *		MicroSD Card interface definitions
 * \author
 * 		Original Source Code:
 * 		Ulrich Radig
 * 		Modified by:
 *              Ulf Kulau <kulau@ibr.cs.tu-bs.de>
 *              Enrico Joerns <joerns@ibr.cs.tu-bs.de>
 */

/**
 * \addtogroup inga_device_driver
 * @{
 */

/**
 * \defgroup sdcard_interface MicroSD Card Interface
 *
 *	\note This sd-card interface is based on the work of Ulrich Radig
 *	      "Connect AVR to MMC/SD" and was a little bit modified plus
 *	      adapted to the mspi-drv
 * 
 * \author
 *		Christoph Peltz <peltz@ibr.cs.tu-bs.de>
 * @{
 */

#ifndef INGA_SDCARD_H
#define INGA_SDCARD_H

#include "mspi.h"
#include <stdio.h>

/*!
 * SPI device order. The chip select number where the
 * sdcard-Card is connected to the BCD-decimal decoder
 */
#define MICRO_SD_CS 					5


/**
 * \brief Powers on and initialize the sdcard / SD-Card
 *
 * \retval 0 SD-Card was initialized without an error
 * \retval 1 CMD0 failure!
 * \retval 2 CMD1 failure!
 * \retval 3 Failure reading the CSD!
 * \retval 4 CMD8 failure!
 * \retval 5 CMD16 failure!
 * \retval 6 ACMD41 failure!
 * \retval 7 CMD58 failure!
 */
uint8_t sdcard_init(void);

/**
 * \brief This function will read the CSD (16 Bytes) of the SD-Card.
 *
 * \param *buffer Pointer to a block buffer, MUST hold at least 16 Bytes.
 *
 * \retval  0 SD-Card CSD read was successful
 * \retval  1 CMD9 failure!
 */
uint8_t sdcard_read_csd(uint8_t *buffer);

/**
 * \brief This function returns the number of bytes in one block.
 *
 * Mainly used to calculated size of the SD-Card together with
 * sdcard_get_card_block_count().
 *
 *
 * \return Number of bytes per block on the SD-Card
 */
uint16_t sdcard_get_block_size();

/**
 * \brief This function indicates if a card is a SDSC or SDHC/SDXC card.
 *
 * \note sdcard_init() must be called beforehand and be successful before this
 * functions return value has any meaning.
 *
 * \return Not 0 if the card is SDSC and 0 if SDHC/SDXC
 */
uint8_t sdcard_is_SDSC();

/**
 */
uint8_t sdcard_erase_blocks(uint32_t startaddr, uint32_t endaddr);

/**
 * \brief This function will read one block (512, 1024, 2048 or 4096Byte) of the SD-Card.
 *
 * \param addr Block address
 * \param *buffer Pointer to a block buffer (needs to be as long as sdcard_get_block_size()).
 *
 * \retval 0 SD-Card block read was successful
 * \retval 1 CMD17 failure!
 * \retval 2 no start byte
 */
uint8_t sdcard_read_block(uint32_t addr, uint8_t *buffer);

/**
 * \brief This function will write one block (512, 1024, 2048 or 4096Byte) of the SD-Card.
 *
 * \param addr Block address
 * \param *buffer Pointer to a block buffer (needs to be as long as sdcard_get_block_size()).
 *
 * \retval 0 SD-Card block write was successful
 * \retval 1 CMD24 failure!
 */
uint8_t sdcard_write_block(uint32_t addr, uint8_t *buffer);

/**
 * \brief This function sends a command via SPI to the SD-Card. An SPI
 * command consists off 6 bytes
 *
 * \param cmd Command number to send
 * \param *arg pointer to 32bit argument (addresses etc.)
 * \param *resp Pointer to the response array. Only needed for responses other than R1. May be NULL if response is R1. Otherwise resp must be long enough for the response (only R3 and R7 are supported yet) and the first byte of the response array must indicate the response that is expected. For Example the first byte should be 0x07 if response type R7 is expected.
 *
 * \return R1 response byte or 0xFF in case of read/write timeout
 */
uint8_t sdcard_write_cmd(uint8_t cmd, uint32_t *arg, uint8_t *resp);

/**
 * \brief This function calculates the CRC16 for a 512 Byte data block.
 *
 * \param *data The array containing the data to be send. Must be 512 Bytes long.
 * \return The CRC16 code for the given data.
 */
uint16_t sdcard_data_crc(uint8_t *data);

/**
 * Turns crc capabilities of the card on or off.
 *
 * \note Does not work properly at this time. (FIXME)
 * \param enable 0 if CRC should be disabled, 1 if it should be enabled
 * \return 0 on success, !=0 otherwise
 */
uint8_t sdcard_set_CRC(uint8_t enable);

/**
 * Returns card size
 * @return card size in bytes
 */
uint64_t sdcard_get_card_size();

/**
 * @return number of blocks
 */
uint32_t sdcard_get_block_num();

/** @} */ // sdcard_interface
/** @} */ // inga_device_driver

#endif /* INGA_SDCARD_H */
