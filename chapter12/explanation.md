Superblock Layout
The superblock is defined as a struct that contains two integers; the number of blocks reserved for inodes, and the block number at the head of the free block list.

i-Node structure
The i-Node is defined as a struct with the integer fields allocated, direct, indirect, and double_indirect. Allocated is 0 if the block is free, and nonzero otherwise. Direct is the block number that the direct pointer is pointing to. Similarly, indirect is the block number that the indirect pointer first points to and double_indirect is the block number that the double indirect pointer first points to.

Block allocation strategy
The allocator will use free_list_head to find the current free list’s ufs_ptr_block. Then, it will traverse through this ufs_ptr_block’s ptrs array to find a free block (from all indices > 0). If any is found, then that ptrs entry is set to 0. However, if none are found then that means this ufs_ptr_block block is exhausted of free blocks, so it will use free_list_head and advance the free_list_head.

When free space gets exhausted, the filesystem treats ufs_alloc_block() returning 0 as an out-of-space condition. In an old version of the code, this case was called die("ufs_write: disk full"), which halted EmbryOS immediately. We changed this so ufs_write() now fails gracefully instead of crashing the system. We did this by stopping the current write and returning without committing a partial update. If a write needs new data blocks and one of those allocations fails, the code frees any blocks that were allocated during that same write attempt and returns early. This keeps the filesystem data consistent and avoids leaking of freshly allocated information.

We tested this graceful dying behavior using the stress tests. The stress test filled the disk until exhaustion, detected that further writes no longer took effect, and then deleted the created files. After cleanup, creating, writing, and reading a new file still worked. This meant the filesystem remained working after running out of space. Therefore we verified that we implemented dying gracefully.


Free-list organization
The free_list_head is in the superblock and points to the first block of the free list. 

The free list consists of blocks, called ufs_ptr_block’s. The ufs_ptr_block is an array (named ptrs) of free block numbers. ptrs[0] points to the next free-list block (as part of the principle of linked structure) and is 0 if there are no more left. The rest of the ptrs entries have the block number of a free block.

How holes are represented and handled
The holes are represented as 0’s in the file’s direct, indirect, and/or double_indirect pointers, and the pointer blocks of the indirect and/or double_indirect pointers. 

If a hole is read from, the system returns zeroed data. If the hole is written to, there is a new block allocated and the pointer is updated to that new block address.  

AI Usage:
Coding Implementation: We passed in the assignment description into AI to generate the code and files necessary to fulfill the implementation, and debugged collaboratively with AI and further inspected and edited on our own. We used chatgpt and claude code to write code and debug the code for all of the files we wrote. 

Test Suite: We used AI to brainstorm ways to test our functionalities and collaboratively write up a test suite, passing in the assignment spec’s expected behavior and edge cases to work with. We also used AI to debug environment-specific linker issues. Used chatgpt to generate test apps to test the files system behavior.

Explanation: We also used AI to help us with writing the explanation.md by summarizing the code and then rewriting that in our own words.
