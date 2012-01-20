#include "cfs/cfs.h"
#include "fat.h"

#define FAT_FD_POOL_SIZE 5

uint8_t sector_buffer[512];
uint32_t sector_buffer_addr = 0;
uint8_t sector_buffer_dirty = 0;

struct file_system {
	struct diskio_device_info *dev;
	struct FAT_Info info;
	uint32_t first_data_sector;
} mounted;

struct dir_entry {
	uint8_t DIR_Name[11];
	uint8_t DIR_Attr;
	uint8_t DIR_NTRes;
	uint8_t CrtTimeTenth;
	uint16_t DIR_CrtTime;
	uint16_t DIR_CrtDate;
	uint16_t DIR_LstAccessDate;
	uint16_t DIR_FstClusHI;
	uint16_t DIR_WrtTime;
	uint16_t DIR_WrtDate;
	uint16_t DIR_FstClusLO;
	uint32_t DIR_FileSize;
};

struct file fat_file_pool[FAT_FD_POOL_SIZE];
struct file_desc fat_fd_pool[FAT_FD_POOL_SIZE];

/* Declerations */
uint16_t _get_free_cluster_16();
uint16_t _get_free_cluster_32();
uint8_t fat_read_block( uint32_t sector_addr );
uint8_t is_a_power_of_2( uint32_t value );
void calc_fat_block( uint32_t cur_block, uint32_t *fat_sec_num, uint32_t *ent_offset );
uint32_t get_free_cluster(uint32_t start_cluster);
uint32_t read_fat_entry( uint32_t sec_addr );
uint32_t find_file_cluster( const char *path );
uint32_t cluster2sector(uint32_t cluster_num);

/**
 * Writes the current buffered block back to the disk if it was changed.
 */
void fat_flush() {
	if( sector_buffer_dirty ) {
		diskio_write_block( mounted.dev, sector_buffer_addr, sector_buffer );
		sector_buffer_dirty = 0;
	}
}

/**
 * Syncs every FAT with the first.
 */
void fat_sync_fats() {
	uint8_t fat_number;
	uint32_t fat_block;
	fat_flush();
	for(fat_block = 0; fat_block < mounted.info.BPB_FATSz; fat_block++) {
		diskio_read_block( mounted.dev, fat_block + mounted.info.BPB_RsvdSecCnt, sector_buffer );
		for(fat_number = 2; fat_number <= mounted.info.BPB_NumFATs; fat_number++) {
			diskio_write_block( mounted.dev, (fat_block + mounted.info.BPB_RsvdSecCnt) + ((fat_number - 1) * mounted.info.BPB_FATSz), sector_buffer );
		}
	}
}

void get_fat_info( struct FAT_Info *info ) {
	memcpy( info, &(mounted.info), sizeof(struct FAT_Info) );
}

void print_current_sector() {
	uint16_t i = 0;
	printf("\n");
	for(i = 0; i < 512; i++) {
		printf("%02x", sector_buffer[i]);
		if( ((i+1) % 2) == 0 )
			printf(" ");
		if( ((i+1) % 32) == 0 )
			printf("\n");
	}
}
/**
 * Determines the type of the fat by using the BPB.
 */
uint8_t determine_fat_type( struct FAT_Info *info ) {
	uint16_t RootDirSectors = ((info->BPB_RootEntCnt * 32) + (info->BPB_BytesPerSec - 1)) / info->BPB_BytesPerSec;
	uint32_t DataSec = info->BPB_TotSec - (info->BPB_RsvdSecCnt + (info->BPB_NumFATs * info->BPB_FATSz) + RootDirSectors);
	uint32_t CountofClusters = DataSec / info->BPB_SecPerClus;
	if(CountofClusters < 4085) {
		/* Volume is FAT12 */
		return FAT12;
	} else if(CountofClusters < 65525) {
		/* Volume is FAT16 */
		return FAT16;
	} else {
		/* Volume is FAT32 */
		return FAT32;
	}
}

/**
 * Parses the Bootsector of a FAT-Filesystem and validates it.
 *
 * \param buffer One sector of the filesystem, must be at least 512 Bytes long.
 * \param info The FAT_Info struct which gets populated with the FAT information.
 * \return <ul>
 *          <li> 0 : Bootsector seems okay.
 *          <li> 1 : BPB_BytesPerSec is not a power of 2
 *          <li> 2 : BPB_SecPerClus is not a power of 2
 *          <li> 4 : Bytes per Cluster is more than 32K
 *          <li> 8 : More than 2 FATs (not supported)
 *          <li> 16: BPB_TotSec is 0
 *          <li> 32: BPB_FATSz is 0
 *          <li> 64: FAT Signature isn't correct
 *         </ul>
 *         More than one error flag may be set but return is 0 on no error.
 */
uint8_t parse_bootsector( uint8_t *buffer, struct FAT_Info *info ) {
	int ret = 0;
	info->BPB_BytesPerSec = (((uint16_t) buffer[12]) << 8) + buffer[11];
	info->BPB_SecPerClus = buffer[13];
	info->BPB_RsvdSecCnt = buffer[14] + (((uint16_t) buffer[15]) << 8);
	info->BPB_NumFATs = buffer[16];
	info->BPB_RootEntCnt = buffer[17] + (((uint16_t) buffer[18]) << 8);
	info->BPB_TotSec = buffer[19] + (((uint16_t) buffer[20]) << 8);
	if( info->BPB_TotSec == 0 )
		info->BPB_TotSec = buffer[32] +
			(((uint32_t) buffer[33]) << 8) +
			(((uint32_t) buffer[34]) << 16) +
			(((uint32_t) buffer[35]) << 24);
	info->BPB_Media = buffer[21];
	info->BPB_FATSz =  buffer[22] + (((uint16_t) buffer[23]) << 8);
	if( info->BPB_FATSz == 0 )
		info->BPB_FATSz = buffer[36] +
			(((uint32_t) buffer[37]) << 8) +
			(((uint32_t) buffer[38]) << 16) +
			(((uint32_t) buffer[39]) << 24);
	info->BPB_RootClus =  buffer[44] +
		(((uint32_t) buffer[45]) << 8) +
		(((uint32_t) buffer[46]) << 16) +
		(((uint32_t) buffer[47]) << 24);
	
	if( is_a_power_of_2( info->BPB_BytesPerSec ) != 0)
		ret += 1;
	if( is_a_power_of_2( info->BPB_SecPerClus ) != 0)
		ret += 2;
	if( info->BPB_BytesPerSec * info->BPB_SecPerClus > 32 * ((uint32_t) 1024) )
		ret += 4;
	if( info->BPB_NumFATs > 2 )
		ret += 8;
	if( info->BPB_TotSec == 0 )
		ret += 16;
	if( info->BPB_FATSz == 0 )
		ret += 32;
	if( buffer[510] != 0x55 || buffer[511] != 0xaa )
		ret += 64;
	printf("\nparse_bootsector() = %u", ret);
	return ret;
}

uint8_t fat_mount_device( struct diskio_device_info *dev ) {
	uint32_t RootDirSectors = 0;
	uint32_t dbg_file_cluster = 0;
	if (mounted.dev != 0) {
		fat_umount_device();
	}

	//read first sector into buffer
	diskio_read_block( dev, 0, sector_buffer );
	//parse bootsector
	if( parse_bootsector( sector_buffer, &(mounted.info) ) != 0 )
		return 1;
	//call determine_fat_type
	mounted.info.type = determine_fat_type( &(mounted.info) );
	//return 2 if unsupported
	if( mounted.info.type != FAT16 && mounted.info.type != FAT32 )
		return 2;

	mounted.dev = dev;

	//sync every FAT to the first on mount
	//fat_sync_fats();

	//Calculated the first_data_sector
	RootDirSectors = ((mounted.info.BPB_RootEntCnt * 32) + (mounted.info.BPB_BytesPerSec - 1)) / mounted.info.BPB_BytesPerSec;
	mounted.first_data_sector = mounted.info.BPB_RsvdSecCnt + (mounted.info.BPB_NumFATs * mounted.info.BPB_FATSz) + RootDirSectors;
	printf("\nfat_mount_device(): first_data_sector = %lu", mounted.first_data_sector );
	fat_read_block( mounted.first_data_sector );
	print_current_sector();
	printf("\nfat_mount_device(): Looking for \"Traum.txt\" = %lu", find_file_cluster("traum.txt") );
	printf("\nfat_mount_device(): Looking for \"prog1.txt\" = %lu", find_file_cluster("prog1.txt") );
	printf("\nfat_mount_device(): Looking for \"prog1.csv\" = %lu", dbg_file_cluster = find_file_cluster("prog1.csv") );
	printf("\nfat_mount_device(): Next cluster of \"prog1.csv\" = %lu", dbg_file_cluster = read_fat_entry(dbg_file_cluster * mounted.info.BPB_SecPerClus));
	printf("\nfat_mount_device(): Next cluster of \"prog1.csv\" = %lu", dbg_file_cluster = read_fat_entry(dbg_file_cluster * mounted.info.BPB_SecPerClus));
	printf("\nfat_mount_device(): Next cluster of \"prog1.csv\" = %lu", dbg_file_cluster = read_fat_entry(dbg_file_cluster * mounted.info.BPB_SecPerClus));
	return 0;
}

void fat_umount_device() {
	uint8_t i = 0;
	// Write last buffer
	fat_flush();
	// Write second FAT
	fat_sync_fats();
	// invalidate file-descriptors
	for(i = 0; i < FAT_FD_POOL_SIZE; i++) {
		fat_fd_pool[i].file = 0;
	}
	// Reset the device pointer
	mounted.dev = 0;
}

uint8_t fat_read_block( uint32_t sector_addr ) {
	if( sector_buffer_addr == sector_addr && sector_addr != 0 )
		return 0;
	fat_flush();
	sector_buffer_addr = sector_addr;
	return diskio_read_block( mounted.dev, sector_addr, sector_buffer );
}

uint32_t cluster2sector(uint32_t cluster_num) {
	return ((cluster_num - 2) * mounted.info.BPB_SecPerClus) + mounted.first_data_sector;
}

uint8_t is_EOC( uint32_t fat_entry ) {
	if( mounted.info.type == FAT16 ) {
		if( fat_entry >= 0xFFF8 )
			return 1;
	} else if( mounted.info.type == FAT32 ) {
		if( (fat_entry & 0x0FFFFFFF) >= 0x0FFFFFF8 )
			return 1;
	}
	return 0;
}

uint8_t fat_next_block() {
	if( sector_buffer_dirty )
		fat_flush();
	/* Are we on a Cluster edge? */
	if( (sector_buffer_addr + 1) % mounted.info.BPB_SecPerClus == 0 ) {
		uint32_t entry = read_fat_entry( sector_buffer_addr );
		printf("\nfat_next_block(): Cluster edge reached");
		if( is_EOC( entry ) )
			return 128;
		return fat_read_block( cluster2sector(entry) );
	} else {
		printf("\nfat_next_block(): read sector %lu", sector_buffer_addr + 1);
		return fat_read_block( sector_buffer_addr + 1 );
	}
}

uint8_t lookup( const char *name, struct dir_entry *dir_entry ) {
		uint16_t i = 0;
		for(;;) {
			for( i = 0; i < 512; i+=32 ) {
				printf("\nlookup(): name = %c%c%c%c%c%c%c%c%c%c%c", name[0], name[1], name[2], name[3], name[4], name[5], name[6], name[7], name[8], name[9], name[10]);
				printf("\nlookup(): sec_buf = %c%c%c%c%c%c%c%c%c%c%c", sector_buffer[i+0], sector_buffer[i+1], sector_buffer[i+2], sector_buffer[i+3], sector_buffer[i+4], sector_buffer[i+5], sector_buffer[i+6], sector_buffer[i+7], sector_buffer[i+8], sector_buffer[i+9], sector_buffer[i+10]);
				if( memcmp( name, &(sector_buffer[i]), 11 ) == 0 ) {
					memcpy( dir_entry, &(sector_buffer[i]), sizeof(struct dir_entry) );
					return 0;
				}
				// There are no more entries in this directory
				if( sector_buffer[i] == 0x00 ) {
					printf("\nlookup(): No more directory entries");
					return 1;
				}
			}
			if( fat_next_block() != 0 )
				return 2;
		}
}

uint32_t read_fat_entry_cluster( uint32_t cluster ) {
	return read_fat_entry( cluster * mounted.info.BPB_SecPerClus );
}

uint32_t read_fat_entry( uint32_t sec_addr ) {
	uint32_t fat_sec_num = 0, ent_offset = 0, ret = 0;;
	calc_fat_block( sec_addr, &fat_sec_num, &ent_offset );
	fat_read_block( fat_sec_num );
	if( mounted.info.type == FAT16 ) {
		ret = (((uint16_t) sector_buffer[ent_offset+1]) << 8) + ((uint16_t) sector_buffer[ent_offset]);
	} else if( mounted.info.type == FAT32 ) {
		ret = (((uint32_t) sector_buffer[ent_offset+3]) << 24) + (((uint32_t) sector_buffer[ent_offset+2]) << 16) + (((uint32_t) sector_buffer[ent_offset+1]) << 8) + ((uint32_t) sector_buffer[ent_offset+0]);
		ret &= 0x0FFFFFFF;
	}
	return ret;
}

void calc_fat_block( uint32_t cur_sec, uint32_t *fat_sec_num, uint32_t *ent_offset ) {
	uint32_t N = cur_sec / mounted.info.BPB_SecPerClus;
	if( mounted.info.type == FAT16 )
		*ent_offset = N * 2;
	else if( mounted.info.type == FAT32 )
		*ent_offset = N * 4;
	*fat_sec_num = mounted.info.BPB_RsvdSecCnt + (*ent_offset / mounted.info.BPB_BytesPerSec);
	*ent_offset = *ent_offset % mounted.info.BPB_BytesPerSec;
	//printf("\ncalc_fat_block(): fat_sec_num = %lu", *fat_sec_num);
	//printf("\ncalc_fat_block(): ent_offset = %lu", *ent_offset);
}

/**
 * \brief Looks through the FAT to find a free cluster.
 *
 * \TODO Check for end of FAT and no free clusters.
 * \return Returns the number of a free cluster.
 */
uint32_t get_free_cluster(uint32_t start_cluster) {
	uint32_t fat_sec_num = 0;
	uint32_t ent_offset = 0;
	uint16_t i = 0;
	calc_fat_block( start_cluster, &fat_sec_num, &ent_offset );
	do {
		fat_read_block( fat_sec_num );
		if( mounted.info.type == FAT16 )
			i = _get_free_cluster_16();
		else if( mounted.info.type == FAT32 )
			i = _get_free_cluster_32();
		fat_sec_num++;
	} while( i == 512 );
	ent_offset = (fat_sec_num - mounted.info.BPB_RsvdSecCnt) * mounted.info.BPB_BytesPerSec;
	if( mounted.info.type == FAT16 )
		ent_offset /= 2;
	else if( mounted.info.type == FAT32 )
		ent_offset /= 4;
	return ent_offset;
}

uint16_t _get_free_cluster_16() {
	uint16_t entry = 0;
	uint16_t i = 0;
	for( i = 0; i < 512; i += 2 ) {
		entry = (((uint16_t) sector_buffer[i]) << 8) + ((uint16_t) sector_buffer[i+1]);
		if( entry == 0 ) {
			return i;
		}
	}
	return 512;
}

uint16_t _get_free_cluster_32() {
	uint32_t entry = 0;
	uint16_t i = 0;
	for( i = 0; i < 512; i += 4 ) {
		entry = (((uint32_t) sector_buffer[i]) << 24) + (((uint32_t) sector_buffer[i+1]) << 16) + (((uint32_t) sector_buffer[i+2]) << 8) + ((uint32_t) sector_buffer[i+3]);
		if( (entry & 0x0FFFFFFF) == 0 ) {
			return i;
		}
	}
	return 512;
}

uint8_t _make_valid_name( const char *path, uint8_t start, uint8_t end, char *name ) {
	uint8_t i = 0, idx = 0, dot_found = 0;
	memset( name, 0x20, 11 );
	for(i = 0, idx = 0; i < end-start; ++i, ++idx) {
		// Part too long
		if( idx >= 11 )
			return 2;
		//ignore . but jump to last 3 chars of name
		if( path[start + i] == '.') {
			if( dot_found )
				return 3;
			idx = 7;
			dot_found = 1;
			continue;
		}
		if( !dot_found && idx > 7 )
			return 4;
		name[idx] = toupper(path[start + i]);
	}
	return 0;
}

uint8_t get_name_part( const char *path, char *name, uint8_t part_num ) {
	uint8_t start = 0, end = 0, idx = 0, i = 0, run = 0;
	for( run = 0, idx = 0; run < part_num; idx++ ) {
		if( path[idx] == '/' ) {
			run++;
		}
		if( path[idx] == '\0' )
			return 1;
	}
	start = end = idx;

	for( i = idx; path[i] != '\0'; i++ ){
		if( path[i] != '/' ) {
			end++;
		}
		if( path[end] == '/' || path[end] == '\0' ) {
			idx = _make_valid_name( path, start, end, name );
			printf("\nget_name_part(): _make_valid_name() = %u", idx);
			printf("\nDIR_Name = %c%c%c%c%c%c%c%c%c%c%c", name[0], name[1], name[2], name[3], name[4], name[5], name[6], name[7], name[8], name[9], name[10]);
			return idx;
			// return _make_valid_name( path, start, end, name );
		}
	}
	return 2;
}

void print_dir_entry( struct dir_entry *dir_entry ) {
	printf("\nDirectory Entry");
	printf("\n\tDIR_Name = %c%c%c%c%c%c%c%c%c%c%c", dir_entry->DIR_Name[0], dir_entry->DIR_Name[1],dir_entry->DIR_Name[2],dir_entry->DIR_Name[3],dir_entry->DIR_Name[4],dir_entry->DIR_Name[5],dir_entry->DIR_Name[6],dir_entry->DIR_Name[7],dir_entry->DIR_Name[8],dir_entry->DIR_Name[9],dir_entry->DIR_Name[10]);
	printf("\n\tDIR_Attr = %x", dir_entry->DIR_Attr);
	printf("\n\tDIR_NTRes = %x", dir_entry->DIR_NTRes);
	printf("\n\tCrtTimeTenth = %x", dir_entry->CrtTimeTenth);
	printf("\n\tDIR_CrtTime = %x", dir_entry->DIR_CrtTime);
	printf("\n\tDIR_CrtDate = %x", dir_entry->DIR_CrtDate);
	printf("\n\tDIR_LstAccessDate = %x", dir_entry->DIR_LstAccessDate);
	printf("\n\tDIR_FstClusHI = %x", dir_entry->DIR_FstClusHI);
	printf("\n\tDIR_WrtTime = %x", dir_entry->DIR_WrtTime);
	printf("\n\tDIR_WrtDate = %x", dir_entry->DIR_WrtDate);
	printf("\n\tDIR_FstClusLO = %x", dir_entry->DIR_FstClusLO);
	printf("\n\tDIR_FileSize = %lu Bytes", dir_entry->DIR_FileSize);
}

uint32_t find_file_cluster( const char *path ) {
	uint32_t first_root_dir_sec_num = 0;
	uint32_t file_sector_num = 0;
	struct dir_entry dir_ent;
	char name[11] = "\0";
	uint8_t i = 0;
	if( mounted.info.type == FAT16 ) {
		// calculate the first cluster of the root dir
		first_root_dir_sec_num = mounted.info.BPB_RsvdSecCnt + (mounted.info.BPB_NumFATs * mounted.info.BPB_FATSz); // TODO Verify this is correct
	} else if( mounted.info.type == FAT32 ) {
		// BPB_RootClus is the first cluster of the root dir
		first_root_dir_sec_num = cluster2sector( mounted.info.BPB_RootClus );
	}
	file_sector_num = first_root_dir_sec_num;
	for(i = 0; get_name_part( path, name, i ) == 0; i++) {
		fat_read_block( file_sector_num );
		if( lookup( name, &dir_ent ) != 0 ) {
			return 0;
		}
		file_sector_num = cluster2sector( dir_ent.DIR_FstClusLO + (((uint32_t) dir_ent.DIR_FstClusHI) << 16) );
		print_dir_entry( &dir_ent );
	}
	if( file_sector_num == first_root_dir_sec_num )
		return 0;
	return dir_ent.DIR_FstClusLO + (((uint32_t) dir_ent.DIR_FstClusHI) << 16);
}

uint32_t find_nth_cluster( uint32_t start_cluster, uint32_t n ) {
	uint32_t cluster = start_cluster, i = 0;
	for( i = 0; i < n; i++ ) {
		cluster = read_fat_entry_cluster( cluster );
		if( is_EOC( cluster ) )
			return 0;
	}
	return cluster;
}

uint8_t fat_read_file( int fd, uint32_t clusters, uint32_t clus_offset ) {
	/*Read the clus_offset Sector of the clusters-th cluster of the file fd*/
	uint32_t cluster = find_nth_cluster( fat_file_pool[fd].cluster, clusters );
	//printf("\nfat_read_file(): cluster = %lu", cluster);
	//printf("\nfat_read_file(): sector = %lu", cluster2sector(cluster) + clus_offset);
	if( cluster == 0 )
		return 1;
	return fat_read_block( cluster2sector(cluster) + clus_offset );
}

int
cfs_open(const char *name, int flags)
{
	// get FileDescriptor
	int fd = -1;
	uint8_t i = 0;
	for( i = 0; i < FAT_FD_POOL_SIZE; i++ ) {
		if( fat_fd_pool[i].file == 0 ) {
			fd = i;
			break;
		}
	}
	/*No free FileDesciptors available*/
	if( fd == -1 )
		return fd;
	// find file on Disk
	fat_file_pool[fd].cluster = find_file_cluster( name );
	if( fat_file_pool[fd].cluster == 0 )
		return -1;
	fat_fd_pool[fd].file = &(fat_file_pool[fd]);
	fat_fd_pool[fd].flags = (uint8_t) flags;
	// put read/write position in the right spot
	fat_fd_pool[fd].offset = 0;
	// return FileDescriptor
	return fd;
}

/*
void
cfs_close(int fd)
{
	if( fd < 0 || fd >= FAT_FD_POOL_SIZE )
		return;
	fat_flush( fd );
	fat_fd_pool[fd].file = NULL;
}
*/

int
cfs_read(int fd, void *buf, unsigned int len)
{
	uint32_t offset = fat_fd_pool[fd].offset % mounted.info.BPB_BytesPerSec;
	uint32_t clusters = (fat_fd_pool[fd].offset / mounted.info.BPB_BytesPerSec) / mounted.info.BPB_SecPerClus;
	uint8_t clus_offset = (fat_fd_pool[fd].offset / mounted.info.BPB_BytesPerSec) % mounted.info.BPB_SecPerClus;
	uint16_t i, j = 0;
	uint8_t *buffer = (uint8_t *) buf;
	while( fat_read_file( fd, clusters, clus_offset ) == 0 ) {
		for( i = offset; i < 512 && j < len; i++,j++,fat_fd_pool[fd].offset++ ) {
			buffer[j] = sector_buffer[i];
		}
		if( (clus_offset + 1) % mounted.info.BPB_SecPerClus == 0 ) {
			clus_offset = 0;
			clusters++;
		} else {
			clus_offset++;
		}
		if( j >= len )
			break;
	}
	return 0;
}
/*
int
cfs_write(int fd, const void *buf, unsigned int len)
{
}

cfs_offset_t
cfs_seek(int fd, cfs_offset_t offset, int whence)
{
}

int
cfs_remove(const char *name)
{
}

int
cfs_opendir(struct cfs_dir *dirp, const char *name)
{
}

int
cfs_readdir(struct cfs_dir *dirp, struct cfs_dirent *dirent)
{
}

void
cfs_closedir(cfs_dir *dirp)
{
}
*/
/**
 * Tests if the given value is a power of 2.
 *
 * \param value Number which should be testet if it is a power of 2.
 * \return 1 on failure and 0 if value is a power of 2.
 */
uint8_t is_a_power_of_2( uint32_t value ) {
	uint32_t test = 1;
	uint8_t i = 0;
	if( value == 0 )
		return 0;
	for( i = 0; i < 32; ++i ) {
		if( test == value )
			return 0;
		test = test << 1;
	}
	return 1;
}

/**
 * Rounds the value down to the next lower power of 2.
 *
 * \param value The number which should be rounded down.
 * \return the next lower number which is a power of 2
 */
uint32_t round_down_to_power_of_2( uint32_t value ) {
	uint32_t po2 = ((uint32_t) 1) << 31;
	while( value < po2 ) {
		po2 = po2 >> 1;
	}
	return po2;
}
