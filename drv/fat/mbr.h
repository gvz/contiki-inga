﻿/* Copyright (c) 2012, Christoph Peltz
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
 * \addtogroup Drivers
 * @{
 *
 * \defgroup mbr_driver Master Boot Record Support
 *
 * <p></p>
 * @{
 *
 */

/**
 * \file
 *		MBR Support definitions
 * \author
 *      Christoph Peltz <peltz@ibr.cs.tu-bs.de>
 */
 
#ifndef _MBR_H_
#define _MBR_H_

#include "diskio.h"

#define MBR_SUCCESS DISKIO_SUCCESS
#define MBR_ERROR_DISKIO_ERROR 1
#define MBR_ERROR_NO_MBR_FOUND 2
#define MBR_ERROR_INVALID_PARTITION 3
#define MBR_ERROR_PARTITION_EXISTS 4

#define MBR_PARTITION_TYPE_FAT32_LBA 0x0C
#define MBR_PARTITION_TYPE_FAT16_LBA 0x0E

/**
 * Represents a primary partition in the mbr.
 */
struct mbr_primary_partition {
	/** 0x00 non-bootable, 0x80 bootable, other values indicate that it is invalid */
	uint8_t status;
	/**
	 * Saves the first sector of the partition in the old CHS format (cylinder, head, sector)
	 *
	 * First byte stores the head value.
	 * Second byte stores the sector in the positions 0 - 5 and the upper to bits of the cylinder (Bits 8 and 9)
	 * Third byte stores the lower 8 bits of the cylinder value (0 - 7)
	 */
	uint8_t chs_first_sector[3];
	/** The type of partition this is, like FAT16 or FAT32; type = 0 means it's unused*/
	uint8_t type;
	/** Stores the last sector of the partition in the old CHS format */
	uint8_t chs_last_sector[3];
	/** Stores the absolute address of the first sector of the partition */
	uint32_t lba_first_sector;
	/** Stores the number of sectors the partition is long */
	uint32_t lba_num_sectors;
};

/**
 * Represents the MBR of a disk, without the code portion and the constant bytes
 */
struct mbr { //ignores everything but primary partitions (saves 448 bytes)
	/** The MBR supports max 4 Primary partitions.
	 * For the sake of simplicity Extended partitions are not implemented.
	 */
	struct mbr_primary_partition partition[4];
};

/**
 * Initializes a mbr structure.
 *
 * Should be called first on a new mbr structure.
 * \param *mbr the mbr which should be initialized
 * \param disk_size the size of the disk on which the mbr will reside
 */
void mbr_init( struct mbr *mbr );

/**
 * Reads the MBR from the specified device.
 *
 * The MBR is 512 Bytes long. That is normally one block to be read.
 * \param *from device from which the mbr is read
 * \param *to whe mbr structure in which the data is parsed
 * \return MBR_SUCCESS on success, MBR_ERROR_DISKIO_ERROR if there was a problem reading the block or MBR_ERROR_NO_MBR_FOUND when there is no MBR on this device.
 */
int mbr_read( struct diskio_device_info *from, struct mbr *to );

/**
 * Write the MBR to the specified device.
 *
 * \param *from the mbr structure which should be written on the device
 * \param *to the device pointer to which we will write the mbr
 * \return MBR_SUCCESS on success, !0 on a diskio error which is returned, see diskio_write_block for more information
 */
int mbr_write( struct mbr *from, struct diskio_device_info *to );

/**
 * Adds a Partition to the mbr-structure.
 *
 * \param *mbr The mbr-structure in which to insert the partition.
 * \param part_num Number of the Partition which should be added.
 * \param part_type Type of the partition.
 * \param start LBA-style start of the partition.
 * \param len LBA-style length of the partition.
 * \return MBR_SUCCESS on success or MBR_ERROR_PARTITION_EXISTS.
 */
int mbr_addPartition(struct mbr *mbr, uint8_t part_num, uint8_t part_type, uint32_t start, uint32_t len );

/**
 * Deletes a Partition from the mbr-structure.
 *
 * \param *mbr The mbr-structure which should be edited.
 * \param part_num Number of the partition which should be removed.
 * \return MBR_SUCCESS on success or MBR_ERROR_INVALID_PARTITION.
 */
int mbr_delPartition(struct mbr *mbr, uint8_t part_num );

/**
 * Checks if the given Partition exists.
 *
 * \param part_num Number of the Partition which should be checked.
 * \return 1 if there is a Partition with the given number, otherwise 0.
 */
int mbr_hasPartition(struct mbr *mbr, uint8_t part_num );

#endif
