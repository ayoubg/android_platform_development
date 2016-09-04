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
#include "HostConnection.h"
#include "TcpStream.h"
#include "Gem5PipeStream.h"
#include "ThreadInfo.h"
#include <cutils/log.h>
#include "GLEncoder.h"
#include "GL2Encoder.h"

#define STREAM_BUFFER_SIZE  4*1024*1024
#define STREAM_PORT_NUM     22468

/* Set to 1 to use a Gem5 pipe, or 0 for a TCP connection */
#define  USE_GEM5_PIPE  1
#define  LOG_TAG  "gem5-tag"

HostConnection::HostConnection() :
    m_stream(NULL),
    m_glEnc(NULL),
    m_gl2Enc(NULL),
    m_rcEnc(NULL)
{
}

HostConnection::~HostConnection()
{
    delete m_stream;
    delete m_glEnc;
    delete m_gl2Enc;
    delete m_rcEnc;
}

HostConnection *HostConnection::get()
{
    DBG("HostConnection::get () is called from pid=%d, tid=%d",getpid(),gettid());
    /* TODO: Make this configurable with a system property */
    const int useGem5Pipe = USE_GEM5_PIPE;

    DBG("useGem5Pipe value is %d \n",useGem5Pipe);
    // Get thread info
    EGLThreadInfo *tinfo = getEGLThreadInfo();
    if (!tinfo) {
        ALOGD("Failing to create ThreadInfo object \n");
        return NULL;
    }

    DBG("Now checking if tinfo->hostConn is null \n");
    if (tinfo->hostConn == NULL) {
        DBG("Creating a HostConnection \n");
        HostConnection *con = new HostConnection();
        if (NULL == con) {
            ALOGD("Failed to create a HostConnection object \n");
            return NULL;
        }

        DBG("Checking if I should use the gem5 pipe \n");
        if (useGem5Pipe) {
            DBG("Opening a gem5 pipe \n");
            Gem5PipeStream *stream = new Gem5PipeStream(STREAM_BUFFER_SIZE);
            if (!stream) {
                ALOGE("Failed to create Gem5 for host connection!!!\n");
                delete con;
                return NULL;
            }
            con->m_stream = stream;
        }
        else /* !useGem5Pipe */
        {
            DBG("I shouldn't be here in TcpStream \n");
            return NULL;

            TcpStream *stream = new TcpStream(STREAM_BUFFER_SIZE);
            if (!stream) {
                ALOGE("Failed to create TcpStream for host connection!!!\n");
                delete con;
                return NULL;
            }

            if (stream->connect("10.0.2.2", STREAM_PORT_NUM) < 0) {
                ALOGE("Failed to connect to host (TcpStream)!!!\n");
                delete stream;
                delete con;
                return NULL;
            }
            con->m_stream = stream;
        }

        // send zero 'clientFlags' to the host.
        unsigned int *pClientFlags =
                (unsigned int *)con->m_stream->allocBuffer(sizeof(unsigned int));
        *pClientFlags = 0;
        con->m_stream->commitBuffer(sizeof(unsigned int));

        ALOGI("HostConnection::get() New Host Connection established %p, tid %d\n", con, gettid());
        tinfo->hostConn = con;
    }

    DBG("HostConnection::get() is returning");
    return tinfo->hostConn;
}

GLEncoder *HostConnection::glEncoder()
{
    if (!m_glEnc) {
        m_glEnc = new GLEncoder(m_stream);
        DBG("HostConnection::glEncoder new encoder %p, tid %d", m_glEnc, gettid());
        m_glEnc->setContextAccessor(s_getGLContext);
    }
    return m_glEnc;
}

GL2Encoder *HostConnection::gl2Encoder()
{
    if (!m_gl2Enc) {
        m_gl2Enc = new GL2Encoder(m_stream);
        DBG("HostConnection::gl2Encoder new encoder %p, tid %d", m_gl2Enc, gettid());
        m_gl2Enc->setContextAccessor(s_getGL2Context);
    }
    return m_gl2Enc;
}

renderControl_encoder_context_t *HostConnection::rcEncoder()
{
    if (!m_rcEnc) {
        m_rcEnc = new renderControl_encoder_context_t(m_stream);
    }
    return m_rcEnc;
}

gl_client_context_t *HostConnection::s_getGLContext()
{
    EGLThreadInfo *ti = getEGLThreadInfo();
    if (ti->hostConn) {
        return ti->hostConn->m_glEnc;
    }
    return NULL;
}

gl2_client_context_t *HostConnection::s_getGL2Context()
{
    EGLThreadInfo *ti = getEGLThreadInfo();
    if (ti->hostConn) {
        return ti->hostConn->m_gl2Enc;
    }
    return NULL;
}
