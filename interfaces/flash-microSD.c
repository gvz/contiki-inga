/* Copyright (c) 2010, Ulf Kulau
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \addtogroup Device Interfaces
 * @{
 *
 * \addtogroup microSD_interface
 * @{
 */

/**
 * \file
 *      MicroSD Card interface implementation
 * \author
 * 		Original source: Ulrich Radig
 * 		<< modified by >>
 *      Ulf Kulau <kulau@ibr.cs.tu-bs.de>
 *		Christoph Peltz <peltz@ibr.cs.tu-bs.de>
 *
 */

#include "flash-microSD.h"
//#define DEBUG 1
/**
 * \brief The number of bytes in one block on the SD-Card.
 */
uint16_t microSD_block_size = 512;
/**
 * \brief Number of blocks on the SD-Card.
 */
uint32_t microSD_card_block_count = 0;

uint16_t microSD_get_block_size() {
	return microSD_block_size;
}

uint32_t microSD_get_card_block_count() {
	return microSD_card_block_count;
}

uint8_t microSD_read_csd( uint8_t *buffer ) {
	/*CMD9 this is just a guess, need to verify TODO*/
	uint8_t cmd[6] = { 0x49, 0x00, 0x00, 0x00, 0x00, 0xFF };
	uint8_t i = 0;
	if( microSD_write_cmd( cmd, NULL ) != 0x00 ) {
#if DEBUG
		printf("\nCMD9 failure!");
#endif
		return 1;
	}

	/*wait for the 0xFE start byte*/
	while (mspi_transceive(MSPI_DUMMY_BYTE) != 0xFE) {
	};

	for (i = 0; i < 16; i++) {
		*buffer++ = mspi_transceive(MSPI_DUMMY_BYTE);
	}
	/*CRC-Byte: don't care*/
	mspi_transceive(MSPI_DUMMY_BYTE);
	mspi_transceive(MSPI_DUMMY_BYTE);

	/*release chip select and disable microSD spi*/
	mspi_chip_release(MICRO_SD_CS);

	return 0;
}

void microSD_setSDCInfo( uint8_t *csd ) {
	uint8_t csd_version = ((csd[0] & (12 << 4)) >> 6);
		uint8_t READ_BL_LEN = 0; 
		uint32_t C_SIZE = 0;
		uint16_t C_SIZE_MULT = 0;
		uint32_t mult = 0;
	if( csd_version == 0 ) {
		/*Bits 80 till 83 are the READ_BL_LEN*/
		READ_BL_LEN = csd[5] & 0x0F;
		/*Bits 62 - 73 are C_SIZE*/
		C_SIZE = ((csd[8] & 07000) >> 6) + (((uint32_t)csd[7]) << 2) + (((uint32_t)csd[6] & 07) << 10);
		/*Bits 47 - 49 are C_SIZE_MULT*/
		C_SIZE_MULT = ((csd[9] & 0x80) >> 7) + ((csd[8] & 0x03) << 1);
		mult = (2 << (C_SIZE_MULT + 2));
		microSD_card_block_count = (C_SIZE + 1) * mult;
		microSD_block_size = 1 << READ_BL_LEN;
	} else if( csd_version == 1 ) {
		/*Bits 80 till 83 are the READ_BL_LEN*/
		READ_BL_LEN = csd[5] & 0x0F;
		C_SIZE = csd[9] + (((uint32_t) csd[8]) << 8) + (((uint32_t) csd[7] & 0x3f) << 16);
		microSD_card_block_count = (C_SIZE + 1) * 1024;
		microSD_block_size = 512;
	}
	#ifdef DEBUG
		printf("microSD_setSDCInfo(): CSD Version = %u\n", ((csd[0] & (12 << 4)) >> 6));
		printf("microSD_setSDCInfo(): READ_BL_LEN = %u\n", READ_BL_LEN);
		printf("microSD_setSDCInfo(): C_SIZE = %lu\n", C_SIZE);
		printf("microSD_setSDCInfo(): C_SIZE_MULT = %u\n", C_SIZE_MULT);
		printf("microSD_setSDCInfo(): mult = %lu\n", mult);
		printf("microSD_setSDCInfo(): microSD_card_block_count  = %lu\n", microSD_card_block_count);
		printf("microSD_setSDCInfo(): microSD_block_size  = %u\n", microSD_block_size);
	#endif
}

void microSD_cmd_crc( uint8_t *cmd ) {
	uint32_t stream = (((uint32_t) cmd[0]) << 24) + (((uint32_t) cmd[1]) << 16) + (((uint32_t) cmd[2]) << 8) + ((uint32_t) cmd[3]);
	uint8_t i = 0;
	uint32_t gen = ((uint32_t) 0x89) << 24;

	for( i = 0; i < 40; i++ ) {
		if( stream & (((uint32_t) 0x80) << 24)) {
			stream ^= gen;
		}
		stream = stream << 1;
		if( i == 7 )
			stream += cmd[4];
	}
	cmd[5] = (stream >> 24) + 1;
}

uint8_t microSD_init(void) {
	uint16_t i;
	uint8_t ret = 0;
	/*set pin for mcro sd card power switch to output*/
	MICRO_SD_PWR_PORT_DDR |= (1 << MICRO_SD_PWR_PIN);
	/*switch off the sd card and tri state buffer whenever this is not done
	 *to avoid initialization failures */
	microSD_deinit();
	/*switch on the SD card and the tri state buffer*/
	MICRO_SD_PWR_PORT |= (1 << MICRO_SD_PWR_PIN);
	/*READY TO INITIALIZE micro SD / SD card*/
	mspi_chip_release(MICRO_SD_CS);
	/*init mspi in mode0, at chip select pin 2 and max baud rate*/
	mspi_init(MICRO_SD_CS, MSPI_MODE_0, MSPI_BAUD_MAX);
	/*set SPI mode by chip select (only necessary when mspi manager is active)*/

	mspi_chip_select(MICRO_SD_CS);
	mspi_chip_release(MICRO_SD_CS);
	/*wait 1ms before initialize sd card*/
	_delay_ms(1);
	/*send >74 clock cycles to setup spi-mode*/
	for (i = 0; i < 16; i++) {
		mspi_transceive(MSPI_DUMMY_BYTE);
	}
	/*CMD0: set sd card to idle state*/
	uint8_t cmd0[6]  = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
	uint8_t cmd1[6]  = { 0x41, 0x00, 0x00, 0x00, 0x00, 0xFF };
	uint8_t cmd8[6]  = { 0x48, 0x00, 0x00, 0x01, 0xaa, 0x01 };
	uint8_t cmd41[6] = { 0x69, 0x40, 0x00, 0x00, 0x00, 0x01 };
	uint8_t cmd55[6] = { 0x77, 0x00, 0x00, 0x00, 0x00, 0x01 };
	uint8_t cmd58[6] = { 0x7a, 0x00, 0x00, 0x00, 0x00, 0x01 };
	uint8_t resp[5]  = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t csd[16];
	#ifdef DEBUG
	printf("Writing cmd0\n");
	#endif
	i = 0;
	while( (ret = microSD_write_cmd( cmd0, NULL )) != 0x01 ) {
		i++;
		if (i > 200) {
			mspi_chip_release(MICRO_SD_CS);
			microSD_deinit();
			#ifdef DEBUG
			printf("microSD_init(): cmd0 timeout -> %d\n", ret);
			#endif
			return 1;
		}
	}
	/*send CMD8*/
	microSD_cmd_crc( cmd8 );
	#ifdef DEBUG
	printf("Writing cmd8\n");
	#endif
	resp[0] = 0x07;
	i = 0;
	while( (ret = microSD_write_cmd( cmd8, resp )) != 0x01 ) {
		if( ret & 0x04 && ret != 0xFF ) {
			#ifdef DEBUG
			printf("microSD_init(): cmd8 not supported -> legacy card\n");
			#endif
			break;
		}
		i++;
		if( i > 200 ) {
			mspi_chip_release( MICRO_SD_CS );
			microSD_deinit();
			#ifdef DEBUG
			printf("microSD_init(): cmd8 timeout -> %d\n", ret);
			#endif
			return 4;
		}
	}
	if( ret & 0x04 ) {
		/*prepare next 6 data bytes: CMD1*/
		#ifdef DEBUG
		printf("Writing cmd1\n");
		#endif
		i = 0;
		while( (ret = microSD_write_cmd( cmd1, NULL )) != 0 ) {
			i++;
			if (i > 5500) {
				#ifdef DEBUG
				printf("microSD_init(): cmd1 timeout reached, last return value was %d\n", ret);
				#endif
				mspi_chip_release(MICRO_SD_CS);
				microSD_deinit();
				return 2;
			}
		}
	} else {
		#ifdef DEBUG
		printf("Writing acmd41 (55 + 41)\n");
		printf("	Writing cmd55\n");
		#endif
		resp[0] = 0x01;
		i = 0;
		ACMD41:
		while( microSD_write_cmd( cmd55, NULL ) != 0x01 ) {
			i++;
			if (i > 500) {
				#ifdef DEBUG
				printf("microSD_init(): acmd41 timeout reached, last return value was %d\n", ret);
				#endif
				mspi_chip_release(MICRO_SD_CS);
				microSD_deinit();
				return 6;
			}
		}
		#ifdef DEBUG
		printf("	Writing cmd41\n");
		#endif
		if( microSD_write_cmd( cmd41, NULL ) != 0 )
			goto ACMD41;
		resp[0] = 0x03;
		i = 0;
		#ifdef DEBUG
		printf("Writing cmd58\n");
		#endif
		while( (ret = microSD_write_cmd( cmd58, resp )) != 0x0 ) {
			i++;
			if (i > 900) {
				#ifdef DEBUG
				printf("microSD_init(): cmd58 timeout reached, last return value was %d\n", ret);
				#endif
				mspi_chip_release(MICRO_SD_CS);
				microSD_deinit();
				return 7;
			}
		}
		if( resp[1] & 0x80 ) {
			#ifdef DEBUG
			printf("microSD_init(): acmd41: Card power up okay!\n");
			if( resp[1] & 0x40 )
				printf("microSD_init(): acmd41: Card is SDHC/SDXC\n");
			else
				printf("microSD_init(): acmd41: Card is SDSC\n");
			#endif
		}
	}
	#ifdef DEBUG
	printf("Reading csd\n");
	#endif
	if( microSD_read_csd( csd ) != 0 )
		return 3;
	microSD_setSDCInfo( csd );
	mspi_chip_release(MICRO_SD_CS);
	return 0;
}

uint8_t microSD_read_block(uint32_t addr, uint8_t *buffer) {
	uint16_t i;
	/*CMD17 read block*/
	uint8_t cmd[6] = { 0x51, 0x00, 0x00, 0x00, 0x00, 0xff };
	/*calculate the start address: block_addr = addr * 512*/
	addr = addr << 9;
	/*create cmd bytes according to the address*/
	cmd[1] = ((addr & 0xFF000000) >> 24);
	cmd[2] = ((addr & 0x00FF0000) >> 16);
	cmd[3] = ((addr & 0x0000FF00) >> 8);

	/* send CMD17 with address information. Chip select is done by
	 * the microSD_write_cmd method and */
	if (microSD_write_cmd( cmd, NULL) != 0x00) {
#if DEBUG
		printf("\nCMD17 failure!");
#endif
		return 1;
	}

	/*wait for the 0xFE start byte*/
	while (mspi_transceive(MSPI_DUMMY_BYTE) != 0xFE) {
	};

	for (i = 0; i < microSD_block_size; i++) {
		*buffer++ = mspi_transceive(MSPI_DUMMY_BYTE);
	}
	/*CRC-Byte: don't care*/
	mspi_transceive(MSPI_DUMMY_BYTE);
	mspi_transceive(MSPI_DUMMY_BYTE);

	/*release chip select and disable microSD spi*/
	mspi_chip_release(MICRO_SD_CS);

	return 0;
}

uint8_t microSD_deinit(void) {
	MICRO_SD_PWR_PORT &= ~(1 << MICRO_SD_PWR_PIN);
	return 0;
}

uint8_t microSD_write_block(uint32_t addr, uint8_t *buffer) {
	uint16_t i;
	/*CMD24 write block*/
	uint8_t cmd[6] = { 0x58, 0x00, 0x00, 0x00, 0x00, 0xFF };

	/*calculate the start address: block_addr = addr * 512*/
	addr = addr << 9;
	/*create cmd bytes according to the address*/
	cmd[1] = ((addr & 0xFF000000) >> 24);
	cmd[2] = ((addr & 0x00FF0000) >> 16);
	cmd[3] = ((addr & 0x0000FF00) >> 8);
	/* send CMD24 with address information. Chip select is done by
	 * the microSD_write_cmd method and */
	if (microSD_write_cmd(cmd, NULL) != 0x00) {
#if DEBUG
		printf("\nCMD24 failure!");
#endif
		return -1;
	}

	for (i = 0; i < 10; i++) {
		mspi_transceive(MSPI_DUMMY_BYTE);
	}

	/* send start byte 0xFE to the microSD card to symbolize the beginning
	 * of one data block (512byte)*/
	mspi_transceive(0xFE);

	/*send 1 block (512byte) to the microSD card*/
	for (i = 0; i < microSD_block_size; i++) {
		mspi_transceive(*buffer++);
	}

	/*write CRC checksum: Dummy*/
	mspi_transceive(MSPI_DUMMY_BYTE);
	mspi_transceive(MSPI_DUMMY_BYTE);

	/*failure check: Data Response XXX00101 = OK*/
	if ((mspi_transceive(MSPI_DUMMY_BYTE) & 0x1F) != 0x05) {
#if DEBUG
		printf("\nblock failure!");
#endif
		return -1;
	}

	/*wait while microSD card is busy*/
	while (mspi_transceive(MSPI_DUMMY_BYTE) != 0xff) {
	};
	/*release chip select and disable microSD spi*/
	mspi_chip_release(MICRO_SD_CS);

	return 0;
}

uint8_t microSD_write_cmd(uint8_t *cmd, uint8_t *resp) {
	uint16_t i;
	uint8_t data;
	uint8_t idx = 0;
	uint8_t resp_type = 0x01;

	if( resp != NULL )
		resp_type = resp[0];

	mspi_chip_release(MICRO_SD_CS);
	mspi_transceive(MSPI_DUMMY_BYTE);
	/*begin to send 6 command bytes to the sd card*/
	mspi_chip_select(MICRO_SD_CS);

	for (i = 0; i < 6; i++) {
		mspi_transceive(*cmd++);
	}
	i = 0;
	/*wait for the answer of the sd card*/
	do {
		/*0x01 for acknowledge*/
		data = mspi_transceive(MSPI_DUMMY_BYTE);
		if (i > 500) {
#if DEBUG
			printf("microsSD_write_cmd(): timeout!\n");
#endif
			break;
		}
		i++;
		if( resp != NULL && data != 0xFF) {
			if( resp_type == 0x01 ) {
				resp[0] = data;
				#ifdef DEBUG
				printf("microSD_write_cmd(): data = %x\n", data);
				#endif
			} else if( resp_type == 0x03 || resp_type == 0x07) {
				i = 0;
				resp[idx] = data;
				#ifdef DEBUG
				printf("microSD_write_cmd(): data = %x\n", data);
				#endif
				idx++;
				if( idx >= 5 || (resp[0] & 0xFE) != 0 ) {
					break;
				}
				data = 0xFF;
				continue;
			}
		}
	} while (data == 0xFF);
	return data;
}
