#include "memory.h"
#include "mics.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

void* memory;
void* memory_end;
int addrSize;
VA lastAddr;

typedef struct header
{
    VA left;
    size_t size;
} BlockHeader;

/**
 * @brief getHeaderSize
 * @return size of memory block rounded to bytes
 */
size_t getHeaderSize(){
    return ceil((double)addrSize*2/8);
}

/**
 * @brief readHeader
 * @param addr
 * @return header
 * Fills header from memory adress
 */
BlockHeader readHeader(void* addr){
    BlockHeader h;
    unsigned va=0;
    unsigned size = 0;
    va |= *((unsigned*)addr) & createMask(0, addrSize-1);
    size |= ((*((unsigned*)addr) & (createMask(0, addrSize) << addrSize)) >> addrSize);

    h.left = va;
    h.size = size;
    return h;
}
/**
 * @brief writeHeader
 * @param addr
 * @param h
 * Writes header to memory
 */
void writeHeader(void* addr, BlockHeader h){
    *((unsigned*)addr) &= ~createMask(0, addrSize*2);
    *((unsigned*)addr) |= (unsigned)h.left & createMask(0, addrSize-1);
    *((unsigned*)addr) |= (h.size & createMask(0, addrSize)) << addrSize;
}

/**
 * @brief getNextBlock
 * @param block
 * @return
 * Returns pointer to block that follows given block
 */
void* getNextBlock(void* block){
    BlockHeader h;
    h = readHeader(block);
    return block + getHeaderSize() + h.size;
}
/**
 * @brief printMemory
 * Prints memory state
 */
void printMemory(){
    void* block = memory;
    printf("=== memory state === (header size is %d bytes)\n", getHeaderSize());
    printf("va\t size(bytes)\n");
    while(block < memory_end){
        BlockHeader blockHeader = readHeader(block);
        printf("%d\t %d\n", blockHeader.left, blockHeader.size);
        block += getHeaderSize()+ blockHeader.size;
    }
    printf("\n\n");
}
/**
 * @brief getByVA
 * @param ptr virtual pointer
 * @return pointer to block start
 * Finds block with given VA
 */
void* getByVA(VA ptr){
    void* block = memory;
    while(block < memory_end){
        BlockHeader blockHeader = readHeader(block);
        if(blockHeader.left <= ptr && ptr < blockHeader.size + blockHeader.left){
            return block;
        }
        block = getNextBlock(block);
    }
    return NULL;
}

/**
 * @brief getFreeBlock
 * @param minSize
 * @return first block whose size is greater than minSize
 * Finds free space with given minimum size
 */
void* getFreeBlock(size_t minSize){
    void* block = memory;
    while(block < memory_end){
        BlockHeader blockHeader = readHeader(block);
        if(blockHeader.left == NULL && blockHeader.size >= minSize + getHeaderSize()){
            return block;
        }
        block = getNextBlock(block);
    }
    return NULL;
}

/**
 * @brief mergeBlocks
 * @param firstBlock
 * @param nextBlock
 * Merge two blocks into one, for internal use only - no checking for neighbourship, etc.
 */
void mergeBlocks(void* firstBlock, void* nextBlock){
    BlockHeader firstHeader = readHeader(firstBlock);
    BlockHeader nextHeader = readHeader(nextBlock);
    firstHeader.size += getHeaderSize() + nextHeader.size;
    writeHeader(firstBlock, firstHeader);
}

/**
 * @brief mergeWithPreviousEmptyBlock
 * @param block
 * Merging empty block with previous empty blocks if they exist
 */
void mergeWithPreviousEmptyBlock(void* block){
    void* prevBlock = memory;
    if(block == prevBlock)
        return;
    while(getNextBlock(prevBlock) != block){//here may be only one empty block
        prevBlock = getNextBlock(prevBlock);
    }
    BlockHeader prevBlockHeader = readHeader(prevBlock);
    if(prevBlockHeader.left == NULL){
        mergeBlocks(prevBlock, block);
    }
}

/**
 * @brief mergeWithNextEmptyBlock
 * @param block
 * Merging empty block with following empty block if exist
 */
void mergeWithNextEmptyBlock(void* block){
    void* nextBlock = getNextBlock(block);
    while(nextBlock < memory_end){
        BlockHeader nextHeader = readHeader(nextBlock);
        if(nextHeader.left == NULL){
            mergeBlocks(block, nextBlock);
        }
        else return;
        nextBlock = getNextBlock(block);
    }
}

/**
 * @brief switchBlocks
 * @param firstBlock
 * @param nextBlock
 * Swaps two neighbour blocks. for internal use only - no checking for neighbourship, etc.
 */
void switchBlocks(void* firstBlock, void* nextBlock){
    BlockHeader firstHeader = readHeader(firstBlock);
    BlockHeader nextHeader = readHeader(nextBlock);

    BlockHeader tmpHeader = firstHeader;
    firstHeader = nextHeader;
    writeHeader(firstBlock, firstHeader);
    memcpy(firstBlock + getHeaderSize(), nextBlock + getHeaderSize(), firstHeader.size);

    nextBlock = getNextBlock(firstBlock);
    nextHeader = tmpHeader;
    writeHeader(nextBlock, nextHeader);
}

/**
 * @brief defragment
 * Defragmentates memory
 */
void defragment(){
    void* block = memory;
    void* nextBlock = getNextBlock(block);
    while (nextBlock < memory_end)
    {
        BlockHeader blockHeader = readHeader(block);
        BlockHeader nextBlockHeader = readHeader(nextBlock);

        if (blockHeader.left != NULL){//engaged block, moving on
            block = nextBlock;
            nextBlock = getNextBlock(block);
            continue;
        }

        if (nextBlockHeader.left != NULL){//one is empty, second is engaged - switching blocks
            switchBlocks(block, nextBlock);
            nextBlock = getNextBlock(block);
        }
        else{
            mergeWithNextEmptyBlock(block);
            nextBlock = getNextBlock(block);
        }
    }
}

/**
 * @brief getNextAddr
 * @return virtual pointer
 * Returns next not used virtual pointer
 */
VA getNextAddr(){
    return ++lastAddr;
}

int _malloc (VA* ptr, size_t szBlock){
    void* block;

    if(szBlock <=0)
        return -1;
    if(szBlock >= memory_end - memory - getHeaderSize())
        return -2;
    block = getFreeBlock(szBlock);
    if(block == NULL){
        defragment();
        block = getFreeBlock(szBlock);
        if(block == NULL)
            return -2;
    }
    BlockHeader blockHeader = readHeader(block);
    size_t freeSize = blockHeader.size;
    *ptr = blockHeader.left = block - memory + 1;
    blockHeader.size = szBlock;
    writeHeader(block, blockHeader);

    BlockHeader newBlockheader = {NULL, freeSize - getHeaderSize() - blockHeader.size};//TODO:case when no empty space remains
    writeHeader(getNextBlock(block), newBlockheader);
    return 0;
}

int _free (VA ptr){
    void* block = getByVA(ptr);
    if (block == NULL){
        return -1;
    }
    BlockHeader blockHeader = readHeader(block);
    blockHeader.left = NULL;
    writeHeader(block, blockHeader);
    mergeWithNextEmptyBlock(block);//order is important
    mergeWithPreviousEmptyBlock(block);
    return 0;
}

int _read (VA ptr, void* pBuffer, size_t szBuffer){
    void* block = getByVA(ptr);
    if (block == NULL)
        return -1;
    BlockHeader header = readHeader(block);
    if (szBuffer + (ptr - header.left) > header.size)
        return -2;
    memcpy(pBuffer, block + getHeaderSize() + (ptr - header.left), szBuffer);
    return 0;
}

int _write (VA ptr, void* pBuffer, size_t szBuffer){
    void* block = getByVA(ptr);
    if(block == NULL)
        return -1;
    BlockHeader header = readHeader(block);
    if (szBuffer  + (ptr - header.left) > header.size)
        return -2;
    memcpy(block + getHeaderSize() + (ptr - header.left), pBuffer, szBuffer);
    return 0;
}

int _init(int n, size_t szPage){
    size_t memorySize = szPage*n;

    if (n <= 0 || szPage <= 0){
        return -1;
    }

    addrSize = ceil(log(memorySize)/log(2));

    lastAddr = 0;
    memory = malloc(memorySize);
    if (memory == NULL)
        return 1;

    BlockHeader h;
    h.left = NULL;
    h.size = memorySize - getHeaderSize();
    writeHeader(memory, h);
    memory_end = memory + memorySize;
    return 0;
}

int main(int argc, const char* argv[]){
    char *action = malloc(10);
    int result;
    int addr;
    int buff;
    VA va;
    size_t size;
    char* temp = strdup("qwerty");
    char* tmp2 = malloc(strlen(temp)*sizeof(char));

    _init(1, 1000);
    _malloc(&va, strlen(temp)*sizeof(char)+100);
    _write(va+2, temp, strlen(temp)*sizeof(char));
    _read(va+4, tmp2, strlen(temp)*sizeof(char)-2);

    printf("%s", temp);

    printf("enter memory size>> ");
    scanf("%d", &size);
    _init(1, size);


    while(action != 'q'){
        printf("enter action (h - help)>> ");scanf("%s", action);
        switch(action[0]){
        case 'p':
            printMemory();
            break;
        case 'm':
            printf("malloc: enter size>> "); scanf("%d", &size);
            result = _malloc(&va, size);
            if(result == -1)
                printf("invalid params\n");
            if(result == -2)
                printf("not enough memory\n");
            if(result == 1)
                printf("unknown error\n");
            break;
        case 'f':
            printf("free: enter addr>> ");   scanf("%d", &addr);
            result = _free(addr);
            if(result == -1)
                printf("invalid params\n");
            if(result == 1)
                printf("unknown error\n");
            break;
        case 'w':
            printf("write: enter addr>> ");  scanf("%d", &addr);
            printf("write: enter size>> ");  scanf("%d", &size);
            printf("write: enter data>> ");scanf("%d", &buff);
            result = _write(addr, &buff, size);
            if(result == -1)
                printf("invalid params\n");
            if(result == -2)
                printf("trying to access memory outside the block\n");
            if(result == 1)
                printf("unknown error\n");
            break;
        case 'r':
            printf("read: enter addr>>");  scanf("%d", &addr);
            printf("read: enter size>>");  scanf("%d", &size);
            result = _read(addr, &buff, size);
            if(result == -1)
                printf("invalid params\n");
            if(result == -2)
                printf("trying to access memory outside the block\n");
            if(result == 1)
                printf("unknown error\n");
            if(result == 0)
                printf("read result:%d\n", buff);
            break;
        case 'h':
            printf("p - print memory\n");
            printf("m - malloc\n");
            printf("f - free\n");
            printf("w - write (as unsigned integer, will will be scliced bitly by entered size)\n");
            printf("r - read (same as write, will be readed as unsigned integer)\n");
            printf("q - quit\n\n");
            break;
        }
    }
    return 0;
}
