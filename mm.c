
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "bteam",
    /* First member's full name */
    "hyeok",
    /* First member's email address */
    "dbsgur98@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12) 
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc)) 
#define GET(p)          (*(unsigned int *)(p)) 
#define PUT(p, val)     (*(unsigned int *)(p) = (val))  
#define GET_SIZE(p)     (GET(p) & ~0x7) 
#define GET_ALLOC(p)    (GET(p) & 0x1)
#define HDRP(bp)        ((char *)(bp) - WSIZE) // 
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 
#define PRED(bp)        ((char *)(bp))
#define SUCC(bp)        ((char *)(bp) + WSIZE)

#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 
//블록 최소 크기인 2**4부터 최대 크기인 2**32를 위한 리스트 29개
#define LIST_NUM 29

static void *extend_heap(size_t words);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
static void *coalesce(void *bp);
void *mm_realloc(void *ptr, size_t size);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
void delete_block(char* bp);
void add_free_block(char* bp);
int get_seg_list_num(size_t size);

static char *heap_listp;
//분리 가용 리스트 생성
void* seg_list[LIST_NUM];

int mm_init(void)
{   
    //각 분리 가용 리스트를 NULL로 초기화해준다.
    for (int i = 0; i < LIST_NUM; i ++){
        seg_list[i] = NULL;
    }

    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) 
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}

static void *extend_heap(size_t words)
{   
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * DSIZE : words * DSIZE; 

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}


void *mm_malloc(size_t size)
{   
    size_t asize;
    size_t extendsize;
    char *bp;
    
    if (size == 0)
        return NULL;
    
    if (size <= DSIZE)
        asize = 2*DSIZE; 
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); 

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

void mm_free(void *bp)
{   
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
    coalesce(bp);
}

static void *coalesce(void *bp)
{   
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        
        delete_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));

        delete_block(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else if (!prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(PREV_BLKP(bp)));

        delete_block(PREV_BLKP(bp));
        delete_block(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    
    add_free_block(bp);
    return bp;
}


void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *find_fit(size_t asize)
{  
    void *search_p;
    //asize가 들어갈 수 있는 seg_list 찾기
    int i = get_seg_list_num(asize);
    
    //리스트 내부의 블록들 중 가장 작은 블록 할당(best-fit)
    void *tmp = NULL;
    while (i < LIST_NUM){
        for (search_p = seg_list[i]; search_p != NULL; search_p = GET(SUCC(search_p))){
            if (GET_SIZE(HDRP(search_p)) >= asize){
                if (tmp == NULL){
                    tmp = search_p;
                } else {
                    if (GET_SIZE(tmp) > GET_SIZE(HDRP(search_p))){
                        tmp = search_p;
                    }
                }
            }
        }
        if (tmp != NULL){
            return tmp;
        }
        i ++;
    }

    return NULL;
}

static void place(void *bp, size_t asize)
{   
    delete_block(bp);
    size_t old_size = GET_SIZE(HDRP(bp));
    if (old_size >= asize + (2 * DSIZE)){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        PUT(HDRP(NEXT_BLKP(bp)), PACK((old_size - asize), 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK((old_size - asize), 0));
        
        coalesce(NEXT_BLKP(bp));
    }
    else {
        PUT(HDRP(bp), PACK(old_size, 1));
        PUT(FTRP(bp), PACK(old_size, 1));
    }
}

void delete_block(char* bp){ 
    int seg_list_num = get_seg_list_num(GET_SIZE(HDRP(bp)));

    if (GET(PRED(bp)) == NULL){
        if (GET(SUCC(bp)) == NULL){
            seg_list[seg_list_num] = NULL;
        } else {
            PUT(PRED(GET(SUCC(bp))), NULL);
            seg_list[seg_list_num] = GET(SUCC(bp));
        }
    } 
    else {
        if (GET(SUCC(bp)) == NULL){
            PUT(SUCC(GET(PRED(bp))), NULL);
        } else {
            PUT(PRED(GET(SUCC(bp))), GET(PRED(bp)));
            PUT(SUCC(GET(PRED(bp))), GET(SUCC(bp)));
        }
    }
}

void add_free_block(char* bp){
    //들어가야 하는 seg_list 찾고 그 seg_list에 추가
    int seg_list_num = get_seg_list_num(GET_SIZE(HDRP(bp)));
    if (seg_list[seg_list_num] == NULL){
        PUT(PRED(bp), NULL);
        PUT(SUCC(bp), NULL);
    } else {
        PUT(PRED(bp), NULL);
        PUT(SUCC(bp), seg_list[seg_list_num]);
        PUT(PRED(seg_list[seg_list_num]), bp);
    }
    seg_list[seg_list_num] = bp;
}

int get_seg_list_num(size_t size){
    //seg_list[0]은 블록의 최소 크기인 2**4를 위한 리스트 
    int i = -4;
    while (size != 1){
        size = (size >> 1);
        i ++;
    }
    return i;
}
// /*
//  * mm-naive.c - The fastest, least memory-efficient malloc package.
//  * 
//  * In this naive approach, a block is allocated by simply incrementing
//  * the brk pointer.  A block is pure payload. There are no headers or
//  * footers.  Blocks are never coalesced or reused. Realloc is
//  * implemented directly using mm_malloc and mm_free.
//  *
//  * NOTE TO STUDENTS: Replace this header comment with your own header
//  * comment that gives a high level description of your solution.
//  */
// #include <stdio.h>
// #include <stdlib.h>
// #include <assert.h>
// #include <unistd.h>
// #include <string.h>

// #include "mm.h"
// #include "memlib.h"

// /*********************************************************
//  * NOTE TO STUDENTS: Before you do anything else, please
//  * provide your team information in the following struct.
//  ********************************************************/
// team_t team = {
//     /* Team name */
//     "bteam",
//     /* First member's full name */
//     "hyeok",
//     /* First member's email address */
//     "dbsgur98@gmail.com",
//     /* Second member's full name (leave blank if none) */
//     "",
//     /* Second member's email address (leave blank if none) */
//     ""
// };

// /* single word (4) or double word (8) alignment */
// #define ALIGNMENT 8

// /* rounds up to the nearest multiple of ALIGNMENT */
// #define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// /* Basic constants and macros */
// #define WSIZE 4       /* Word and header / footer size (bytes) */
// #define DSIZE 8       /* Double word size (bytes) */
// #define CHUNKSIZE (1<<12) /* Extend heap by this amout (bytes) */

// #define MAX(x, y) ((x) > (y) ? (x) : (y))

// /* Pack a size and allocated bit into a word */
// #define PACK(size, alloc) ((size) | (alloc))

// /* Read and write a allocated bit into a word */
// #define GET(p) (*(unsigned int*)(p))
// #define PUT(p, val) (*(unsigned int*)(p) = (val))

// /* Read th size and allocated fields from address p */
// #define GET_SIZE(p) (GET(p) & ~0x7) // ~은 역수니까 11111000이 된다.
// #define GET_ALLOC(p) (GET(p) & 0x1) // 00000001이 된다. 헤더에서 가용여부만 가져온다.

// /* Given block ptr bp, compute address of its header and footer */
// #define HDRP(bp) ((char*)(bp) - WSIZE)  // dp가 어디에 있든 상관없이 WSIZE 앞에 위치한다.
// #define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 헤더의 끝 지점부터 GET SIZE(블록의 사이즈)만큼 더한 다음 word를 2번 뺀게 footer의 시작 위치가 된다.

// /* Given block ptr bp, compute address of next and previous blocks */
// #define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp)-WSIZE)))
// #define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp)-DSIZE)))
// static char *heap_listp; // 처음에 쓸 큰 가용블록 힙을 만들어준다.

// // 👆 Defining default constants and macros for manipulating the available list


// // coalesce : 블록 연결하는 함수
// static void *coalesce(void *bp)
// {
//   size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 가용여부
//   size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록의 가용여부
//   size_t size = GET_SIZE(HDRP(bp)); // 현재 블록의 사이즈

//   if(prev_alloc && next_alloc){ // CASE 1 이전과 다음 블록이 모두 할당된 경우
//     return bp;
//   }

//   else if(prev_alloc && !next_alloc){ // CASE 2 이전블록은 할당 상태 다음블록은 가용상태
//     size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 다음 블록의 헤더를 보고 그 크기만큼 지금 블록의 사이즈에 추가한다.
//     PUT(HDRP(bp), PACK(size, 0));
//     PUT(FTRP(bp), PACK(size, 0));
//   }

//   else if(!prev_alloc && next_alloc){ // CASE 3 이전블록은 가용상태 / 다음 블록은 할당 상태
//     size+= GET_SIZE(HDRP(PREV_BLKP(bp)));
//     PUT(FTRP(bp), PACK(size, 0));
//     PUT(HDRP(PREV_BLKP(bp)),PACK(size, 0));
//     bp = PREV_BLKP(bp);
//   }

//   else { //CASE 4 둘다 가용상태
//     size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
//     PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
//     PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
//     bp = PREV_BLKP(bp);
//   }
//   return bp;
// }

// static void *extend_heap(size_t words){
//   char *bp;
//   size_t size;

//   /* Allocate an even number of words to maintain alignment */
//   size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
//   if ((long)(bp = mem_sbrk(size)) == -1)
//     return NULL; // 사이즈를 늘릴 때마다 old brk는 과거의 mem_brk위치로 간다.
  
//   /* Initialize free block header/footer and the epilogue header */
//   PUT(HDRP(bp), PACK(size, 0));             /* Free block header */
//   PUT(FTRP(bp), PACK(size, 0));             /* Free block footer */
//   PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  /* New epilogue header */
//   // 처음 세팅의 의미 -> bp를 헤더에서 읽은 사이즈만큼 이동하고, 앞으로 한칸 간다. 그 위치가 결국 늘린 블록 끝에서 한칸 간거라 거기가 epilogue haeder 위치이다.

//   /* Coalesce if the previous was free */
//   return coalesce(bp);
// }


// /* 
//  * mm_init - initialize the malloc package.
//  */
// int mm_init(void)
// {
//   /* Create the initial empty heap */
//   if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
//     return -1;
//   PUT(heap_listp, 0);                         /* Alignment padding */
//   PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
//   PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
//   PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
//   heap_listp += (2*WSIZE); // prologue header와 footer 사이로 포인터를 옮긴다. header 뒤 위치. 다른 블록 이동할때

//     /* Extend the empty heap with a free block of CHUNKSIZE bytes */
//   if(extend_heap(CHUNKSIZE/WSIZE) == NULL) // extend heap을 통해 시작할 때 한번 heap을 늘려준다.
//     return -1;
//   return 0;
// }


// /*
//  * mm_free - Freeing a block does nothing.
//  */
// void mm_free(void *ptr)
// {
//   size_t size = GET_SIZE(HDRP(ptr));

//   PUT(HDRP(ptr), PACK(size, 0));
//   PUT(FTRP(ptr), PACK(size, 0));
//   coalesce(ptr);
// }

// // first fit
// static void *find_fit(size_t asize){
//   void *bp;
//   for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
//     if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
//       return bp;
//     }
//   }
//   return NULL;
// }

// static void place(void *bp, size_t asize){
//   size_t csize = GET_SIZE(HDRP(bp));
//   if((csize - asize) >= (2*DSIZE)){
//     PUT(HDRP(bp), PACK(asize, 1));
//     PUT(FTRP(bp), PACK(asize, 1));
//     bp = NEXT_BLKP(bp);
//     PUT(HDRP(bp), PACK(csize-asize, 0));
//     PUT(FTRP(bp), PACK(csize-asize, 0));
//   }
//   else{
//     PUT(HDRP(bp), PACK(csize, 1));
//     PUT(FTRP(bp), PACK(csize, 1));
//   }
// }

// /* 
//  * mm_malloc - Allocate a block by incrementing the brk pointer.
//  *     Always allocate a block whose size is a multiple of the alignment.
//  */
// void *mm_malloc(size_t size)
// {
//   size_t asize; /* Adjusted block size */
//   size_t extendsize; /* Amout to extend heap if no fit */
//   char *bp;

//   /* Ignore spurious request */
//   if (size ==0)
//     return NULL;
//   /* Adjust block size to include overhead and alignment reqs */
//   if(size <= DSIZE)
//     asize = 2*DSIZE;
//   else
//     //8바이트를 넘는 요청에 대해서 일반적인 규칙은 오버헤드 바이트를 추가하고, 인접 8의 배수로 반올림한다.
//     asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/DSIZE);

//   /* Search the free list for a fit */
//   // 할당기가 요청한 크기를 조정한 후에 적절한 가용 블록을 가용 리스트에서 검색한다.
//   if ((bp = find_fit(asize)) != NULL){
//     // 만일 맞는 블록을 찾으면, 할당기는 요청한 블록을 배치하고, 옵션으로 초과부분을 분할하고  -> 새롭게 할당한 블록을 리턴한다(return bp),
//     place(bp, asize);
//     return bp; // after place return location of block
//   }

//   /* No fit found. Get more memory and place the block */
//   extendsize = MAX(asize, CHUNKSIZE);
//   if((bp = extend_heap(extendsize/WSIZE)) == NULL)
//     return NULL;
//   place(bp, asize);
//   return bp;
// }


// /*
//  * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
//  */
// void *mm_realloc(void *ptr, size_t size)
// {
//     void *oldptr = ptr;
//     void *newptr;
//     size_t copySize;
    
//     newptr = mm_malloc(size);
//     if (newptr == NULL)
//       return NULL;
//     copySize = GET_SIZE(HDRP(oldptr));  
//     if (size < copySize)
//       copySize = size;
//     memcpy(newptr, oldptr, copySize);
//     mm_free(oldptr);
//     return newptr;
// }


