/* CSC 4103 Filesystems Project 3
 * Code by:
 * Joshua McCain
 * Steven Seiden
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "filesystem.h"
#include "softwaredisk.h"


//------------DEFINE STATEMENTS------------

/* The max file name size is 256, but because we need to pad the DirEntry struct to have memory that is divisible
 * into the 4096 byte block size limit, we use this number to pad. Since the size of DirEntry with 256 would be
 * 259 bytes given the extra 2 fields, the next best multiple dividing into 4096 would be 512 bytes, hence
 * 512 - (3 bytes given by 2 extra fields) = 509 as the "max" file size 
 */
#define MAX_FILENAME_SIZE 509

/* These define statements are for determining how many blocks are allocated to inodes/dentries based on # files
 * permitted on the system.
 *
 * SETUP_BLOCKS is the cutoff for the amount of blocks used to support the file system.
 * ***IMPORTANT*** since the disk starts addressing at 0, setup blocks is actually the address of the first
 * block available for user input.
 */
#define MAX_FILES_SUPPORTED 512
#define NUM_INODE_IN_BLOCK 128 //(SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)) // 4096 / 32 = *128*
#define NUM_DENTRY_IN_BLOCK 8 //(SOFTWARE_DISK_BLOCK_SIZE / sizeof(DirEntry)) // 4096 / 512 = *8*

#define INODE_BLOCKS 4 //ceil(MAX_FILES_SUPPORTED / NUM_INODE_IN_BLOCK) // 512 / 128 = *4*
#define DENTRY_BLOCKS 64 //ceil(MAX_FILES_SUPPORTED / NUM_DENTRY_IN_BLOCK) // 512 / 8 = *64*
#define SETUP_BLOCKS 70 //(2 + INODE_BLOCKS + DENTRY_BLOCKS) // 70
#define NUM_USER_BLOCKS 4026 //(software_disk_size() - SETUP_BLOCKS) // 4096 - 70 = *4026*

//The following values start from 0 blocks on the disk, with the values shown being the start
//and final block of that section, inclusive on both.
//
//BITMAP_START is the inode bitmap, BITMAP_END is the user space block bitmap
#define BITMAP_START 0
#define BITMAP_END 1
#define INODE_START 2 //(BITMAP_END + 1) // 2
#define INODE_END 5 //(INODE_START + INODE_BLOCKS) - 1 // 2 + 4 = *5*
#define DENTRY_START 6 //(INODE_END + 1) // 6
#define DENTRY_END 69 //(DENTRY_START + DENTRY_BLOCKS) - 1 // 6 + 64 - 1 = *69*
#define USER_START 70 //(DENTRY_END + 1) // 70, note how it matches MAX_SETUP_BLOCKS
#define USER_END (software_disk_size() - 1)

//Update with caution, this value must keep Inode struct's size a multiple of 4096, which is the size of a block.
#define NUM_DENTRIES_IN_INODE 13

//Helper functions for finding the inode/dentry in memory given its bitmap "address"
#define BLOCK_OF_INODE(b) (b / NUM_INODE_IN_BLOCK + INODE_START)
#define ADDRESS_OF_INODE(b) (b % NUM_INODE_IN_BLOCK)
#define BLOCK_OF_DENTRY(b) (b / NUM_DENTRY_IN_BLOCK + DENTRY_START)
#define ADDRESS_OF_DENTRY(b) (b % NUM_DENTRY_IN_BLOCK)

#define DIRECT_ENTRY_INODE_ADDRESS(b) (b / SOFTWARE_DISK_BLOCK_SIZE)
#define DIRECT_ENTRY_INODE_WRITEPOS(b) (b % SOFTWARE_DISK_BLOCK_SIZE)

#define MAX_NUM_DENTRY_IN_INODE (NUM_DENTRIES_IN_INODE + (SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint16_t)))

//Bitmap constants that are used in below functions. Credit for these constants found below in bitmaps functions sect.
typedef uint32_t word_t;
enum { BITS_PER_WORD = sizeof(word_t) * CHAR_BIT };
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)

/* Holds the structure for the two bitmaps we need for the filesystem, 1 keeping blocks used and another
 * inodes/dir entries used, since both inodes and dir entries map to each other 1 to 1
 * Bitmap functions are defined below to help with operations on the map.
 */
typedef struct _BitmapBlock {
    word_t map[SOFTWARE_DISK_BLOCK_SIZE / sizeof(word_t)];
} BitmapBlock;


//------------STRUCTURES------------

/* Struct of directory entries meant for finding inodes and keeping track of the file being open
 * This struct is compiled together in a block by the DirEntryBlock struct. Holds 512 bytes for each entry.
 *
 * ***Credit to Dr. Golden for this struct*** 
 */
typedef struct __attribute__((packed)) _DirEntry {
	uint8_t file_open;
	uint16_t inode_idx;
	char file_name[MAX_FILENAME_SIZE];
} DirEntry;

/* Because the DirEntry struct is made to be a multiple of the block size, this struct can evenly hold a number of
 * DirEntries. This block should hold 8 dentries if the size of 1 is 512 bytes.
 */
typedef struct _DirEntryBlock {
	DirEntry dentries[NUM_DENTRY_IN_BLOCK];
} DirEntryBlock;

//Size of the inode is 32 bytes, important to keep 4096 divisible by the size
typedef struct _Inode {
	uint32_t file_size;
	uint16_t dir_blocks[NUM_DENTRIES_IN_INODE + 1]; //Holds enough for dir entries and 1 indir entry which will be
							//at the back of this array
} Inode;

//This struct should be able to hold 128 inodes if the size of the inode stays at size 32 bytes.
typedef struct _InodeBlock {
	Inode inodes[NUM_INODE_IN_BLOCK];
} InodeBlock;

/* When an inode references its indirect entry address, this block is what it will point to. It holds all the direct
 * block addresses just like an inode would but as an entire block
 */
typedef struct _IndirEntryBlock {
	uint16_t dir_blocks[SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint16_t)];
} IndirEntryBlock;



/* This block is for continuity purposes so we know what a user data block looks like in human readable terms.
 * The page consists of chars going up to the block size since 1 char = 1 byte.
 */
typedef struct _UserDataBlock {
	char page[SOFTWARE_DISK_BLOCK_SIZE];
} UserDataBlock;

/* Built to flesh out the File type of the filesystem.h file. This struct is returned to the user for easy
 * access to their file in memory.
 */
struct FileInternals {
	char file_name[MAX_FILENAME_SIZE];
	uint32_t current_pos;
	FileMode mode;
	Inode inode;
	DirEntry dentry;
};


//------------FUNCTION PROTOTYPES------------

int find_empty_inode(BitmapBlock *bblock);
void set_inode_bit(BitmapBlock *bblock, int n);
void clear_inode_bit(BitmapBlock *bblock, int n);
void write_dentry_to_disk(File file);
void write_inode_to_disk(File file);
uint16_t find_empty_user_block(void);
void set_user_bit(BitmapBlock *bblock, int n);
void clear_user_bit(BitmapBlock *bblock, int n);
int find_file(char *name);
int give_inode_new_address(File file);
uint16_t update_to_cur_user_block(File file, UserDataBlock *user_block, IndirEntryBlock *indir_block);
void update_max_file_size(File file);
void set_bit(BitmapBlock *bblock, int n);
void clear_bit(BitmapBlock *bblock, int n);
int get_bit(BitmapBlock *bblock, int n);


//------------GLOBALS------------

FSError fserror;

//------------FILESYSTEM FUNCTIONS------------

File open_file(char *name, FileMode mode){
	fserror = FS_NONE;

	//Find file of a specific name in the system
	int dentry_pos = find_file(name);

	if(dentry_pos == -1){
		fserror = FS_FILE_NOT_FOUND;
		return NULL;
	}
	
	//Get dentries/inode and check if the file is already open
	DirEntryBlock dentry_block;
	read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(dentry_pos));
	DirEntry* new_dentry = &((dentry_block.dentries)[ADDRESS_OF_DENTRY(dentry_pos)]);

	InodeBlock inode_block;
	read_sd_block(inode_block.inodes, BLOCK_OF_INODE(new_dentry->inode_idx));
	Inode* new_inode = &((inode_block.inodes)[ADDRESS_OF_INODE(new_dentry->inode_idx)]);

	if(new_dentry->file_open){
		fserror = FS_FILE_OPEN;
		return NULL;
	}

	//Make open changes to the dentry and write to disk
	new_dentry->file_open = 1;
	write_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(dentry_pos));

	//Make new File for the user and return
	File ret_file;
	ret_file->current_pos = 0;
	strcpy(ret_file->file_name, name);
	ret_file->mode = mode;
	memcpy(&(ret_file->inode), new_inode, sizeof(Inode));
	memcpy(&(ret_file->dentry), new_dentry, sizeof(DirEntry));

	return ret_file;
}


File create_file(char *name){
	fserror = FS_NONE;

	//Check for name being incorrect number of characters or begins with null
	//The minus 1 is included to account for the null terminating character filling the last slot in the array
	if(name[0] == '\0' || strlen(name) > MAX_FILENAME_SIZE - 1){
		fserror = FS_ILLEGAL_FILENAME;
		return NULL;
	}

	//Check if file already exists
	if(file_exists(name)){
		fserror = FS_FILE_ALREADY_EXISTS;
		return NULL;
	}

	//Initial reading of blocks
	BitmapBlock inode_bits;
	read_sd_block(inode_bits.map, BITMAP_START);

	//Find empty to use
	int inode_pos = find_empty_inode(&inode_bits);
	if (inode_pos == -1){
			fserror = FS_OUT_OF_SPACE;
			return NULL;
	}

	//With empty address, update dentry with its name, idx, and show the file slot as filled in the bitmap
	set_inode_bit(&inode_bits, inode_pos);

	DirEntryBlock dentry_block;
	read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(inode_pos));
	DirEntry* new_dentry = &((dentry_block.dentries)[ADDRESS_OF_DENTRY(inode_pos)]);

	strcpy(new_dentry->file_name, name);
	//***IMPORTANT***, not sure if this compression of int to 16 bit will cause problems.
	new_dentry->inode_idx = (uint16_t) inode_pos;

	//Write new dentry data to the software disk
	write_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(inode_pos));

	//Call the open file method
        File ret_file = open_file(name, READ_WRITE);

	//Return the File
	return ret_file;
}

void close_file(File file){
	fserror = FS_NONE;

	if(file_exists(file->file_name)){

		if((file->dentry).file_open){
			(file->dentry).file_open = 0;
			write_dentry_to_disk(file);
		}
		else{
			fserror = FS_FILE_NOT_OPEN;
		}
	}
	else{
		fserror = FS_FILE_NOT_FOUND;
	}
}

unsigned long read_file(File file, void *buf, unsigned long numbytes){
	fserror = FS_NONE;

	if(!((file->dentry).file_open)){
		fserror = FS_FILE_NOT_OPEN;
		return 0;
	}

	if(!file_exists(file->file_name)){
		fserror = FS_FILE_NOT_FOUND;
		return 0;
	}

	//Get initial user block to use
	UserDataBlock user_block;
	IndirEntryBlock indir_block;
	uint16_t cur_address = update_to_cur_user_block(file, &user_block, &indir_block);

	for(int i = 0; i < numbytes; i++){
		
		//Take into account running out of page space, aka we have moved to a new page here
		if((file->current_pos) % SOFTWARE_DISK_BLOCK_SIZE == 0){

			//Make sure we don't go over the space allocated for file use, similar to seek file check
			if(file->current_pos >= (MAX_NUM_DENTRY_IN_INODE * SOFTWARE_DISK_BLOCK_SIZE)){
				//Update maximum file position
				if(file->current_pos > (file->inode).file_size){
					(file->inode).file_size = file->current_pos;
					write_inode_to_disk(file);
				}

				fserror = FS_EXCEEDS_MAX_FILE_SIZE;
				return i;
			}

			cur_address = update_to_cur_user_block(file, &user_block, &indir_block);
		}

		//Read the character in position
		((char *) buf)[i] = (user_block.page)[(file->current_pos) % SOFTWARE_DISK_BLOCK_SIZE];

		//Increment reading position
		file->current_pos += 1;
	}
}

unsigned long write_file(File file, void *buf, unsigned long numbytes){
	fserror = FS_NONE;

	//Test for things that would not allow us to write
	if(!((file->dentry).file_open)){
		fserror = FS_FILE_NOT_OPEN;
		return 0;
	}

	if(!file_exists(file->file_name)){
		fserror = FS_FILE_NOT_FOUND;
		return 0;
	}
	
	if(file->mode == READ_ONLY){
		fserror = FS_FILE_READ_ONLY;
		return 0;
	}

	char *new_buf = (char *) buf;

	//give the memory address we need to write to an address for the block it will use.
	if(!give_inode_new_address(file)){
		fserror = FS_OUT_OF_SPACE;
		return 0;
	}

	//Get initial user block to use
	UserDataBlock user_block;
	IndirEntryBlock indir_block;
	uint16_t cur_address = update_to_cur_user_block(file, &user_block, &indir_block);

	//Start writing
	for(int i = 0; i < numbytes; i++){

		//Take into account running out of page space, aka we have moved to a new page here
		if((file->current_pos) % SOFTWARE_DISK_BLOCK_SIZE == 0){
			write_sd_block(user_block.page, cur_address);

			//Make sure we don't go over the space allocated for file use, similar to seek file check
			if(file->current_pos >= (MAX_NUM_DENTRY_IN_INODE * SOFTWARE_DISK_BLOCK_SIZE)){
				//Update maximum file position
				update_max_file_size(file);

				fserror = FS_EXCEEDS_MAX_FILE_SIZE;
				return i;
			}

			if(!give_inode_new_address(file)){
				//Update maximum file position
				update_max_file_size(file);

				fserror = FS_OUT_OF_SPACE;
				return i;
			}

			cur_address = update_to_cur_user_block(file, &user_block, &indir_block);
		}

		//Write the character in position
		(user_block.page)[(file->current_pos) % SOFTWARE_DISK_BLOCK_SIZE] = new_buf[i];

		//Increment writing position
		file->current_pos += 1;
	}

	//Final update to the disk
	write_sd_block(user_block.page, cur_address);

	//Update maximum file position
	update_max_file_size(file);

	return numbytes;
}

int seek_file(File file, unsigned long bytepos){
	fserror = FS_NONE;

	if(bytepos >= (MAX_NUM_DENTRY_IN_INODE * SOFTWARE_DISK_BLOCK_SIZE)){
		fserror = FS_EXCEEDS_MAX_FILE_SIZE;
		return 0;
	}

	//if the position would put the cursor into indir block space, allocate an indir block
	if(bytepos >= (NUM_DENTRIES_IN_INODE * SOFTWARE_DISK_BLOCK_SIZE)){
		uint16_t block_address = find_empty_user_block();
		if(block_address == -1){
			fserror = FS_OUT_OF_SPACE;
			return 0;
		}
		else{
			((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE] = block_address;
			write_inode_to_disk(file);
		}
	}

	update_max_file_size(file);

	file->current_pos = (uint32_t) bytepos;
}

unsigned long file_length(File file){
	fserror = FS_NONE;

	if(!file_exists(file->file_name)){
		fserror = FS_FILE_NOT_FOUND;
		return 0;
	}

	return (file->inode).file_size;
}

int delete_file(char *name){
	fserror = FS_NONE;

    //Check to make sure the file exists
    if(!file_exists(name)){
        fserror = FS_FILE_NOT_FOUND;
        return 0;
    }


	//Get all info related to file
	BitmapBlock inode_bits;
    read_sd_block(inode_bits.map, BITMAP_START);

	BitmapBlock user_bits;
	read_sd_block(user_bits.map, BITMAP_END);

	int dentry_pos = find_file(name);

	DirEntryBlock dentry_block;
    read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(dentry_pos));
    DirEntry* new_dentry = &((dentry_block.dentries)[ADDRESS_OF_DENTRY(dentry_pos)]);

	InodeBlock inode_block;
	read_sd_block(inode_block.inodes, BLOCK_OF_INODE(new_dentry->inode_idx));
	Inode* new_inode = &((inode_block.inodes)[ADDRESS_OF_INODE(new_dentry->inode_idx)]);


	//Check for open files
	if(new_dentry->file_open){
		fserror = FS_FILE_OPEN;
		return 0;
	}


	//Delete bitmap data for all inode mem addresses
	//Direct addresses
	for(int i = 0; i < NUM_DENTRIES_IN_INODE; i++){
        if((new_inode->dir_blocks)[i] != 0){
            clear_bit(&user_bits, ((new_inode->dir_blocks)[i]) - USER_START);
        }
    }

    //Indirect addresses, but only if necessary
	if((new_inode->dir_blocks)[NUM_DENTRIES_IN_INODE] != 0){
		//Grab block
    	IndirEntryBlock indir_block;
    	read_sd_block(indir_block.dir_blocks, (new_inode->dir_blocks)[NUM_DENTRIES_IN_INODE]);
    	
		//Loop through its direct mem addresses, unlinking from the bitmap
		for(int i = 0; i < SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint16_t); i++){
			if((indir_block.dir_blocks)[i] != 0){
				clear_bit(&user_bits, ((indir_block.dir_blocks)[i]) - USER_START);
			}
		}

		//Unlink the indir mem address itself
		clear_bit(&user_bits, ((new_inode->dir_blocks)[NUM_DENTRIES_IN_INODE]) - USER_START);
	}


	//Data inside inode is now clear, good to clear the inode
	bzero(new_inode, sizeof(Inode));

	//Don't need data inside dentry either
	bzero(new_dentry, sizeof(DirEntry));


	//Now that we have "deleted" all associated meta data with the file, we clear the inode bit
	clear_bit(&inode_bits, dentry_pos);


    //Everything is cleared, write the changes to the disk
	write_sd_block(inode_bits.map, BITMAP_START);
	write_sd_block(user_bits.map, BITMAP_END);
	write_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(dentry_pos));
	write_sd_block(inode_block.inodes, BLOCK_OF_INODE(dentry_pos));

	return 1;
}

/* Similar to the support function find_file, except, instead of returning the address of the dentry,
 * This will return 1 if it exits and 0 if not.
 * Function is mostly here for program3 completion purposes.
 */
int file_exists(char *name){
	BitmapBlock inode_bits;
	read_sd_block(inode_bits.map, BITMAP_START);

	DirEntryBlock dentry_block;
	int i = 0;
	fserror = FS_FILE_NOT_FOUND;
	int file_found = 0;
	while (i < MAX_FILES_SUPPORTED){
		//Aka, we hit a new block of dentries
		if(ADDRESS_OF_DENTRY(i) == 0){
			read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(i));
		}

		//If the current position is occupied by a file with the name we are attempting to match
		if(get_bit(&inode_bits, i) && !strcmp(((dentry_block.dentries)[ADDRESS_OF_DENTRY(i)]).file_name, name)) {
			//i is set to the right dir entry, and the block is set correctly. Exit the loop.
			fserror = FS_NONE;
			file_found = 1;
			break;
		}
		i++;
	}
	
	return file_found;
}

void fs_print_error(void){
	switch (fserror) {
  		case FS_NONE:
    		printf("FS: No error.\n");
    		break;
  		case FS_OUT_OF_SPACE:
    		printf("FS: Software disk at full capacity, possibly did not complete operation.\n");
    		break;
  		case FS_FILE_NOT_OPEN:
 			printf("FS: Operation requires an open file and was given a close file.\n");
    		break;
  		case FS_FILE_OPEN:
    		printf("FS: Operation requires a closed file and was given an open file.\n");
    		break;
		case FS_FILE_NOT_FOUND:
    		printf("FS: File does not exist, did you delete this file recently?.\n");
    		break;
		case FS_FILE_READ_ONLY:
    		printf("FS: Cannot write to a file whose current mode is read only.\n");
    		break;
		case FS_FILE_ALREADY_EXISTS:
    		printf("FS: Cannot create a file with the name matching an existing file.\n");
    		break;
		case FS_EXCEEDS_MAX_FILE_SIZE:
    		printf("FS: Cannot go beyond the max file size.\n");
    		break;
		case FS_ILLEGAL_FILENAME:
    		printf("FS: Cannot have empty file name or one that goes beyond 512 characters.\n");
    		break;
  		default:
    		printf("FS: Unknown error code %d.\n", sderror);
  	}
}

int check_structure_alignment(void){
    printf("Expecting sizeof(Inode) = 32, actual = %lu\n",sizeof(Inode));
    printf("Expecting sizeof(IndirEntryBlock) = %d, actual %lu\n", SOFTWARE_DISK_BLOCK_SIZE, sizeof(IndirEntryBlock));
    printf("Expecting sizeof(InodeBlock) = %d, actual = %lu\n", SOFTWARE_DISK_BLOCK_SIZE, sizeof(InodeBlock));
    printf("Expecting sizeof(DirEntry) = 512, actual = %lu\n", sizeof(DirEntry));
    printf("Expecting sizeof(DirEntryBlock) = %d, actual = %lu\n", SOFTWARE_DISK_BLOCK_SIZE, sizeof(DirEntryBlock));
    printf("Expecting sizeof(BitmapBlock) = %d, actual = %lu\n", SOFTWARE_DISK_BLOCK_SIZE, sizeof(BitmapBlock));

    if(sizeof(Inode) != 32 || sizeof(IndirEntryBlock) != SOFTWARE_DISK_BLOCK_SIZE || sizeof(InodeBlock) != SOFTWARE_DISK_BLOCK_SIZE || sizeof(DirEntry) != 512 || sizeof(DirEntryBlock) != SOFTWARE_DISK_BLOCK_SIZE || sizeof(BitmapBlock) != SOFTWARE_DISK_BLOCK_SIZE){
        return 0;
    } else {
        return 1;
    }

}

//------------SUPPORT FUNCTIONS------------

/* Finds the first bit that is 0 in the inode bitmap before the # files max and returns its address.
 * A return value of -1 means failure, which is caused by a 0 bit not being found.
 */
int find_empty_inode(BitmapBlock *bblock){
	int i = 0;
	while (get_bit(bblock, i)){
		i++;
		if(i == MAX_FILES_SUPPORTED){
			i = -1;
			break;
		}
	}
	return i;
}

void set_inode_bit(BitmapBlock *bblock, int n){
	set_bit(bblock, n);
	write_sd_block(bblock->map, BITMAP_START);
}

void clear_inode_bit(BitmapBlock *bblock, int n){
	clear_bit(bblock, n);
	write_sd_block(bblock->map, BITMAP_START);
}

/* The next functions are meant to take the dentry or the inode inside a File and write it to the disk
 */
void write_dentry_to_disk(File file){
	DirEntryBlock dentry_block;
	read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY((file->dentry).inode_idx));
	DirEntry* write_to_dentry = &((dentry_block.dentries)[ADDRESS_OF_DENTRY((file->dentry).inode_idx)]);
	
	memcpy(write_to_dentry, &(file->dentry), sizeof(DirEntry));

	write_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY((file->dentry).inode_idx));
}

void write_inode_to_disk(File file){
	InodeBlock inode_block;
	read_sd_block(inode_block.inodes, BLOCK_OF_INODE((file->dentry).inode_idx));
	Inode* write_to_inode = &((inode_block.inodes)[ADDRESS_OF_INODE((file->dentry).inode_idx)]);
	
	memcpy(write_to_inode, &(file->inode), sizeof(Inode));

	write_sd_block(inode_block.inodes, BLOCK_OF_INODE((file->dentry).inode_idx));
}

/* Finds the first bit that is 0 in the user blocks bitmap before the # user blocks allowed and returns its address.
 * The address is given as the real block address on the software disk
 * A return value of -1 means failure, which is caused by a 0 bit not being found.
 * If the function does not fail, it zeros the user block that is about to be used
 * and sets the bit in bitmap to 1 (in use)
 */
uint16_t find_empty_user_block(void){
	BitmapBlock user_bits;
	read_sd_block(user_bits.map, BITMAP_END);
	int i = 0;
	while (get_bit(&user_bits, i)){
		i++;
		if(i == NUM_USER_BLOCKS){
			return -1;
		}
	}

	//Clear the user space so we know it is clear (easier to do here than in delete_file)
	UserDataBlock user_block;
	bzero(user_block.page, sizeof(user_block.page));
	write_sd_block(user_block.page, i + USER_START);

	set_user_bit(&user_bits, i);

	return i + USER_START;
}

void set_user_bit(BitmapBlock *bblock, int n){
	set_bit(bblock, n);
	write_sd_block(bblock->map, BITMAP_END);
}

void clear_user_bit(BitmapBlock *bblock, int n){
	clear_bit(bblock, n);
	write_sd_block(bblock->map, BITMAP_END);
}

/* Finds if the software disk has a file with the name argument as its file name.
 * Returns the bitmap address if it is found, -1 if not found.
 */
int find_file(char *name){
	BitmapBlock inode_bits;
	read_sd_block(inode_bits.map, BITMAP_START);

	DirEntryBlock dentry_block;
	int i = 0;
	while (i < MAX_FILES_SUPPORTED){
		//Aka, we hit a new block of dentries
		if(ADDRESS_OF_DENTRY(i) == 0){
			read_sd_block(dentry_block.dentries, BLOCK_OF_DENTRY(i));
		}
		//If the current position is occupied by a file with the name we are attempting to match
		if(get_bit(&inode_bits, i) && !strcmp(((dentry_block.dentries)[ADDRESS_OF_DENTRY(i)]).file_name, name)) {
			//i is set to the right dir entry, and the block is set correctly. Exit the loop.
			break;
		}
		i++;
	}

	//File not found
	if(i == MAX_FILES_SUPPORTED)
		i = -1;
	
	return i;
}

/* Given the current position of the cursor, if there is no mem address, give the inode the address it needs
 * Updates the inode to the software disk if it is written to.
 *
 * Returns 0 on failure, 1 if the address was assigned successfully.
 * 
 * ***IMPORTANT*** tbh, not too confident in this code. may need overhaul.
 */
int give_inode_new_address(File file){

	//Using the current file position, find the memory block in the inode we must write to
	if(DIRECT_ENTRY_INODE_ADDRESS(file->current_pos) < NUM_DENTRIES_IN_INODE){

		if(((file->inode).dir_blocks)[DIRECT_ENTRY_INODE_ADDRESS(file->current_pos)] == 0){ //if no address
			uint16_t block_address = find_empty_user_block();
			if(block_address == -1){
				return 0; //out of space
			}
			else{
				((file->inode).dir_blocks)[DIRECT_ENTRY_INODE_ADDRESS(file->current_pos)] = block_address;
				write_inode_to_disk(file);
			}
		}
	}
	else{
		
		if(((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE] == 0){ //No address found for indir block
			uint16_t block_address = find_empty_user_block();
			if(block_address == -1){
				return 0; //out of space
			}
			else{
				((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE] = block_address;
				write_inode_to_disk(file);
			}
		}
		
		IndirEntryBlock indirBlock;
		read_sd_block(indirBlock.dir_blocks, ((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE]);
		uint16_t *dir_address = &((indirBlock.dir_blocks)[DIRECT_ENTRY_INODE_ADDRESS(file->current_pos) - NUM_DENTRIES_IN_INODE]);

		if(*dir_address == 0){ //No address found inside indir block
			uint16_t block_address = find_empty_user_block();
			if(block_address == -1){
				return 0; //out of space
			}
			else{
				*dir_address = block_address;
				write_sd_block(indirBlock.dir_blocks, ((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE]);
			}
		}
	}

	return 1;
}

/* Takes in the current File with pointers to predefined user data block and indir entry block.
 * Updates the values inside user data block and indir entry block to be reflection of the disk.
 * In the case that the position does not have an address (it is 0), an array of 0s is returned in the user block
 *
 * Returns the address of the user block being worked on in disk so it can be written to later.
 */
uint16_t update_to_cur_user_block(File file, UserDataBlock *user_block, IndirEntryBlock *indir_block){
	
	uint16_t cur_address;
	//***IMPORTANT*** not sure if this is what he means by a clear array of chars to read from
	memset(indir_block->dir_blocks, 0, SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint16_t));

	if(DIRECT_ENTRY_INODE_ADDRESS(file->current_pos) < NUM_DENTRIES_IN_INODE){

		cur_address = ((file->inode).dir_blocks)[DIRECT_ENTRY_INODE_ADDRESS(file->current_pos)];
		if(cur_address != 0){
			read_sd_block(user_block->page, cur_address);
		}
		else{
			//***IMPORTANT*** not sure if this is what he means by a clear array of chars to read from
			memset(user_block->page, 0, SOFTWARE_DISK_BLOCK_SIZE);
		}
	}
	else{
		if(((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE] != 0){
			read_sd_block(indir_block->dir_blocks, ((file->inode).dir_blocks)[NUM_DENTRIES_IN_INODE]);

			cur_address = (indir_block->dir_blocks)[DIRECT_ENTRY_INODE_ADDRESS(file->current_pos) - NUM_DENTRIES_IN_INODE];
			if(cur_address != 0){
				read_sd_block(user_block->page, cur_address);
			}
			else{
				//***IMPORTANT*** not sure if this is what he means by a clear array of chars to read from
				memset(user_block->page, 0, SOFTWARE_DISK_BLOCK_SIZE);
			}
		}
		else{
			//***IMPORTANT*** not sure if this is what he means by a clear array of chars to read from
			memset(user_block->page, 0, SOFTWARE_DISK_BLOCK_SIZE);
		}
	}

	return cur_address;
}

void update_max_file_size(File file){
	if(file->current_pos > (file->inode).file_size){
		(file->inode).file_size = file->current_pos;
		write_inode_to_disk(file);
	}
}


//------------BITMAP OPERATIONS------------
//***CREDIT TO https://stackoverflow.com/questions/1225998/what-is-a-bitmap-in-c for the explanation and code

/* Given a pointer to the bitmap block operating on and the block to access, turn that bit to 1
 * 
 * Basically, the | (or) bitwise operation will transfer bits not to be changed to the new bitstring.
 * The only bit that will possibly change is where a 1 is, which the shift does by putting the 1 in the correct bit spot
 */
void set_bit(BitmapBlock *bblock, int n){
	(bblock->map)[WORD_OFFSET(n)] |= (1 << BIT_OFFSET(n));	
}

/* Takes a pointer to the bitmap block operating on and the block to access, turn that bit to 0
 *
 * Basically, the & (and) bitwise operation will always result to 0 if one of the operands is 0
 * The right side will produce a bitstring similar to 111111011111111 etc. meaning the only bit that can change
 * through the & operation is wherever that 0 is placed.
 */
void clear_bit(BitmapBlock *bblock, int n){
	(bblock->map)[WORD_OFFSET(n)] &= ~(1 << BIT_OFFSET(n));
}

/* Takes a pointer to the bitmap block operating on and the block to access, return the status of the bit, 1 or 0
 *
 * Basically, the & bitwise operation will take the string on the right, being similar to 0000010000000 etc.
 * and take the bitstring to be operated on. It will either keep the 1 or switch to all 0 if the current bitstring
 * has a 1 or 0 in that spot, respectively. returning bit != 0 gives the true or false value as an int.
 */
int get_bit(BitmapBlock *bblock, int n){
	word_t bit = (bblock->map)[WORD_OFFSET(n)] & (1 << BIT_OFFSET(n));
	return bit != 0;
}
