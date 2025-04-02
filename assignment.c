#include <assert.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>

#define NUM_PROCS 4
#define CACHE_SIZE 4
#define MEM_SIZE 16
#define MSG_BUFFER_SIZE 256
#define MAX_INSTR_NUM 32

typedef unsigned char byte;

typedef enum { MODIFIED, EXCLUSIVE, SHARED, INVALID } cacheLineState;

// In addition to cache line states, each directory entry also has a state
// EM ( exclusive or modified ) : memory block is in only one cache.
//                  When a memory block in a cache is written to, it would have
//                  resulted in cache hit and transition from EXCLUSIVE to MODIFIED.
//                  Main memory / directory has no way of knowing of this transition.
//                  Hence, this state only cares that the memory block is in a single
//                  cache, and not whether its in EXCLUSIVE or MODIFIED.
// S ( shared )     : memory block is in multiple caches
// U ( unowned )    : memory block is not in any cache
typedef enum { EM, S, U } directoryEntryState;

typedef enum {
    READ_REQUEST,       // requesting node sends to home node on a read miss
    WRITE_REQUEST,      // requesting node sends to home node on a write miss
    REPLY_RD,           // home node replies with data to requestor for read request
    REPLY_WR,           // home node replies to requestor for write request
    REPLY_ID,           // home node replies with IDs of sharers to requestor
    INV,                // owner node asks sharers to invalidate
    UPGRADE,            // owner node asks home node to change state to EM
    WRITEBACK_INV,      // home node asks owner node to flush and change to INVALID
    WRITEBACK_INT,      // home node asks owner node to flush and change to SHARED
    FLUSH,              // owner flushes data to home + requestor
    FLUSH_INVACK,       // flush, piggybacking an InvAck message
    EVICT_SHARED,       // handle cache replacement of a shared cache line
    EVICT_MODIFIED      // handle cache replacement of a modified cache line
} transactionType;

// We will create our own address space which will be of size 1 byte
// LSB 4 bits indicate the location in memory
// MSB 4 bits indicate the processor it is present in.
// For example, 0x36 means the memory block at index 6 in the 3rd processor
typedef struct instruction {
    byte type;      // 'R' for read, 'W' for write
    byte address;
    byte value;     // used only for write operations
} instruction;

typedef struct cacheLine {
    byte address;           // this is the address in memory
    byte value;             // this is the value stored in cached memory
    cacheLineState state;   // state for you to implement MESI protocol
} cacheLine;

typedef struct directoryEntry {
    byte bitVector;         // each bit indicates whether that processor has this
                            // memory block in its cache
    directoryEntryState state;
} directoryEntry;

// Note that each message will contain values only in the fields which are relevant
// to the transactionType
typedef struct message {
    transactionType type;
    int sender;          // thread id that sent the message
    byte address;        // memory block address
    byte value;          // value in memory / cache
    byte bitVector;      // ids of sharer nodes
    int secondReceiver;  // used when you need to send a message to 2 nodes, where
                         // 1 node id is in the sender field
    directoryEntryState dirState;   // directory entry state of the memory block
} message;

typedef struct messageBuffer {
    message queue[ MSG_BUFFER_SIZE ];
    // a circular queue message buffer
    int head;
    int tail;
    int count;          // store total number of messages processed by the node
} messageBuffer;

typedef struct processorNode {
    cacheLine cache[ CACHE_SIZE ];
    byte memory[ MEM_SIZE ];
    directoryEntry directory[ MEM_SIZE ];
    instruction instructions[ MAX_INSTR_NUM ];
    int instructionCount;
} processorNode;

void initializeProcessor( int threadId, processorNode *node, char *dirName );
void sendMessage( int receiver, message msg );  // IMPLEMENT
void handleCacheReplacement( int sender, cacheLine oldCacheLine );  // IMPLEMENT
void printProcessorState( int processorId, processorNode node );

messageBuffer messageBuffers[ NUM_PROCS ];
// IMPLEMENT: DONE
// Create locks to ensure thread-safe access to each processor's message buffer.
omp_lock_t msgBufferLocks[ NUM_PROCS ];

int main( int argc, char * argv[] ) {
    if (argc < 2) {
        fprintf( stderr, "Usage: %s <test_directory>\n", argv[0] );
        return EXIT_FAILURE;
    }
    char *dirName = argv[1];

    // IMPLEMENT: DONE
    // set number of threads to NUM_PROCS
    omp_set_num_threads(NUM_PROCS);

    for ( int i = 0; i < NUM_PROCS; i++ ) {
        messageBuffers[ i ].count = 0;
        messageBuffers[ i ].head = 0;
        messageBuffers[ i ].tail = 0;
        // IMPLEMENT: DONE
        // initialize the locks in msgBufferLocks
        omp_init_lock(&msgBufferLocks[i]);
    }
    processorNode node;

    // IMPLEMENT: DONE
    // Create the omp parallel region with an appropriate data environment
    #pragma omp parallel private(node)
    {
        int threadId = omp_get_thread_num();
        initializeProcessor( threadId, &node, dirName );
        // IMPLEMENT: DONE
        // wait for all processors to complete initialization before proceeding
        #pragma omp barrier

        message msg;
        message msgReply;
        instruction instr;
        int instructionIdx = -1;
        int printProcState = 1;         // control how many times to dump processor
        byte waitingForReply = 0;       // if a processor is waiting for a reply
                                        // then dont proceed to next instruction
                                        // as current instruction has not finished
        while ( 1 ) {
            // Process all messages in message queue first
            while (
                messageBuffers[ threadId ].count > 0 &&
                messageBuffers[ threadId ].head != messageBuffers[ threadId ].tail
            ) {
                if ( printProcState == 0 ) {
                    printProcState++;
                }
                int head = messageBuffers[ threadId ].head;
                msg = messageBuffers[ threadId ].queue[ head ];
                messageBuffers[ threadId ].head = ( head + 1 ) % MSG_BUFFER_SIZE;

                #ifdef DEBUG_MSG
                printf( "Processor %d msg from: %d, type: %d, address: 0x%02X\n",
                        threadId, msg.sender, msg.type, msg.address );
                #endif /* ifdef DEBUG */

                // IMPLEMENT: DONE
                // extract procNodeAddr and memBlockAddr from message address

                // Upper 4 bits of address indicate the processor
                // Lower 4 bits of address indicate the memory index
                byte procNodeAddr = (msg.address >> 4) & 0x0F;
                byte memBlockAddr = msg.address & 0x0F;
                byte cacheIndex = memBlockAddr % CACHE_SIZE;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        // IMPLEMENT: DONE
                        // This is in the home node
                        // If directory state is,
                        // U: update directory and send value using REPLY_RD
                        // S: update directory and send value using REPLY_RD
                        // EM: forward request to the current owner node for
                        //     writeback intervention using WRITEBACK_INT

                        // Handle read miss in requesting processor

                        directoryEntryState readMemState = node.directory[memBlockAddr].state;

                        if (readMemState == U){

                            // U (Unowned): No processor has the memory block in its cache
                            // 1. Send memory value to requestor using REPLY_RD
                            // 2. Update directory state from U to EM
                            // 3. Set bit vector to indicate requestor now has the block exclusively

                            msgReply = (message) {
                                .type = REPLY_RD,
                                .sender = threadId,
                                .address = msg.address,
                                .value = node.memory[memBlockAddr],
                                .dirState = EM
                            };

                            sendMessage(msg.sender, msgReply);

                            node.directory[memBlockAddr].state = EM;
                            node.directory[memBlockAddr].bitVector = 1 << msg.sender;
                        } else if (readMemState == S){


                            // S (Shared): Multiple processors have read-only copies of the block
                            // 1. Send memory value to requestor using REPLY_RD
                            // 2. Update bit vector to include requestor as a new sharer
                            // 3. Maintain S state as the block is still shared

                            msgReply = (message) {
                                .type = REPLY_RD,
                                .sender = threadId,
                                .address = msg.address,
                                .value = node.memory[ memBlockAddr ],
                                .dirState = S
                            };

                            sendMessage(msg.sender, msgReply);


                            node.directory[memBlockAddr].bitVector |= 1 << msg.sender;

                        } else if (readMemState == EM){

                            // EM (Exclusive/Modified): Block is owned exclusively by one processor
                            // 1. Cannot directly send value since another processor might have modified it
                            // 2. Send WRITEBACK_INT to current owner to flush data
                            // 3. Owner will send data to both home node and requestor
                            // 4. This ensures memory coherence by getting the latest value

                            msgReply = (message) {
                                .type = WRITEBACK_INT,
                                .sender = threadId,
                                .address = msg.address,
                                .secondReceiver = msg.sender,
                            };

                            int oldOwner = __builtin_ctz(node.directory[memBlockAddr].bitVector);
                            sendMessage(oldOwner, msgReply);
                        }
                        break;

                    case REPLY_RD:
                        // IMPLEMENT: DONE
                        // This is in the requesting node
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        // Read in the memory block sent in the message to cache
                        // Handle state of the memory block appropriately

                        // 1. Check if the cache line is already occupied with a different block. If so, call handleCacheReplacement to evict the old block to make room.
                        // 2. Update the cache line with the received memory block
                        // 3. Clears the waitingForReply flag to allow processor to continue indicating that the read transaction has completed

                        if (node.cache[cacheIndex].address != msg.address && node.cache[cacheIndex].state != INVALID) {
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        node.cache[cacheIndex].state = (msg.dirState == S) ? SHARED : EXCLUSIVE;

                        waitingForReply = 0;

                        break;

                    case WRITEBACK_INT:
                        // IMPLEMENT: DONE
                        // This is in old owner node which contains a cache line in
                        // MODIFIED state
                        // Flush this value to the home node and the requesting node
                        // using FLUSH
                        // Change cacheline state to SHARED
                        // If home node is the requesting node, avoid sending FLUSH
                        // twice

                        // 1. Create a FLUSH message containing the current (potentially modified) value
                        // 2. Send the FLUSH message to:
                        //    - The home node (procNodeAddr) to update main memory
                        //    - The requesting node (msg.secondReceiver) to provide it with the latest value
                        // 3. Avoid sending duplicate messages to the same processor by checking if home node is also the requestor
                        // 4. Change the cache line state from EM (EXCLUSIVE/MODIFIED) to SHARED
                        msgReply = (message) {
                            .type = FLUSH,
                            .sender = threadId,
                            .address = msg.address,
                            .value = node.cache[ cacheIndex ].value,
                            .secondReceiver = msg.secondReceiver,
                        };
                        sendMessage( procNodeAddr, msgReply );

                        if (procNodeAddr != msg.secondReceiver)
                            sendMessage( msg.secondReceiver, msgReply );

                        node.cache[cacheIndex].state = SHARED;
                        break;

                    case FLUSH:
                        // IMPLEMENT: DONE
                        // If in home node,
                        // update directory state and bitvector appropriately
                        // update memory value
                        //
                        // If in requesting node, load block into cache
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        //
                        // IMPORTANT: there can be cases where home node is same as
                        // requesting node, which need to be handled appropriately

                        // 1. If this processor is the home node (procNodeAddr):
                        //    - Update directory state to S (Shared)
                        //    - Update bit vector to include the requesting processor
                        //    - Update memory with the latest value from the cache that had it
                        // 2. If this processor is the requesting node (msg.secondReceiver):
                        //    - Handle cache replacement if necessary
                        //    - Update cache with the received memory block
                        //    - Set the state to SHARED since the block now exists in multiple caches
                        // 3. Clear the waitingForReply flag to allow processor to continue indicating that the read transaction has completed
                        if (threadId == procNodeAddr) {
                            node.directory[memBlockAddr].state = S;
                            node.directory[memBlockAddr].bitVector |= (1 << msg.secondReceiver);

                            node.memory[memBlockAddr] = msg.value;
                        }

                        if (threadId == msg.secondReceiver) {
                            if (node.cache[cacheIndex].address != msg.address &&
                                node.cache[cacheIndex].state != INVALID) {
                                handleCacheReplacement(threadId, node.cache[cacheIndex]);
                            }
                            node.cache[cacheIndex].address = msg.address;
                            node.cache[cacheIndex].value = msg.value;
                            node.cache[cacheIndex].state = SHARED;
                        }

                        waitingForReply = 0;
                        break;

                    case UPGRADE:
                        // IMPLEMENT: DONE
                        // This is in the home node
                        // The requesting node had a write hit on a SHARED cacheline
                        // Send list of sharers to requesting node using REPLY_ID
                        // Update directory state to EM, and bit vector to only have
                        // the requesting node set
                        // IMPORTANT: Do not include the requesting node in the
                        // sharers list


                        // 1. Create a bit vector of other sharers excluding the requesting processor to identify all processors that need to be invalidated
                        // 2. Send REPLY_ID to the requesting processor with this sharers list
                        // 3. Update directory state to EM (Exclusive/Modified) as only one processor now has the block
                        // 4. Update bit vector to include only the requesting processor to indicate that the requesting processor is now the exclusive owner

                        byte otherSharers = node.directory[memBlockAddr].bitVector & (~(1 << msg.sender));

                        msgReply = (message) {
                            .type = REPLY_ID,
                            .sender = threadId,
                            .address = msg.address,
                            .bitVector = otherSharers,
                        };
                        sendMessage( msg.sender, msgReply );

                        node.directory[memBlockAddr].state = EM;
                        node.directory[memBlockAddr].bitVector = (1 << msg.sender);

                        break;

                    case REPLY_ID:
                        // IMPLEMENT: DONE
                        // This is in the requesting ( new owner ) node
                        // The owner node recevied the sharers list from home node
                        // Invalidate all sharers' entries using INV
                        // Handle cache replacement if needed, and load the block
                        // into the cacheline
                        // NOTE: Ideally, we should update the owner node cache line
                        // after we receive INV_ACK from every sharer, but for that
                        // we will have to keep track of all the INV_ACKs.
                        // Instead, we will assume that INV does not fail.


                        // 1. Send INV (invalidate) messages to all other processors sharing this block
                        // 2. Handle cache replacement if necessary for the current cache line
                        // 3. Update the cache line with the memory block and writes the new value
                        // 4. Clears the waitingForReply flag to allow processor to continue to indicate that the write transaction has completed

                        for (int i = 0; i < NUM_PROCS; i++) {
                            if (msg.bitVector & (1 << i)) {
                                msgReply = (message) {
                                    .type = INV,
                                    .sender = threadId,
                                    .address = msg.address,
                                };
                                sendMessage(i, msgReply);
                            }
                        }

                        if (node.cache[cacheIndex].address != msg.address && node.cache[cacheIndex].state != INVALID) {
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }

                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = instr.value;
                        node.cache[cacheIndex].state = MODIFIED;

                        waitingForReply = 0;
                        break;

                    case INV:
                        // IMPLEMENT: DONE
                        // This is in the sharer node
                        // Invalidate the cache entry for memory block
                        // If the cache no longer has the memory block ( replaced by
                        // a different block ), then do nothing

                        // 1. Check if the processor actually has the memory block in its cache
                        // 2. If the block is present, invalidate it by changing state to INVALID
                        if (node.cache[cacheIndex].address == msg.address) {
                            node.cache[cacheIndex].state = INVALID;
                        }
                        break;

                    case WRITE_REQUEST:
                        // IMPLEMENT: DONE
                        // This is in the home node
                        // Write miss occured in requesting node
                        // If the directory state is,
                        // U:   no cache contains this memory block, and requesting
                        //      node directly becomes the owner node, use REPLY_WR
                        // S:   other caches contain this memory block in clean state
                        //      which have to be invalidated
                        //      send sharers list to new owner node using REPLY_ID
                        //      update directory
                        // EM:  one other cache contains this memory block, which
                        //      can be in EXCLUSIVE or MODIFIED
                        //      send WRITEBACK_INV to the old owner, to flush value
                        //      into memory and invalidate cacheline


                        directoryEntryState writeMemState = node.directory[memBlockAddr].state;

                        if (writeMemState == U) {

                            // 1. U (Unowned): No processor has the memory block in its cache
                            //    - Send REPLY_WR to indicate no invalidations needed
                            //    - Update directory to show requestor is now exclusive owner

                            msgReply = (message) {
                                .type = REPLY_WR,
                                .sender = threadId,
                                .address = msg.address,
                            };

                            sendMessage(msg.sender, msgReply);
                        } else if (writeMemState == S) {


                            // 2. S (Shared): Multiple processors have read-only copies
                            //    - Create bit vector of other sharers excluding the requestor
                            //    - Send REPLY_ID so requestor can invalidate all other copies
                            //    - Update directory to show requestor is now exclusive owner

                            byte otherSharers = node.directory[memBlockAddr].bitVector & (~(1 << msg.sender));
                            msgReply = (message) {
                                .type = REPLY_ID,
                                .sender = threadId,
                                .address = msg.address,
                                .bitVector = otherSharers,
                            };

                            sendMessage(msg.sender, msgReply);
                        } else if (writeMemState == EM) {

                            // 3. EM (Exclusive/Modified): One processor owns the block exclusively
                            //    - Send WRITEBACK_INV to current owner to flush and invalidate
                            //    - This ensures memory coherence by getting latest value
                            //    - Update directory to show requestor is now owner

                            msgReply = (message) {
                                .type = WRITEBACK_INV,
                                .sender = threadId,
                                .address = msg.address,
                                .value = msg.value,
                                .secondReceiver = msg.sender,
                            };

                            int oldOwner = __builtin_ctz(node.directory[memBlockAddr].bitVector);
                            sendMessage(oldOwner, msgReply);
                        }

                        node.directory[memBlockAddr].state = EM;
                        node.directory[memBlockAddr].bitVector = (1 << msg.sender);

                        break;

                    case REPLY_WR:
                        // IMPLEMENT: DONE
                        // This is in the requesting (new owner) node
                        // Handle cache replacement if needed, and load the memory
                        // block into cache

                        // 1. Handle cache replacement if necessary
                        // 2. Update the cache line with the memory block address
                        //    - Store the new value from the write instruction
                        //    - Set state to MODIFIED to indicate the block has been changed
                        // 3. Clears the waitingForReply flag to allow processor to continue
                        // 4. Since no other processor had the block (U state), no invalidations are needed

                        handleCacheReplacement(threadId, node.cache[cacheIndex]);

                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = instr.value;
                        node.cache[cacheIndex].state = MODIFIED;

                        waitingForReply = 0;
                        break;

                    case WRITEBACK_INV:
                        // IMPLEMENT: DONE
                        // This is in the old owner node
                        // Flush the currrent value to home node using FLUSH_INVACK
                        // Send an ack to the requesting node which will become the
                        // new owner node
                        // If home node is the new owner node, dont send twice
                        // Invalidate the cacheline

                        // 1. Create a FLUSH_INVACK message containing the current value
                        // 2. Send the message to:
                        //    - The home node to update main memory
                        //    - The requesting node to provide it with the latest value
                        // 3. Invalidate its own copy of the cache line to ensure it will not use old data in the future\

                        msgReply = (message) {
                            .type = FLUSH_INVACK,
                            .sender = threadId,
                            .address = msg.address,
                            .value = node.cache[ cacheIndex ].value,
                            .secondReceiver = msg.secondReceiver,
                        };
                        sendMessage(procNodeAddr, msgReply);

                        sendMessage(msg.secondReceiver, msgReply);

                        node.cache[cacheIndex].state = INVALID;
                        break;

                    case FLUSH_INVACK:
                        // IMPLEMENT: DONE
                        // If in home node, update directory and memory
                        // The bit vector should have only the new owner node set
                        // Flush the value from the old owner to memory
                        //
                        // If in requesting node, handle cache replacement if needed,
                        // and load block into cache

                        // 1. If this processor is the home node:
                        //    - Update bit vector to include only the new owner (msg.secondReceiver)
                        //    - Update memory with the latest value from the old owner
                        // 2. If this processor is the requesting node:
                        //    - Handle cache replacement if necessary
                        //    - Update cache with the memory block address
                        //    - Write the new value (from instruction) to the cache line
                        //    - Set state to MODIFIED to indicate it's been changed
                        // 3. Clear the waitingForReply flag to allow processor to continue

                        if (threadId == procNodeAddr) {
                            node.directory[memBlockAddr].bitVector = (1 << msg.secondReceiver);

                            node.memory[memBlockAddr] = msg.value;
                        }

                        if (threadId == msg.secondReceiver) {
                            if (node.cache[ cacheIndex ].address != msg.address &&
                                node.cache[ cacheIndex ].state != INVALID) {
                                handleCacheReplacement( threadId, node.cache[ cacheIndex ] );
                            }
                            node.cache[ cacheIndex ].address = msg.address;
                            node.cache[ cacheIndex ].value = instr.value;
                            node.cache[ cacheIndex ].state = MODIFIED;
                        }

                        waitingForReply = 0;
                        break;

                    case EVICT_SHARED:
                        // IMPLEMENT: DONE
                        // If in home node,
                        // Requesting node evicted a cacheline which was in SHARED
                        // Remove the old node from bitvector,
                        // if no more sharers exist, change directory state to U
                        // if only one sharer exist, change directory state to EM
                        // Inform the remaining sharer ( which will become owner ) to
                        // change from SHARED to EXCLUSIVE using EVICT_SHARED
                        //
                        // If in remaining sharer ( new owner ), update cacheline
                        // from SHARED to EXCLUSIVE

                        // 1. Update the bit vector to remove the evicting processor
                        // 2. Determine how many processors still have the block:
                        //    - If none (numSharers == 0): changes directory state to U (Unowned)
                        //    - If one (numSharers == 1): changes directory state to EM
                        //      - Also send EVICT_SHARED to the remaining processor
                        //      - This notifies it to change from SHARED to EXCLUSIVE
                        // 3. If this is the remaining sharer receiving EVICT_SHARED then update cache line state from SHARED to EXCLUSIVE

                        if (threadId == procNodeAddr) {
                            node.directory[memBlockAddr].bitVector &= ~(1 << msg.sender);

                            int numSharers = __builtin_popcount(node.directory[memBlockAddr].bitVector);
                            if (numSharers == 0) {
                                node.directory[memBlockAddr].state = U;
                            } else if (numSharers == 1) {
                                node.directory[memBlockAddr].state = EM;

                                int newOwner = __builtin_ctz(node.directory[memBlockAddr].bitVector);
                                msg = (message) {
                                    .type = EVICT_SHARED,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = node.memory[ memBlockAddr ],
                                };
                                sendMessage( newOwner, msg );
                            }
                        } else {
                            node.cache[ cacheIndex ].state = EXCLUSIVE;
                        }
                        break;

                    case EVICT_MODIFIED:
                        // IMPLEMENT: DONE
                        // This is in home node,
                        // Requesting node evicted a cacheline which was in MODIFIED
                        // Flush value to memory
                        // Remove the old node from bitvector HINT: since it was in
                        // modified state, not other node should have had that
                        // memory block in a valid state its cache

                        // 1. Updates memory with the latest value from the evicting cache
                        // 2. Clears the bit vector (sets to 0) since no processor has the block in its cache
                        // 3. Changes directory state to U (Unowned)

                        node.memory[memBlockAddr] = msg.value;

                        node.directory[memBlockAddr].bitVector = 0;
                        node.directory[memBlockAddr].state = U;
                        break;
                }
                // Decrease message count
                messageBuffers[threadId].count --;
            }

            // Check if we are waiting for a reply message
            // if yes, then we have to complete the previous instruction before
            // moving on to the next
            if ( waitingForReply > 0 ) {
                continue;
            }

            // Process an instruction
            if ( instructionIdx < node.instructionCount - 1 ) {
                instructionIdx++;
            } else {
                if ( printProcState > 0 ) {
                    printProcessorState( threadId, node );
                    printProcState--;
                }
                // even though there are no more instructions, this processor might
                // still need to react to new transaction messages
                //
                // once all the processors are done printing and appear to have no
                // more network transactions, please terminate the program by sending
                // a SIGINT ( CTRL+C )
                continue;
            }
            instr = node.instructions[ instructionIdx ];

            #ifdef DEBUG_INSTR
            printf( "Processor %d: instr type=%c, address=0x%02X, value=%hhu\n",
                    threadId, instr.type, instr.address, instr.value );
            #endif

            // IMPLEMENT: DONE
            // Extract the home node's address and memory block index from
            // instruction address
            byte procNodeAddr = (instr.address >> 4) & 0x0F;
            byte memBlockAddr = instr.address & 0x0F;
            byte cacheIndex = memBlockAddr % CACHE_SIZE;
            // Determine if the requested memory block is in the cache
            int cacheHit = node.cache[cacheIndex].address == instr.address
                          ? node.cache[cacheIndex].state == INVALID ? 0 : 1
                          : 0;

            if ( instr.type == 'R' ) {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and the cacheline state is not invalid, then use
                // that value. no transaction/transition takes place so no work.
                //
                // if cacheline is invalid, or memory block is not present, it is
                // treated as a read miss
                // send a READ_REQUEST to home node on a read miss


                // 1. If cache hit:
                //    - No action needed - can use the cached value directly
                // 2. If cache miss:
                //    - Create a READ_REQUEST message to the home node
                //    - Set waitingForReply flag to pause instruction processing
                //    - The processor will resume once a reply is received

                if (!cacheHit) {
                    /* memory block not in cache, send READ_REQUEST to home node */
                    msg = (message) {
                        .type = READ_REQUEST,
                        .sender = threadId,
                        .address = instr.address,
                    };
                    sendMessage( procNodeAddr, msg );
                    waitingForReply = 1;
                }
            } else {
                // IMPLEMENT: DONE
                // check if memory block is present in cache
                //
                // if cache hit and cacheline state is not invalid, then it is a
                // write hit
                // if modified or exclusive, update cache directly as only this node
                // contains the memory block, no network transactions required
                // if shared, other nodes contain this memory block, request home
                // node to send list of sharers. This node will have to send an
                // UPGRADE to home node to promote directory from S to EM, and also
                // invalidate the entries in the sharers
                //
                // if cache miss or cacheline state is invalid, then it is a write
                // miss
                // send a WRITE_REQUEST to home node on a write miss

                // 1. For write hits:
                //    - If cache line is MODIFIED/EXCLUSIVE:
                //       - Update cache value directly and sets state to MODIFIED
                //    - If cache line is SHARED:
                //       - Send UPGRADE request to home node
                //       - Set waitingForReply flag to pause instruction processing
                //       - Home node will reply with list of sharers to invalidate
                // 2. For write misses:
                //    - Create a WRITE_REQUEST message to home node
                //    - Set waitingForReply flag to pause instruction processing
                //    - Home node will manage invalidations and data transfer

                if (cacheHit) {

                    if (node.cache[cacheIndex].state == MODIFIED || node.cache[cacheIndex].state == EXCLUSIVE) {
                        node.cache[cacheIndex].value = instr.value;
                        node.cache[cacheIndex].state = MODIFIED;
                    } else {

                        msg = (message) {
                            .type = UPGRADE,
                            .sender = threadId,
                            .address = instr.address,
                            .value = instr.value
                        };
                        sendMessage(procNodeAddr, msg);
                        waitingForReply = 1;
                    }
                } else {
                    msg = (message) {
                        .type = WRITE_REQUEST,
                        .sender = threadId,
                        .address = instr.address,
                        .value = instr.value
                    };
                    sendMessage(procNodeAddr, msg);
                    waitingForReply = 1;
                }
            }

        }
    }
}

void sendMessage( int receiver, message msg ) {
    // IMPLEMENT: DONE
    // Ensure thread safety while adding a message to the receiver's buffer
    // Manage buffer indices correctly to maintain a circular queue structure

    // 1. Acquire the lock for the receiver's message buffer
    // 2. Check if the message buffer has space available
    //    - Add the message if space is available
    //    - Update tail pointer to maintain circular queue structure
    //    - Increment count to track number of messages in buffer
    // 3. Releases the lock when done

    omp_set_lock( &msgBufferLocks[ receiver ] );

    messageBuffer *buf = &messageBuffers[ receiver ];
    if (buf->count < MSG_BUFFER_SIZE) {
      buf->queue[ buf->tail ] = msg;
      buf->tail = (buf->tail + 1) % MSG_BUFFER_SIZE;
      buf->count++;
    }

    omp_unset_lock( &msgBufferLocks[ receiver ] );

}

void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    // IMPLEMENT: DONE
    // Notify the home node before a cacheline gets replaced
    // Extract the home node's address and memory block index from cacheline address
    byte memBlockAddr = oldCacheLine.address & 0x0F;
    byte procNodeAddr = oldCacheLine.address >> 4 & 0x0F;
    message msgSend;

    switch (oldCacheLine.state) {
        case EXCLUSIVE:
        case SHARED:
            // IMPLEMENT: DONE
            // If cache line was shared or exclusive, inform home node about the
            // eviction
            msgSend = (message) {
                .type = EVICT_SHARED,
                .sender = sender,
                .address = oldCacheLine.address,
            };
            sendMessage(procNodeAddr, msgSend);
            break;
        case MODIFIED:
            // IMPLEMENT: DONE
            // If cache line was modified, send updated value to home node
            // so that memory can be updated before eviction
            msgSend = (message) {
                .type = EVICT_MODIFIED,
                .sender = sender,
                .address = oldCacheLine.address,
                .value = oldCacheLine.value,
            };
            sendMessage(procNodeAddr, msgSend);
            break;
        case INVALID:
            // No action required for INVALID state
            break;
    }
}

void initializeProcessor( int threadId, processorNode *node, char *dirName ) {
    // IMPORTANT: DO NOT MODIFY
    for ( int i = 0; i < MEM_SIZE; i++ ) {
        node->memory[ i ] = 20 * threadId + i;  // some initial value to mem block
        node->directory[ i ].bitVector = 0;     // no cache has this block at start
        node->directory[ i ].state = U;         // this block is in Unowned state
    }

    for ( int i = 0; i < CACHE_SIZE; i++ ) {
        node->cache[ i ].address = 0xFF;        // this address is invalid as we can
                                                // have a maximum of 8 nodes in the
                                                // current implementation
        node->cache[ i ].value = 0;
        node->cache[ i ].state = INVALID;       // all cache lines are invalid
    }

    // read and parse instructions from core_<threadId>.txt
    char filename[ 128 ];
    snprintf(filename, sizeof(filename), "tests/%s/core_%d.txt", dirName, threadId);
    FILE *file = fopen( filename, "r" );
    if ( !file ) {
        fprintf( stderr, "Error: count not open file %s\n", filename );
        exit( EXIT_FAILURE );
    }

    char line[ 20 ];
    node->instructionCount = 0;
    while ( fgets( line, sizeof( line ), file ) &&
            node->instructionCount < MAX_INSTR_NUM ) {
        if ( line[ 0 ] == 'R' && line[ 1 ] == 'D' ) {
            sscanf( line, "RD %hhx",
                    &node->instructions[ node->instructionCount ].address );
            node->instructions[ node->instructionCount ].type = 'R';
            node->instructions[ node->instructionCount ].value = 0;
        } else if ( line[ 0 ] == 'W' && line[ 1 ] == 'R' ) {
            sscanf( line, "WR %hhx %hhu",
                    &node->instructions[ node->instructionCount ].address,
                    &node->instructions[ node->instructionCount ].value );
            node->instructions[ node->instructionCount ].type = 'W';
        }
        node->instructionCount++;
    }

    fclose( file );
    printf( "Processor %d initialized\n", threadId );
}

void printProcessorState(int processorId, processorNode node) {
    // IMPORTANT: DO NOT MODIFY
    static const char *cacheStateStr[] = { "MODIFIED", "EXCLUSIVE", "SHARED",
                                           "INVALID" };
    static const char *dirStateStr[] = { "EM", "S", "U" };

    char filename[32];
    snprintf(filename, sizeof(filename), "core_%d_output.txt", processorId);

    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return;
    }

    fprintf(file, "=======================================\n");
    fprintf(file, " Processor Node: %d\n", processorId);
    fprintf(file, "=======================================\n\n");

    // Print memory state
    fprintf(file, "-------- Memory State --------\n");
    fprintf(file, "| Index | Address |   Value  |\n");
    fprintf(file, "|----------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %5d   |\n", i, (processorId << 4) + i,
                node.memory[i]);
    }
    fprintf(file, "------------------------------\n\n");

    // Print directory state
    fprintf(file, "------------ Directory State ---------------\n");
    fprintf(file, "| Index | Address | State |    BitVector   |\n");
    fprintf(file, "|------------------------------------------|\n");
    // Had to modify bitVector since I am using MinGW
    for (int i = 0; i < MEM_SIZE; i++) {
        byte bv = node.directory[i].bitVector;
        fprintf(file, "|  %3d  |  0x%02X   |  %2s   |   0x%c%c%c%c%c%c%c%c   |\n",
                i, (processorId << 4) + i, dirStateStr[node.directory[i].state],
                (bv & 0x80) ? '1' : '0', (bv & 0x40) ? '1' : '0',
                (bv & 0x20) ? '1' : '0', (bv & 0x10) ? '1' : '0',
                (bv & 0x08) ? '1' : '0', (bv & 0x04) ? '1' : '0',
                (bv & 0x02) ? '1' : '0', (bv & 0x01) ? '1' : '0');
    }
    fprintf(file, "--------------------------------------------\n\n");

    // Print cache state
    fprintf(file, "------------ Cache State ----------------\n");
    fprintf(file, "| Index | Address | Value |    State    |\n");
    fprintf(file, "|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %3d  |  %8s \t|\n",
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    fprintf(file, "----------------------------------------\n\n");

    fclose(file);
}
