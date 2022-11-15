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

//Update with caution, this value must keep Inode struct's size a multiple of 4096, which is the size of a block.
#define NUM_DENTRIES_IN_INODE 13

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
 * This struct is compiled together in a block by the DirEntryBlock struct.
 *
 * ***Credit to Dr. Golden for this struct*** 
 */
typedef struct _DirEntry {
	uint8_t file_open;
	uint16_t inode_idx;
	char file_name[MAX_FILENAME_SIZE];
} DirEntry;

/* Because the DirEntry struct is made to be a multiple of the block size, this struct can evenly hold a number of
 * DirEntries
 */
typedef struct _DirEntryBlock {
	DirEntry dentries[SOFTWARE_DISK_BLOCK_SIZE / sizeof(DirEntry)];
} DirEntryBlock;


typedef struct _Inode {
	uint32_t file_size;
	uint16_t dir_blocks[NUM_DENTRIES_IN_INODE + 1]; //Holds enough for dir entries and 1 indir entry which will be
							//at the back of this array
} Inode;

typedef struct _InodeBlock {
	Inode inodes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)];
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


//------------FUNCTIONS------------



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
