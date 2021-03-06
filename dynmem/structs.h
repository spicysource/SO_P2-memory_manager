#ifndef STRUCTS_H
#define STRUCTS_H

#include <bits/wordsize.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

//zwracane adresy muszą być wielokrotnością najdłuższego słowa
#define ALIGNMENT __WORDSIZE/8

//ile minimalnie musi zostać wolnego miejsca, żeby móc usunąć obszar
#define UNMAP_AREA_COND 5*(1<<12) //5 pełnych stron

typedef struct block block;
typedef struct area area;

struct block {
	block* prev;
	block* next;
	ssize_t size;
} __attribute__ ((aligned(ALIGNMENT)));

struct area {
	area* prev;
	area* next;
	block* firstBlock;
	size_t size;
} __attribute__ ((aligned(ALIGNMENT)));

block initializeBlock(size_t size, bool free);
area initializeArea(size_t size);

//rozmiary w bajtach struktur block i area
static const size_t blockSize = sizeof(block);
static const size_t areaSize = sizeof(area);

//długość słowa maszynowego w bajtach (zwracane adresy muszą być podzielne przez tę wartość)
static const int alignment = ALIGNMENT;

//mutexy potrzebne do synchronizacji wątków
extern pthread_mutex_t memoryMutex;
extern pthread_mutex_t printingMutex;

//dzieli blok na dwa bloki, z których pierwszy ma rozmiar size a drugi dopełnia oryginalny
//jeśli blok za drugim blokiem jest wolny to scala te dwa bloki
//jeśli to nie jest możliwe to zostawia oryginalny blok
void divideBlock(void* ptr, size_t size);

//bierze wolny blok wskazany przez ptr i scala go z poprzednim i następnym jeśli są wolne
//do zrobienia: munmap jeśli zostaje jeden blok w obszarze, a ilość wolnego miejsca
//w pozostałych obszarach przekracza ustalony próg
void* mergeFreeBlocks(block* ptr);

//tworzy strukturę obszaru i umieszcza go na początku obszaru wskazanego przez ptr
//za strukturą umieszcza blok długości co najmniej bsize, z tym że
//tworzy blok długości bsize a za nim blok pusty dopełniający obszar, jeśli to możliwe
//wpp po prostu tworzy blok zajmujący cały obszar
//utworzony obszar dodaje do końca listy dwukierunkowej
void createArea(void* ptr, size_t asize, size_t bsize, uint64_t offset);

//przeszukuje obszary w poszukiwaniu wolnego bloku długości co najmniej size
//jeśli taki istnieje, zwraca wskaźnik na niego, wpp zwraca NULL
block* sfree(size_t size, int align);

//wskaźniki na pierwszy i ostatni obszar 
extern area* firstArea;
extern area* lastArea;

#endif