#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <asm-generic/errno-base.h>

#include "dynmem.h"
#include "structs.h"
#include "stats.h"

pthread_mutex_t memoryMutex = PTHREAD_MUTEX_INITIALIZER;

void* malloc(size_t size)
{
    ++mallocCalls; //STAT

	if( size == 0 ) return NULL;

	size += (alignment - (size % alignment)) % alignment;
	size_t pageSize = (size_t)getpagesize();

	pthread_mutex_lock(&memoryMutex); //MUTEX

	block* dest = sfree(size,alignment);

	if( dest == NULL ){
		size_t size1 = size;
		size1 += blockSize + areaSize;
		size1 += (pageSize - (size1 % pageSize)) % pageSize;

		void* ptr = mmap(NULL, size1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if( ptr == MAP_FAILED ){
			perror("mmap error");
			return NULL;
		}

		createArea(ptr, size1, size, 0);

		pthread_mutex_unlock(&memoryMutex); //MUTEX
		return ptr + areaSize + blockSize;
	}	
	else {
		divideBlock(dest, size);

		void* ptr = (void*) dest;

		pthread_mutex_unlock(&memoryMutex); //MUTEX
		return ptr + blockSize;
	}
}

void* calloc(size_t count, size_t size)
{
    ++callocCalls; //STAT
    --mallocCalls; //STAT
	return malloc(count * size);
}

void* realloc(void* ptr, size_t size)
{
    ++reallocCalls; //STAT
	if( ptr == NULL )
		return malloc(size);

	if( size == 0 ) {
		free(ptr);
		return NULL;
	}
	size += (alignment - (size % alignment)) % alignment;

	block* blockPlace = (block*)(ptr - blockSize);

	pthread_mutex_lock(&memoryMutex); //MUTEX	

	if( size < blockPlace->size ){
		takenSpace -= blockPlace->size; //STAT
		freeSpace += blockPlace->size; //STAT

		divideBlock(blockPlace,size);

		pthread_mutex_unlock(&memoryMutex); //MUTEX
		return ptr;
	}
	else if( size > blockPlace->size ){
		if( blockPlace->next != NULL && blockPlace->next->size < 0 
			&& blockPlace->size - blockPlace->next->size + blockSize >= size ){

			takenSpace -= blockPlace->size; //STAT
			freeSpace += blockSize + blockPlace->size; //STAT

			blockPlace->size += blockSize - blockPlace->next->size;
			blockPlace->next = blockPlace->next->next;

			if( blockPlace->next != NULL ) blockPlace->next->prev = blockPlace;

			divideBlock(blockPlace,size);

			pthread_mutex_unlock(&memoryMutex); //MUTEX
			return blockPlace+1;
		}
		else {
			size_t oldSize = abs(blockPlace->size);

			pthread_mutex_unlock(&memoryMutex); //MUTEX

			void* newBlock = malloc(size);

			pthread_mutex_lock(&memoryMutex); //MUTEX	
			memmove(newBlock, ptr, oldSize);
			pthread_mutex_unlock(&memoryMutex); //MUTEX

			free(ptr);

			return newBlock;
		}
	}

	pthread_mutex_unlock(&memoryMutex); //MUTEX
	return ptr;
}

int posix_memalign(void** memptr, size_t align, size_t size)
{
    ++posix_memalignCalls; //STAT
	
	if( align % alignment != 0 || (align & (align - 1)) != 0 )
		return EINVAL;

	pthread_mutex_lock(&memoryMutex); //MUTEX

	size += (alignment - (size % alignment)) % alignment;

	block* dest = sfree(size,align);

	if( dest == NULL ){
		size_t pageSize = (size_t)getpagesize();
		uint64_t offset = (uint64_t)(areaSize);
		offset = (align - (offset % align)) % align;

		size_t size1 = size;
		size1 += blockSize + areaSize + offset;
		size1 += (pageSize - (size1 % pageSize)) % pageSize;

		void* ptr = mmap(NULL, size1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if( ptr == MAP_FAILED ){
			perror("mmap error");
			return ENOMEM;
		}

		createArea(ptr, size1, size, offset);

		*memptr = ptr + areaSize + blockSize + offset;		
	}
	else{
		uint64_t offset = (uint64_t)(dest+1);
		offset = (align - (offset % align)) % align;

		block oldBlock = *dest;

		block* newBlock = (block*)((void*)dest + offset);
		*newBlock = initializeBlock(-oldBlock.size - offset, true);

		if( oldBlock.prev != NULL ){
			newBlock->prev = oldBlock.prev;
			oldBlock.prev->next = newBlock;

			oldBlock.prev->size += offset;
		}
		else {
			area* currentArea = (area*)((void*)newBlock - areaSize - offset);
			currentArea->firstBlock = newBlock;
		}

		newBlock->next = oldBlock.next;

		printBlocks();

		divideBlock(newBlock, size);

		*memptr = newBlock+1;
	}

	pthread_mutex_unlock(&memoryMutex); //MUTEX

	return 0;
}

void free(void* ptr)
{
    ++freeCalls; //STAT

    if(ptr == NULL) return;

	ptr -= blockSize;
	block* freeBlock = (block*)ptr;

	pthread_mutex_lock(&memoryMutex); //MUTEX	
	
	takenSpace -= freeBlock->size; //STAT
	freeSpace += freeBlock->size; //STAT

	freeBlock->size *= -1;
	freeBlock = mergeFreeBlocks(freeBlock);

	if( freeBlock->prev == NULL && freeBlock->next == NULL && freeSpace + freeBlock->size >= UNMAP_AREA_COND ){
		uintptr_t pageSize = (uintptr_t)getpagesize();
		uintptr_t ptr1 = (uintptr_t)freeBlock;
		area* freeArea = (area*)(ptr1 - (ptr1 % pageSize));
		
		if( freeArea == firstArea ){
			firstArea = freeArea->next;
			firstArea->prev = NULL;
		}
		else if( freeArea == lastArea ){
			lastArea = freeArea->prev;
			lastArea->next = NULL;
		}
		else{
			freeArea->prev->next = freeArea->next;
			freeArea->next->prev = freeArea->prev;
		}

		freeSpace -= -freeBlock->size;

		if( munmap(freeArea, freeArea->size) == -1) {
			perror("munmap error");
		}
        ++areasUnmapped;
	}

	pthread_mutex_unlock(&memoryMutex); //MUTEX	
}
