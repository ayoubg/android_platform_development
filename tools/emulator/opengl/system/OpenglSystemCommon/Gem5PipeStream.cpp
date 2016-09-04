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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <cutils/log.h>
#include <utils/CallStack.h>
#include "Gem5PipeStream.h"

#define  LOG_TAG  "gem5-tag"

void dumpStack(){
    android::CallStack a;
    a.update();
    a.dump();
}

#ifdef GEM5PIPE_DBGSTACK
#define DBGSTACK(...)    dumpStack()
#else
#define DBGSTACK(...)    ((void)0)
#endif

//Gem5PipeMemory
Gem5PipeMemory Gem5PipeMemory::Memory;

Gem5PipeMemory::Gem5PipeMemory(){
   buffersCount = 0;
   currLockedBufIdx = -1;
   pthread_mutex_init(&mutexLock, 0);
}

Gem5PipeMemory::~Gem5PipeMemory(){
   pthread_mutex_destroy(&mutexLock);
}

void* Gem5PipeMemory::AllocateNewGBuffer(size_t size){
   pthread_mutex_lock(&mutexLock);
   DBG("allocating graphics memory, size= %d \n", size);
   if(buffersCount<BuffersLimit){
      buffersCount++;
   } else {
      //limit reached
      DBG("graphics buffers limit reached");
      pthread_mutex_unlock(&mutexLock);
      return NULL;
   }

   void* newBuf = malloc(size);
   if(newBuf == NULL){
      DBG("failed to allocate memory for graphics");
      pthread_mutex_unlock(&mutexLock);
      return NULL;
   }

   for(int i=0; i<BuffersLimit; i++){
      if(!mBuffers[i].valid){
         mBuffers[i].valid = true;
         mBuffers[i].ptr = newBuf;
         mBuffers[i].size = size; 
         pthread_mutex_unlock(&mutexLock);
         return newBuf;
      }
   }

   //we shouldn't get to this point
   pthread_mutex_unlock(&mutexLock);
   return NULL;
}

void Gem5PipeMemory::DeleteGBuffer(void* buf){
   pthread_mutex_lock(&mutexLock);
   for(int i=0; i<BuffersLimit; i++){
      if(mBuffers[i].valid && (mBuffers[i].ptr == buf)){
         if(i==currLockedBufIdx){
            munlock(mBuffers[i].ptr, mBuffers[i].size);
            currLockedBufIdx = -1;
         }
         free(mBuffers[i].ptr);
         mBuffers[i].valid = false;
         buffersCount--;
         pthread_mutex_unlock(&mutexLock);
         return;
      }
   }
   //buffer not found!
   DBG("failed to delete graphics buffer, buffer not found!");
   pthread_mutex_unlock(&mutexLock);
   return;
}

bool Gem5PipeMemory::LockGBuffer(void* buf){
   pthread_mutex_lock(&mutexLock);
   if(currLockedBufIdx != -1){
      if(mBuffers[currLockedBufIdx].valid){
         //if already locked then return
        if(mBuffers[currLockedBufIdx].ptr == buf){
           pthread_mutex_unlock(&mutexLock);
           return true;
        }
        munlock(mBuffers[currLockedBufIdx].ptr,mBuffers[currLockedBufIdx].size);
        currLockedBufIdx = -1;
      }
   }

   for(int i=0; i<BuffersLimit; i++){
      if(mBuffers[i].valid && (mBuffers[i].ptr == buf)){
         if(mlock(mBuffers[i].ptr, mBuffers[i].size) != 0){
            //failed
            pthread_mutex_unlock(&mutexLock);
            return false;
         }
         currLockedBufIdx = i;
         pthread_mutex_unlock(&mutexLock);
         return true;
      }
   }

   //we shouldn't get to this point if locking succeeds
   pthread_mutex_unlock(&mutexLock);
   return false;
}


void Gem5PipeMemory::UnlockGBuffer(){
   pthread_mutex_lock(&mutexLock);
   if(currLockedBufIdx != -1){
      if(mBuffers[currLockedBufIdx].valid){
        munlock(mBuffers[currLockedBufIdx].ptr, mBuffers[currLockedBufIdx].size);
        currLockedBufIdx = -1;
      }
   }
   
   //we shouldn't get to this point if locking succeeds
   pthread_mutex_unlock(&mutexLock);
}

Gem5PipeStream::Gem5PipeStream(size_t bufSize) :
IOStream(bufSize),
m_bufsize(bufSize),
m_buf(NULL), m_pggm(NULL)
{
   DBG("called Gem5PipeStream::allocBuffer with size of %d \n",bufSize);
   rlimit limitS;
   getrlimit(RLIMIT_MEMLOCK, &limitS);
   sendRWOperation(gem5_debug, (void*)gem5_info, limitS.rlim_cur);
   sendRWOperation(gem5_debug, (void*)gem5_info, limitS.rlim_max);
}

Gem5PipeStream::~Gem5PipeStream()
{
   if (m_buf != NULL)
      free(m_buf);
   if (m_pggm !=NULL){
      Gem5PipeMemory::Memory.DeleteGBuffer(m_pggm);
   }
}

void *Gem5PipeStream::allocBuffer(size_t minSize)
{
    DBG("calling Gem5PipeStream::allocBuffer with size of %d \n",minSize);
    DBGSTACK();

    size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
    if (!m_buf) {
        m_buf = (unsigned char *)malloc(allocSize);
    }
    else if (m_bufsize < allocSize) {
        unsigned char *p = (unsigned char *)realloc(m_buf, allocSize);
        if (p != NULL) {
            m_buf = p;
            m_bufsize = allocSize;
        } else {
            sendRWOperation(gem5_debug, (void*)pipe_mem_alloc_fail, 0);
            ERR("realloc (%d) failed\n", allocSize);
            free(m_buf);
            m_buf = NULL;
            m_bufsize = 0;
        }
    }

    //tounching the buffer to allocate pages in OS
    for(void* ptr=m_buf; ptr< (m_buf+allocSize); ptr+=0x1000) //0x1000 = page size
    {
        *((char*)ptr) = 0;
    }

    //just in case the buffer is not aligned on page size
    void * ptr = m_buf + allocSize - 1;
    *((char*)ptr) = 0;

    return m_buf;
};

void Gem5PipeStream::pack(char *bytes, int &bytes_off, int *lengths, int &lengths_off, char *arg, int arg_size)
{
    for (int i = 0; i < arg_size; i++) {
        bytes[bytes_off + i] = *arg;
        arg++;
    }
    *(lengths + lengths_off) = arg_size;

    bytes_off += arg_size;
    lengths_off += 1;
}

int Gem5PipeStream::doRWOperation(gem5GraphicsCall type, const void* buff, size_t len){

    DBG("calling Gem5PipeStream::doRWOperation calling %d with buf=%x  and len=%d \n",type, (unsigned int)buff,len);
    //DBGSTACK();

    gpusyscall_t call_params;
    call_params.pid = getpid();
    call_params.tid = gettid();
    call_params.num_args = 2;
    call_params.arg_lengths = new int[call_params.num_args];

    call_params.arg_lengths[0] = sizeof(void*);
    call_params.arg_lengths[1] = sizeof(size_t);
    call_params.total_bytes = call_params.arg_lengths[0]+call_params.arg_lengths[1];

    call_params.args = new char[call_params.total_bytes];

    call_params.ret = new char[sizeof(int)];
    int * ret_spot = (int*)call_params.ret;
    *ret_spot = 0;

    int bytes_off = 0;
    int lengths_off = 0;

    pack(call_params.args, bytes_off, call_params.arg_lengths, lengths_off, (char *)&buff, call_params.arg_lengths[0]);
    pack(call_params.args, bytes_off, call_params.arg_lengths, lengths_off, (char *)&len, call_params.arg_lengths[1]);

    if(needToLockUponSend(type)){
       int lret = mlock(buff, len);
       if(lret != 0){
          m5_gpu(gem5_call_buffer_fail, &call_params);
       } else {
          m5_gpu(type, &call_params);
          munlock(buff, len);
       }
    } else {
       m5_gpu(type, &call_params);
    }

    int ret= *((int*)call_params.ret);

    DBG("doRWOperation received a return value of %x \n", ret);


    delete [] call_params.args;
    delete [] call_params.arg_lengths;
    delete [] call_params.ret;

    return ret;
}

int Gem5PipeStream::sendRWOperation(gem5GraphicsCall type, const void*buff, size_t len){
    //block till we are allowed to send
    while(doRWOperation(gem5_block, NULL,  0));
    bool sim_active = doRWOperation(gem5_sim_active, NULL, 0);
    //make sure graphics memory is locked before sending every call to gem5 when simulation is active

    if(sim_active){
       if(m_pggm==NULL)
          allocGBuffer();
       if(!Gem5PipeMemory::Memory.LockGBuffer(m_pggm)){
          doRWOperation(gem5_debug, (void*)gmem_lock_fail, 0);
          DBG("failed to pin graphics' memory");
       }
    }

    //now we can do the operation
    int ret = doRWOperation(type, buff, len);
    if(sim_active) Gem5PipeMemory::Memory.UnlockGBuffer();
    return ret;
}

int Gem5PipeStream::writeFully(const void *buf, size_t len)
{
    //replace with sending len bytes from buff
    //DBG(">> Gem5PipeStream::writeFully %d\n", len);
    DBG("calling writeFully with buf=%x  and len=%d \n", (unsigned int)buf,len);
    DBGSTACK();

    int ret = sendRWOperation(gem5_writeFully, buf,  len);

    //DBG("<< Gem5PipeStream::writeFully %d\n", len );
    return ret;
}

int Gem5PipeStream::commitBuffer(size_t size)
{
    DBG("calling commitBuffer with buf=%x  and len=%d \n", (unsigned int)m_buf,size);
    DBGSTACK();
    
    return writeFully(m_buf, size);
}

const unsigned char *Gem5PipeStream::readFully(void *buf, size_t len)
{
    //DBG(">> Gem5PipeStream::readFully %d\n", len);

    DBG("calling readFully with buf=%x  and len=%d \n", (unsigned int)buf,len);
    DBGSTACK();

    int ret = sendRWOperation(gem5_readFully, buf, len);
    //DBG("<< Gem5PipeStream::readFully %d\n", len);
    return (const unsigned char *) ret;
}

const unsigned char *Gem5PipeStream::read( void *buf, size_t *inout_len)
{
    //DBG(">> Gem5PipeStream::read %d\n", *inout_len);

    DBG("calling read with buf=%x  and len=%d \n",(unsigned int)buf,*inout_len);
    DBGSTACK();

    int n = sendRWOperation(gem5_read, buf, *inout_len);

    DBG("finished read with buf=%x  and len=%d \n",(unsigned int)buf,*inout_len);

    if (n > 0) {
        *inout_len = n;
        return (const unsigned char *)buf;
    }



    //DBG("<< Gem5PipeStream::read %d\n", *inout_len);
    return NULL;
}

int Gem5PipeStream::recv(void *buf, size_t len)
{
    DBG("calling recv with buf=%x  and len=%d \n", (unsigned int)buf,len);
    DBGSTACK();

    int ret = sendRWOperation(gem5_recv, buf, len);
    return ret;
}

bool Gem5PipeStream::needToLockUponSend(gem5GraphicsCall type){
   if((type == gem5_writeFully) 
      or (type == gem5_readFully) 
      or (type == gem5_read)
      or (type == gem5_recv)) 
      return true;
   return false;
}

void Gem5PipeStream::allocGBuffer(){
   DBG("allocating graphics memory");
   m_pggm_size = GEM5_GRAPHICS_MEM_SIZE;
   m_pggm = Gem5PipeMemory::Memory.AllocateNewGBuffer(m_pggm_size);
   if(m_pggm==NULL){
      DBG("failed to allocate memory for graphics");
      doRWOperation(gem5_debug, (void*)gmem_alloc_fail, 0);
   }
   doRWOperation(gem5_graphics_mem, m_pggm, m_pggm_size);
}
