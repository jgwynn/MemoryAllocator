//defines miscellaneous symbolic constants and types
#include <unistd.h>
#include <string.h>
//contains function declarations and mappings for threading interfaces and defines a number of constants used by those functions
#include <pthread.h>
// Only for the debug printf
#include <stdio.h>

//used to hold 16 bytes of memory 
typedef char ALIGN[16];

//union forms a grouping of differently typed variables in the same memory location
union header {
	struct {
		size_t size;
		unsigned is_free;
		union header *next;
	} s;
	// force the header to be aligned to 16 bytes 
	ALIGN stub;
};
//defines the union as header_t
typedef union header header_t;

//creates the head and tail pointers for the memory allocation of the heap
header_t *head = NULL, *tail = NULL;
//creates a malloc lock for multithreading
pthread_mutex_t global_malloc_lock;


//cycles through the blocks to see if one of the blocks is free.
//notably free in this file simply means the block is no longer being used
//but is still malloced
header_t *get_free_block(size_t size)
{
	header_t *curr = head;
	while(curr) {
		/* see if there's a free block that can accomodate requested size */
		if (curr->s.is_free && curr->s.size >= size)
			return curr;
		curr = curr->s.next;
	}
	return NULL;
}

//attempts to "free" the block by marking it not in use.
//if the block to be freed happens to be the last block in the linked list, we can release it instead.
void free(void *block)
{
	header_t *header, *tmp;
	/* program break is the end of the process's data segment */
	void *programbreak;

	if (!block)
		return;
    //locks the mutex to ensure no other thread attempts to read or manipulate the memory
    //although the use of sbrk kind of nullifys the overall safety due to it not being threadsafe
	pthread_mutex_lock(&global_malloc_lock);
	header = (header_t*)block - 1;
	/* sbrk(0) gives the current program break address */
	programbreak = sbrk(0);

	/*
	   Check if the block to be freed is the last one in the
	   linked list. If it is, then we could shrink the size of the
	   heap and release memory to OS. Else, we will keep the block
	   but mark it as free.
	 */
	if ((char*)block + header->s.size == programbreak) {
		if (head == tail) {
			head = tail = NULL;
		} else {
			tmp = head;
			while (tmp) {
				if(tmp->s.next == tail) {
					tmp->s.next = NULL;
					tail = tmp;
				}
				tmp = tmp->s.next;
			}
		}
		/*
		   sbrk() with a negative argument decrements the program break.
		   So memory is released by the program to OS.
		*/
		sbrk(0 - header->s.size - sizeof(header_t));
		/* Note: This lock does not really assure thread
		   safety, because sbrk() itself is not really
		   thread safe. Suppose there occurs a foregin sbrk(N)
		   after we find the program break and before we decrement
		   it, then we end up realeasing the memory obtained by
		   the foreign sbrk().
		*/
		pthread_mutex_unlock(&global_malloc_lock);
		return;
	}
	header->s.is_free = 1;
	pthread_mutex_unlock(&global_malloc_lock);
}

//allocates memory of a specific size
void *malloc(size_t size)
{
	size_t total_size;
	void *block;
	header_t *header;
	if (!size)
		return NULL;
	pthread_mutex_lock(&global_malloc_lock);
	header = get_free_block(size);
	if (header) {
		/* Woah, found a free block to accomodate requested memory. */
		header->s.is_free = 0;
		pthread_mutex_unlock(&global_malloc_lock);
		return (void*)(header + 1);
	}
	/* We need to get memory to fit in the requested block and header from OS. */
	total_size = sizeof(header_t) + size;
	block = sbrk(total_size);
    //if memory was unable to be allocated, unlock the mutex and return NULL
	if (block == (void*) -1) {
		pthread_mutex_unlock(&global_malloc_lock);
		return NULL;
	}
    //block set up
	header = block;
	header->s.size = size;
	header->s.is_free = 0;
	header->s.next = NULL;
	if (!head)
		head = header;
	if (tail)
		tail->s.next = header;
	tail = header;
    //unlock the mutex and return the block as a void pointer
	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header + 1);
}

//assigns memory and pre-sets the memory to 0
void *calloc(size_t num, size_t nsize)
{
	size_t size;
	void *block;
    //if the passed variables are NULL or NULL-like return NULL
	if (!num || !nsize)
		return NULL;
	size = num * nsize;
	/* check multiplication overflow */
	if (nsize != size / num)
		return NULL;
	block = malloc(size);
    //if allocation failed, return NULL
	if (!block)
		return NULL;
    //set memory of block to be 0
	memset(block, 0, size);
	return block;
}

//reallocates memory to be larger then copies the contents of the existing block
//into the new block and returns it
void *realloc(void *block, size_t size)
{
	header_t *header;
	void *ret;
	if (!block || !size)
		return malloc(size);
	header = (header_t*)block - 1;
    //return block as is if the block is already the requested size or bigger
	if (header->s.size >= size)
		return block;
	ret = malloc(size);
	if (ret) {
		/* Relocate contents to the new bigger block */
		memcpy(ret, block, header->s.size);
		/* Free the old memory block */
		free(block);
	}
	return ret;
}

/* A debug function to print the entire link list */
void print_mem_list()
{
	header_t *curr = head;
	printf("head = %p, tail = %p \n", (void*)head, (void*)tail);
	while(curr) {
		printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
			(void*)curr, curr->s.size, curr->s.is_free, (void*)curr->s.next);
		curr = curr->s.next;
	}
}
