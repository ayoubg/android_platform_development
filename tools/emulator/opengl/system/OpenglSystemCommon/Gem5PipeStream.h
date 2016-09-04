/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#ifndef __Gem5_PIPE_STREAM_H
#define __Gem5_PIPE_STREAM_H
/* This file implements an IOStream that uses a Gem5 psuedo instructions to communicate
 'opengles' commands. 
 */
#include <stdlib.h>
#include <pthread.h>
#include "IOStream.h"
extern "C" {
    #include "m5/m5op.h"
}

 /* TODO: Make this configurable with a system property */
 #define  GEM5_GRAPHICS_MEM_SIZE (17*1024*1024) //17 MB

//the start of gpu graphics calls, defined in src/api/cuda_syscalls.h. TODO: Can we add it to the build configs?
const unsigned GEM5_CUDA_CALLS_START = 100;
 enum gem5GraphicsCall {
    gem5_writeFully = GEM5_CUDA_CALLS_START,
    gem5_readFully,
    gem5_read,
    gem5_recv,
    gem5_graphics_mem,
    gem5_block,
    gem5_debug,
    gem5_call_buffer_fail,
    gem5_sim_active
};

enum gem5DebugCall {
   gmem_alloc_fail,
   gmem_lock_fail,
   pipe_mem_alloc_fail,
   gem5_info
};


class Gem5PipeMemory{
public:
   static Gem5PipeMemory Memory;
   Gem5PipeMemory();
   ~Gem5PipeMemory();
private: 
   static const int BuffersLimit = 1;
   struct MemBuffer{
      MemBuffer(){
         valid = false;
         ptr = NULL;
         size = 0;
      }
      bool valid;
      void* ptr;
      size_t size;
   }; 

   MemBuffer mBuffers[BuffersLimit];
   int currLockedBufIdx;
   int buffersCount;
   pthread_mutex_t mutexLock;
public:
   void* AllocateNewGBuffer(size_t size);
   void DeleteGBuffer(void*);
   bool LockGBuffer(void*);
   void UnlockGBuffer();
};

class Gem5PipeStream : public IOStream {
public:
    explicit Gem5PipeStream(size_t bufsize = 10000);
    ~Gem5PipeStream();
    
    virtual void *allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char *readFully( void *buf, size_t len);
    virtual const unsigned char *read( void *buf, size_t *inout_len);
    virtual int writeFully(const void *buf, size_t len);

    int recv(void *buf, size_t len);
    static bool is_graphics_mem_init;

private:
typedef struct gpucall {
    int pid;
    int tid;
    int total_bytes;
    int num_args;
    int* arg_lengths;
    char* args;
    char* ret;
} gpusyscall_t;

    size_t m_bufsize;
    unsigned char *m_buf;
    void *m_pggm;
    size_t m_pggm_size;
    void pack(char *bytes, int &bytes_off, int *lengths, int &lengths_off, char *arg, int arg_size);
    int doRWOperation(gem5GraphicsCall type, const void*buf, size_t len);
    int sendRWOperation(gem5GraphicsCall type, const void*buf, size_t len);
    inline bool needToLockUponSend(gem5GraphicsCall type);
    void allocGBuffer();
};

#endif
