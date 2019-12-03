#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"

const int MAX_BLOCK_NR = NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS + (NR_INDIRECT_BLOCKS * NR_INDIRECT_BLOCKS);

/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */

// reads ONE block at a time. need to map the logical block # to the phyrical block in memory
static int testfs_read_block(struct inode *in, int log_block_nr, char *block) {
	int phy_block_nr = 0;
	
	assert(log_block_nr >= 0); 

	if (log_block_nr >= MAX_BLOCK_NR)
	    return -EFBIG;

	// find which block type the logical address belongs to 
	int found_direct = log_block_nr < NR_DIRECT_BLOCKS; 
	int found_indirect = (log_block_nr - NR_DIRECT_BLOCKS) < NR_INDIRECT_BLOCKS; 
	int found_dindirect = (log_block_nr - NR_DIRECT_BLOCKS - NR_INDIRECT_BLOCKS) >= 0; // LMAOOOOOOO the problem was doing > and not >=

	if (found_direct) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr]; 
    } 
	else if (found_indirect) {
		log_block_nr -= NR_DIRECT_BLOCKS;

		if(in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int*)block)[log_block_nr];
		} else
			phy_block_nr = 0;
	} 
	else if(found_dindirect) {
		log_block_nr -= (NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS);

		if(in->in.i_dindirect > 0) {
			read_blocks(in->sb, block, in->in.i_dindirect, 1);

			int indir_block_nr = ((int *)block)[log_block_nr / NR_INDIRECT_BLOCKS];
			if (indir_block_nr > 0) {
				read_blocks(in->sb, block, indir_block_nr, 1);
				phy_block_nr = ((int *)block)[log_block_nr % NR_INDIRECT_BLOCKS];
			}
		} else
			phy_block_nr = 0;
	}
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	} 
	else {
		/* we support sparse files by zeroing out a block that is not allocated on disk. */
		bzero(block, BLOCK_SIZE); 
	}
	return phy_block_nr;
}

/* read data from file associated with inode in. read up to len bytes of data
 * into buffer buf, starting from offset off in file. on success, returns the
 * number of bytes read. on error, returns a negative value. */

// int testfs_read_data(struct inode *in, char *buf, off_t off, size_t len);
int testfs_read_data(struct inode *in, char *buf, off_t start, size_t size) {
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // block number 
	long block_ix = start % BLOCK_SIZE; // index within block
	int ret;
	unsigned total_read_size = 0;
	unsigned remaining_read_size = size;
	unsigned current_read_size;
	// long curr_read_size;

	assert(buf);
    
	if (start + (off_t) size > in->in.i_size) {
		size = in->in.i_size - start;
	}
	
	if (block_ix + size > BLOCK_SIZE) { // size over multiple blocks
 		while (total_read_size < size) {
			// update block # and index
			block_nr = (start + total_read_size) / BLOCK_SIZE;
			block_ix = (start + total_read_size) % BLOCK_SIZE;

			if ((ret = testfs_read_block(in, block_nr, block)) < 0)
				return ret;

			remaining_read_size = size - total_read_size;
			current_read_size = (remaining_read_size < (BLOCK_SIZE - block_ix)) ? remaining_read_size : BLOCK_SIZE - block_ix;
	
			// read onto buffer
			memcpy(buf, block + block_ix, current_read_size);
			// update location of buffer and total amount read so far
			buf += current_read_size; 
			total_read_size += current_read_size; 
		}
	} else { // this clause was given
		if ((ret = testfs_read_block(in, block_nr, block)) < 0)
			return ret;
		memcpy(buf, block + block_ix, size);
		total_read_size += size;
	}
	/* return the number of bytes read or any error */
	return total_read_size;
}


/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int testfs_allocate_block(struct inode *in, int log_block_nr, char *block) {
	int phy_block_nr;
	char indirect[BLOCK_SIZE];
	char dindirect[BLOCK_SIZE];
	int indirect_allocated, dindirect_allocated = 0;

 	if (log_block_nr >= MAX_BLOCK_NR) {
        return -EFBIG;
    }

	assert(log_block_nr >= 0);
	int found_direct = log_block_nr < NR_DIRECT_BLOCKS; 
	int found_indirect = (log_block_nr - NR_DIRECT_BLOCKS) < NR_INDIRECT_BLOCKS; 
	int found_dindirect = (log_block_nr - NR_DIRECT_BLOCKS - NR_INDIRECT_BLOCKS) >= 0;

	/* phy_block_nr > 0: block exists, so we don't need to allocate it,
	   phy_block_nr < 0: some error */
	if ((phy_block_nr = testfs_read_block(in, log_block_nr, block)) != 0) // given
		return phy_block_nr;

	/* allocate a direct block */
	if (found_direct) { // GIVEN 
		assert(in->in.i_block_nr[log_block_nr] == 0);
		if ((phy_block_nr = testfs_alloc_block_for_inode(in)) >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}
	// allocate an indirect block
	else if (found_indirect) { // GIVEN
		log_block_nr -= NR_DIRECT_BLOCKS;

		/* allocate an indirect block */
		if (in->in.i_indirect == 0) {	
			bzero(indirect, BLOCK_SIZE);
			if ((phy_block_nr = testfs_alloc_block_for_inode(in)) < 0)
				return phy_block_nr;
			indirect_allocated = 1;
			in->in.i_indirect = phy_block_nr;
		} 
		else {	/* read indirect block */
			read_blocks(in->sb, indirect, in->in.i_indirect, 1);
		}

		/* allocate direct block */
		assert(((int *)indirect)[log_block_nr] == 0);	

		if ((phy_block_nr = testfs_alloc_block_for_inode(in)) >= 0) {
			/* update indirect block */
			((int *)indirect)[log_block_nr] = phy_block_nr;
			write_blocks(in->sb, indirect, in->in.i_indirect, 1);
		} 
		else if (indirect_allocated) {
			/* there was an error while allocating the direct block, 
			* free the indirect block that was previously allocated */
			testfs_free_block_from_inode(in, in->in.i_indirect);
			in->in.i_indirect = 0;
		}
		return phy_block_nr;
	}
	else if (found_dindirect) { // TBD
		log_block_nr -= (NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS);
		
        // ** DOUBLE INDIRECT ** // 
        if(in->in.i_dindirect == 0) { // allocate a double indirect block
            bzero(dindirect, BLOCK_SIZE);
            if((phy_block_nr = testfs_alloc_block_for_inode(in)) < 0)
                return phy_block_nr;
            dindirect_allocated = 1;
            in->in.i_dindirect = phy_block_nr;
        } 
		else { // one exists
            read_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
        }

		// ** INDIRECT BLOCK **//
        if(((int*) dindirect)[log_block_nr/NR_INDIRECT_BLOCKS] == 0) { // allocate an indirect block
            bzero(indirect, BLOCK_SIZE);
            if ((phy_block_nr = testfs_alloc_block_for_inode(in)) < 0)  // noice
                return phy_block_nr;

			indirect_allocated = 1;
			((int *) dindirect)[log_block_nr/NR_INDIRECT_BLOCKS] = phy_block_nr;
        }
		else { // one exists
            read_blocks(in->sb, indirect, ((int*) dindirect)[log_block_nr/NR_INDIRECT_BLOCKS], 1);
        }

        // ** DIRECT BLOCK **//
        // assert(((int *) indirect)[log_block_nr % NR_INDIRECT_BLOCKS] == 0);
		if ((phy_block_nr = testfs_alloc_block_for_inode(in)) >= 0) {
			/* update indirect block */
            ((int *) indirect)[log_block_nr%NR_INDIRECT_BLOCKS] = phy_block_nr;
			write_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
            write_blocks(in->sb, indirect, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS], 1);

        } 
		else {
			/* there was an error while allocating shit,  
			* free the indirect block that was previously allocated */
            if(dindirect_allocated){
                // testfs_free_block_from_inode(in, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS]);
                testfs_free_block_from_inode(in, in->in.i_dindirect);
				in->in.i_dindirect = 0;
            }
			
			if(indirect_allocated) { 
                testfs_free_block_from_inode(in, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS]);
				((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS] = 0;
				// testfs_free_block_from_inode(in, in->in.i_indirect);
				// in->in.i_indirect = 0;
			}
        }
        return phy_block_nr;
	}
	return 0;
}

int testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size) {
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE;
	long block_ix = start % BLOCK_SIZE;
	int ret;
	// int bytes_written = 0; // new 
	// unsigned remaining_write_size = size;
	unsigned total_written_size = 0;

	while (block_ix + ((int)size-total_written_size) > BLOCK_SIZE) {
	  	if((ret = testfs_allocate_block(in, block_nr, block))<0) {
			if(ret == -EFBIG)
				in->in.i_size = MAX(in->in.i_size, start + (off_t)total_written_size);
			return ret;
		}

		memcpy(block+block_ix, buf+total_written_size, BLOCK_SIZE - block_ix);
		write_blocks(in->sb, block, ret, 1);
		
		total_written_size += BLOCK_SIZE - block_ix;
		block_ix = 0;
		block_nr++;
	}
	
	/* ret is the newly allocated physical block number */
	if ((ret = testfs_allocate_block(in, block_nr, block)) < 0) {
		in->in.i_size = MAX(in->in.i_size, start + (off_t)total_written_size);
		return ret;
	}

	memcpy(block + block_ix, buf+total_written_size, (int)size-total_written_size);
	write_blocks(in->sb, block, ret, 1);
	total_written_size += (int)size-total_written_size;
	/* increment i_size by the number of bytes written. */
	if (size > 0)
		in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
	in->i_flags |= I_FLAGS_DIRTY;
	/* return the number of bytes written or any error */
	return size;
}

int testfs_free_blocks(struct inode *in) {
	int i;
	int e_block_nr;

	/* last block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		assert(e_block_nr > 0);
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (int i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			if (((int *)block)[i] == 0)
				continue;
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
	// this is the part we gotta write
	if (e_block_nr > 0) {
		char block[BLOCK_SIZE];
		char indirect_block[BLOCK_SIZE];
		assert(e_block_nr > 0);
		read_blocks(in->sb, block, in->in.i_dindirect, 1);
		for (int i = 0; i < NR_INDIRECT_BLOCKS; ++i) {
			if (((int *)block)[i] == 0) 
				continue; 
			
			read_blocks(in->sb, indirect_block, ((int *)block)[i], 1);

			for (int j = 0; (i * NR_INDIRECT_BLOCKS + j) < e_block_nr && j < NR_INDIRECT_BLOCKS; ++j) {
				if (((int *)indirect_block)[j] == 0) 
					continue;

				testfs_free_block_from_inode(in, ((int *)indirect_block)[j]);
				((int *)indirect_block)[j] = 0;
			}
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_dindirect);
		in->in.i_dindirect = 0;
	}

	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	return 0;
}
