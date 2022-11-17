#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
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
 * MAX_SETUP_BLOCKS is the cutoff for the amount of blocks used to support the file system.
 * ***IMPORTANT*** since the disk starts addressing at 0, max setup blocks is actually the address of the first
 * block available for user input.
 */
#define MAX_FILES_SUPPORTED 512
#define NUM_INODE_IN_BLOCK (SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)) // 4096 / 32 = *128*
#define NUM_DENTRY_IN_BLOCK (SOFTWARE_DISK_BLOCK_SIZE / sizeof(DirEntry)) // 4096 / 512 = *8*

#define INODE_BLOCKS ceil(MAX_FILES_SUPPORTED / NUM_INODE_IN_BLOCK) // 512 / 128 = *4*
#define DENTRY_BLOCKS ceil(MAX_FILES_SUPPORTED / NUM_DENTRY_IN_BLOCK) // 512 / 8 = *64*
#define MAX_SETUP_BLOCKS (2 + INODE_BLOCKS + DENTRY_BLOCKS) // 70

//The following values start from 0 blocks on the disk, with the values shown being the start
//and final block of that section, inclusive on both
#define BITMAP_START 0
#define BITMAP_END 1
#define INODE_START (BITMAP_END + 1) // 2
#define INODE_END (INODE_START + INODE_BLOCKS) - 1 // 2 + 4 = *5*
#define DENTRY_START (INODE_END + 1) // 6
#define DENTRY_END (DENTRY_START + DENTRY_BLOCKS) - 1 // 6 + 64 - 1 = *69*
#define USER_START (DENTRY_END + 1) // 70, note how it matches MAX_SETUP_BLOCKS

//Update with caution, this value must keep Inode struct's size a multiple of 4096, which is the size of a block.
#define NUM_DENTRIES_IN_INODE 13

//Helper functions for finding the inode/dentry in memory given its bitmap "address"
#define BLOCK_OF_INODE(b) (b / NUM_INODE_IN_BLOCK + INODE_START)
#define ADDRESS_OF_INODE(b) (b % NUM_INODE_IN_BLOCK)
#define BLOCK_OF_DENTRY(b) (b / NUM_DENTRY_IN_BLOCK + DENTRY_START)
#define ADDRESS_OF_DENTRY(b) (b % NUM_DENTRY_IN_BLOCK)

//Bitmap constants that are used in below functions. Credit for these constants found below in bitmaps functions sect.
typedef uint32_t word_t;
enum { BITS_PER_WORD = sizeof(word_t) * CHAR_BIT };
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)


//------------FUNCTION PROTOTYPES------------

void set_bit(BitmapBlock *bblock, int n);
void clear_bit(BitmapBlock *bblock, int n);
int get_bit(BitmapBlock *bblock, int n);


//------------STRUCTURES------------

/* Struct of directory entries meant for finding inodes and keeping track of the file being open
 * This struct is compiled together in a block by the DirEntryBlock struct. Holds 512 bytes for each entry.
 *
 * ***Credit to Dr. Golden for this struct*** 
 */
typedef struct _DirEntry {
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

/* Holds the structure for the two bitmaps we need for the filesystem, 1 keeping blocks used and another
 * inodes/dir entries used, since both inodes and dir entries map to each other 1 to 1
 * Bitmap functions are defined below to help with operations on the map.
 */
typedef struct _BitmapBlock {
	word_t map[SOFTWARE_DISK_BLOCK_SIZE / sizeof(word_t)];
} BitmapBlock;


//------------FILESYSTEM FUNCTIONS------------




//------------SUPPORT FUNCTIONS------------



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
