/*
 * Copyright (C) 2006 Robert Reif
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "mmsystem.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#include <semaphore.h>
#endif

#include "wine/library.h"
#include "wine/debug.h"

#ifdef HAVE_JACK_JACK_H
#include <jack/jack.h>
#endif

#ifdef HAVE_ASIO_SDK_ASIO_H
#define IEEE754_64FLOAT 1
#include <common/asio.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(asio);

#if defined(HAVE_JACK_JACK_H) && defined(HAVE_ASIO_SDK_ASIO_H) && defined(HAVE_PTHREAD_H)

#ifndef SONAME_LIBJACK
#define SONAME_LIBJACK "libjack.so"
#endif

#define MAKE_FUNCPTR(f) static typeof(f) * fp_##f = NULL;

/* Function pointers for dynamic loading of libjack */
/* these are prefixed with "fp_", ie. "fp_jack_client_new" */
MAKE_FUNCPTR(jack_activate);
MAKE_FUNCPTR(jack_deactivate);
MAKE_FUNCPTR(jack_connect);
MAKE_FUNCPTR(jack_client_open);
MAKE_FUNCPTR(jack_client_close);
MAKE_FUNCPTR(jack_transport_query);
MAKE_FUNCPTR(jack_transport_start);
MAKE_FUNCPTR(jack_set_process_callback);
MAKE_FUNCPTR(jack_get_sample_rate);
MAKE_FUNCPTR(jack_port_register);
MAKE_FUNCPTR(jack_port_get_buffer);
MAKE_FUNCPTR(jack_get_ports);
MAKE_FUNCPTR(jack_port_name);
MAKE_FUNCPTR(jack_get_buffer_size);
#undef MAKE_FUNCPTR

void *jackhandle = NULL;

/* JACK callback function */
static int jack_process(jack_nframes_t nframes, void * arg);

/* WIN32 callback function */
static DWORD CALLBACK win32_callback(LPVOID arg);

/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static GUID const CLSID_WineASIO = {
0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };

#define twoRaisedTo32           4294967296.0
#define twoRaisedTo32Reciprocal	(1.0 / twoRaisedTo32)

#define MAX_INPUTS      2
#define MAX_OUTPUTS     2

/* ASIO drivers use the thiscall calling convention which only Microsoft compilers
 * produce.  These macros add an extra layer to fixup the registers properly for
 * this calling convention.
 */

#ifdef __i386__  /* thiscall functions are i386-specific */

#ifdef __GNUC__
/* GCC erroneously warns that the newly wrapped function
 * isn't used, lets help it out of it's thinking
 */
#define SUPPRESS_NOTUSED __attribute__((used))
#else
#define SUPPRESS_NOTUSED
#endif /* __GNUC__ */

#define WRAP_THISCALL(type, func, parm) \
    extern type func parm; \
    __ASM_GLOBAL_FUNC( func, \
                      "popl %eax\n\t" \
                      "pushl %ecx\n\t" \
                      "pushl %eax\n\t" \
                      "jmp " __ASM_NAME("__wrapped_" #func) ); \
    SUPPRESS_NOTUSED static type __wrapped_ ## func parm
#else
#define WRAP_THISCALL(functype, function, param) \
    functype function param
#endif

/*****************************************************************************
 * IWineAsio interface
 */
#define INTERFACE IWineASIO
DECLARE_INTERFACE_(IWineASIO,IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    STDMETHOD_(ASIOBool,init)(THIS_ void *sysHandle) PURE;
    STDMETHOD_(void,getDriverName)(THIS_ char *name) PURE;
    STDMETHOD_(long,getDriverVersion)(THIS) PURE;
    STDMETHOD_(void,getErrorMessage)(THIS_ char *string) PURE;
    STDMETHOD_(ASIOError,start)(THIS) PURE;
    STDMETHOD_(ASIOError,stop)(THIS) PURE;
    STDMETHOD_(ASIOError,getChannels)(THIS_ long *numInputChannels, long *numOutputChannels) PURE;
    STDMETHOD_(ASIOError,getLatencies)(THIS_ long *inputLatency, long *outputLatency) PURE;
    STDMETHOD_(ASIOError,getBufferSize)(THIS_ long *minSize, long *maxSize, long *preferredSize, long *granularity) PURE;
    STDMETHOD_(ASIOError,canSampleRate)(THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError,getSampleRate)(THIS_ ASIOSampleRate *sampleRate) PURE;
    STDMETHOD_(ASIOError,setSampleRate)(THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError,getClockSources)(THIS_ ASIOClockSource *clocks, long *numSources) PURE;
    STDMETHOD_(ASIOError,setClockSource)(THIS_ long reference) PURE;
    STDMETHOD_(ASIOError,getSamplePosition)(THIS_ ASIOSamples *sPos, ASIOTimeStamp *tStamp) PURE;
    STDMETHOD_(ASIOError,getChannelInfo)(THIS_ ASIOChannelInfo *info) PURE;
    STDMETHOD_(ASIOError,createBuffers)(THIS_ ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks) PURE;
    STDMETHOD_(ASIOError,disposeBuffers)(THIS) PURE;
    STDMETHOD_(ASIOError,controlPanel)(THIS) PURE;
    STDMETHOD_(ASIOError,future)(THIS_ long selector,void *opt) PURE;
    STDMETHOD_(ASIOError,outputReady)(THIS) PURE;
};
#undef INTERFACE

typedef struct IWineASIO *LPWINEASIO, **LPLPWINEASIO;

enum
{
    Init,
    Run,
    Exit
};

struct IWineASIOImpl
{
    /* COM stuff */
    const IWineASIOVtbl *lpVtbl;
    LONG                ref;

    /* ASIO stuff */
    HWND                hwnd;
    ASIOSampleRate      sample_rate;
    long                input_latency;
    long                output_latency;
    long                block_frames;
    ASIOTime            asio_time;
    long                miliseconds;
    ASIOTimeStamp       system_time;
    double              sample_position;
    ASIOBufferInfo      *bufferInfos;
    ASIOCallbacks       *callbacks;
    char                error_message[256];
    long                in_map[MAX_INPUTS];
    long                out_map[MAX_OUTPUTS];
    long                num_inputs;
    long                num_outputs;
    short               *input_buffers[MAX_INPUTS][2];
    short               *output_buffers[MAX_OUTPUTS][2];
    long                active_inputs;
    long                active_outputs;
    BOOL                time_info_mode;
    BOOL                tc_read;
    long                state;

    /* JACK stuff */
    jack_port_t         *input_port[MAX_INPUTS];
    jack_port_t         *output_port[MAX_OUTPUTS];
    jack_client_t       *client;
    long                client_state;
    long                toggle;

    /* callback stuff */
    HANDLE              thread;
    HANDLE              start_event;
    HANDLE              stop_event;
    DWORD               thread_id;
    sem_t               semaphore1;
    sem_t               semaphore2;
    BOOL                terminate;
};

typedef struct IWineASIOImpl              IWineASIOImpl;

static ULONG WINAPI IWineASIOImpl_AddRef(LPWINEASIO iface)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    ULONG ref = InterlockedIncrement(&(This->ref));
    TRACE("(%p) ref was %ld\n", This, ref - 1);
    return ref;
}

static ULONG WINAPI IWineASIOImpl_Release(LPWINEASIO iface)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    ULONG ref = InterlockedDecrement(&(This->ref));
    TRACE("(%p) ref was %ld\n", This, ref + 1);

    if (!ref) {
        fp_jack_client_close(This->client);
        TRACE("JACK client closed\n");

        wine_dlclose(jackhandle, NULL, 0);
        jackhandle = NULL;

        This->terminate = TRUE;
        sem_post(&This->semaphore1);

        WaitForSingleObject(This->stop_event, INFINITE);

        sem_destroy(&This->semaphore1);
        sem_destroy(&This->semaphore2);

        HeapFree(GetProcessHeap(),0,This);
        TRACE("(%p) released\n", This);
    }
    return ref;
}

static HRESULT WINAPI IWineASIOImpl_QueryInterface(LPWINEASIO iface, REFIID riid, void** ppvObject)
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(riid), ppvObject);

    if (ppvObject == NULL)
        return E_INVALIDARG;

    if (IsEqualIID(&CLSID_WineASIO, riid))
    {
        IWineASIOImpl_AddRef(iface);

        *ppvObject = This;

        return S_OK;
    }

    return E_NOINTERFACE;
}

WRAP_THISCALL( ASIOBool __stdcall, IWineASIOImpl_init, (LPWINEASIO iface, void *sysHandle))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    jack_status_t status;
    int i;
    TRACE("(%p, %p)\n", iface, sysHandle);

    This->sample_rate = 48000.0;
    This->block_frames = 1024;
    This->input_latency = This->block_frames;
    This->output_latency = This->block_frames * 2;
    This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);
    This->callbacks = NULL;
    This->sample_position = 0;
    strcpy(This->error_message, "No Error");
    This->num_inputs = 0;
    This->num_outputs = 0;
    This->active_inputs = 0;
    This->active_outputs = 0;
    This->toggle = 0;
    This->client_state = Init;
    This->time_info_mode = FALSE;
    This->tc_read = FALSE;
    This->terminate = FALSE;
    This->state = Init;

    sem_init(&This->semaphore1, 0, 0);
    sem_init(&This->semaphore2, 0, 0);

    This->start_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    This->stop_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    This->thread = CreateThread(NULL, 0, win32_callback, (LPVOID)This, 0, &This->thread_id);
    if (This->thread)
    {
        WaitForSingleObject(This->start_event, INFINITE);
        CloseHandle(This->start_event);
        This->start_event = INVALID_HANDLE_VALUE;
    }
    else
    {
        WARN("Couldn't create thread\n");
        return ASIOFalse;
    }

    for (i = 0; i < MAX_INPUTS; i++)
    {
        This->in_map[i] = 0;
    }

    for (i = 0; i < MAX_OUTPUTS; i++)
    {
        This->out_map[i] = 0;
    }

    This->client = fp_jack_client_open("Wine_ASIO_Jack_Client", JackNullOption, &status, NULL);
    if (This->client == NULL)
    {
        WARN("failed to open jack server\n");
        return ASIOFalse;
    }

    TRACE("JACK client opened\n");

    if (status & JackServerStarted)
        TRACE("JACK server started\n");

    fp_jack_set_process_callback(This->client, jack_process, This);

    This->sample_rate = fp_jack_get_sample_rate(This->client);
    This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);

    TRACE("sample rate: %f\n", This->sample_rate);

    for (i = 0; i < MAX_INPUTS; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "Input%d", i);
        This->input_port[i] = fp_jack_port_register(This->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, i);
        if (This->input_port[i] == 0)
            break;

        This->num_inputs++;
    }

    TRACE("found %ld inputs\n", This->num_inputs);

    for (i = 0; i < MAX_OUTPUTS; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "Output%d", i);
        This->output_port[i] = fp_jack_port_register(This->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, i);
        if (This->output_port[i] == 0)
            break;

        This->num_outputs++;
    }

    TRACE("found %ld outputs\n", This->num_outputs);

    return ASIOTrue;
}

WRAP_THISCALL( void __stdcall, IWineASIOImpl_getDriverName, (LPWINEASIO iface, char *name))
{
    TRACE("(%p, %p)\n", iface, name);
    strcpy(name, "Wine ASIO");
}

WRAP_THISCALL( long __stdcall, IWineASIOImpl_getDriverVersion, (LPWINEASIO iface))
{
    TRACE("(%p)\n", iface);
    return 1;
}

WRAP_THISCALL( void __stdcall, IWineASIOImpl_getErrorMessage, (LPWINEASIO iface, char *string))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p)\n", iface, string);
    strcpy(string, This->error_message);
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_start, (LPWINEASIO iface))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    const char ** ports;
    int i;
    TRACE("(%p)\n", iface);

    if (This->callbacks)
    {
        This->sample_position = 0;
        This->system_time.lo = 0;
        This->system_time.hi = 0;

        if (fp_jack_activate(This->client))
        {
            WARN("couldn't activate client\n");
            return ASE_NotPresent;
        }

        ports = fp_jack_get_ports(This->client, NULL, NULL, JackPortIsPhysical | JackPortIsOutput);

        if (ports == NULL)
        {
            WARN("couldn't get input ports\n");
            return ASE_NotPresent;
        }

        for (i = 0; i < This->active_inputs; i++)
        {
            if (fp_jack_connect(This->client, ports[i], fp_jack_port_name(This->input_port[i])))
            {
                WARN("input %d connect failed\n", i);
                free(ports);
                return ASE_NotPresent;
            }
        }

        free(ports);

        ports = fp_jack_get_ports(This->client, NULL, NULL, JackPortIsPhysical | JackPortIsInput);

        if (ports == NULL)
        {
            WARN("couldn't get output ports\n");
            return ASE_NotPresent;
        }

        for (i = 0; i < This->active_outputs; i++)
        {
            if (fp_jack_connect(This->client, fp_jack_port_name(This->output_port[i]), ports[i]))
            {
                WARN("output %d connect failed\n", i);
                free(ports);
                return ASE_NotPresent;
            }
        }

        free(ports);

        This->state = Run;
        TRACE("started\n");

        return ASE_OK;
    }

    return ASE_NotPresent;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_stop, (LPWINEASIO iface))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p)\n", iface);

    This->state = Exit;

    if (fp_jack_deactivate(This->client))
    {
        WARN("couldn't deactivate client\n");
        return ASE_NotPresent;
    }

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getChannels, (LPWINEASIO iface, long *numInputChannels, long *numOutputChannels))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p, %p)\n", iface, numInputChannels, numOutputChannels);

    if (numInputChannels)
        *numInputChannels = This->num_inputs;

    if (numOutputChannels)
        *numOutputChannels = This->num_outputs;

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getLatencies, (LPWINEASIO iface, long *inputLatency, long *outputLatency))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p, %p)\n", iface, inputLatency, outputLatency);

    if (inputLatency)
        *inputLatency = This->input_latency;

    if (outputLatency)
        *outputLatency = This->output_latency;

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getBufferSize, (LPWINEASIO iface, long *minSize, long *maxSize, long *preferredSize, long *granularity))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p, %p, %p, %p)\n", iface, minSize, maxSize, preferredSize, granularity);

    if (minSize)
        *minSize = This->block_frames;

    if (maxSize)
        *maxSize = This->block_frames;

    if (preferredSize)
        *preferredSize = This->block_frames;

    if (granularity)
        *granularity = 0;

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_canSampleRate, (LPWINEASIO iface, ASIOSampleRate sampleRate))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %f)\n", iface, sampleRate);

    if (sampleRate == This->sample_rate)
        return ASE_OK;

    return ASE_NoClock;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getSampleRate, (LPWINEASIO iface, ASIOSampleRate *sampleRate))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p)\n", iface, sampleRate);

    if (sampleRate)
        *sampleRate = This->sample_rate;

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_setSampleRate, (LPWINEASIO iface, ASIOSampleRate sampleRate))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %f)\n", iface, sampleRate);

    if (sampleRate != This->sample_rate)
        return ASE_NoClock;

    if (sampleRate != This->sample_rate)
    {
        This->sample_rate = sampleRate;
        This->asio_time.timeInfo.sampleRate = sampleRate;
        This->asio_time.timeInfo.flags |= kSampleRateChanged;
        This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);

        if (This->callbacks && This->callbacks->sampleRateDidChange)
            This->callbacks->sampleRateDidChange(This->sample_rate);
    }

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getClockSources, (LPWINEASIO iface, ASIOClockSource *clocks, long *numSources))
{
    TRACE("(%p, %p, %p)\n", iface, clocks, numSources);

    if (clocks && numSources)
    {
        clocks->index = 0;
        clocks->associatedChannel = -1;
        clocks->associatedGroup = -1;
        clocks->isCurrentSource = ASIOTrue;
        strcpy(clocks->name, "Internal");

        *numSources = 1;
        return ASE_OK;
    }

    return ASE_InvalidParameter;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_setClockSource, (LPWINEASIO iface, long reference))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %ld)\n", iface, reference);

    if (reference == 0)
    {
        This->asio_time.timeInfo.flags |= kClockSourceChanged;

        return ASE_OK;
    }

    return ASE_NotPresent;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getSamplePosition, (LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %p, %p)\n", iface, sPos, tStamp);

    tStamp->lo = This->system_time.lo;
    tStamp->hi = This->system_time.hi;

    if (This->sample_position >= twoRaisedTo32)
    {
        sPos->hi = (unsigned long)(This->sample_position * twoRaisedTo32Reciprocal);
        sPos->lo = (unsigned long)(This->sample_position - (sPos->hi * twoRaisedTo32));
    }
    else
    {
        sPos->hi = 0;
        sPos->lo = (unsigned long)This->sample_position;
    }

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_getChannelInfo, (LPWINEASIO iface, ASIOChannelInfo *info))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    int i;
    char name[32];
    TRACE("(%p, %p)\n", iface, info);

    if (info->channel < 0 || (info->isInput ? info->channel >= MAX_INPUTS : info->channel >= MAX_OUTPUTS))
        return ASE_InvalidParameter;

    info->type = ASIOSTInt16LSB;
    info->channelGroup = 0;
    info->isActive = ASIOFalse;

    if (info->isInput)
    {
        for (i = 0; i < This->active_inputs; i++)
        {
            if (This->in_map[i] == info->channel)
            {
                info->isActive = ASIOTrue;
                break;
            }
        }

        snprintf(name, sizeof(name), "Input %ld", info->channel);
    }
    else
    {
        for (i = 0; i < This->active_outputs; i++)
        {
            if (This->out_map[i] == info->channel)
            {
                info->isActive = ASIOTrue;
                break;
            }
        }

        snprintf(name, sizeof(name), "Output %ld", info->channel);
    }

    strcpy(info->name, name);

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_disposeBuffers, (LPWINEASIO iface))
{
    int i;
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p)\n", iface);

    This->callbacks = NULL;
    __wrapped_IWineASIOImpl_stop(iface);

    for (i = 0; i < This->active_inputs; i++)
    {
        HeapFree(GetProcessHeap(), 0, This->input_buffers[i][0]);
        HeapFree(GetProcessHeap(), 0, This->input_buffers[i][1]);
    }

    This->active_inputs = 0;

    for (i = 0; i < This->active_outputs; i++)
    {
        HeapFree(GetProcessHeap(), 0, This->output_buffers[i][0]);
        HeapFree(GetProcessHeap(), 0, This->output_buffers[i][1]);
    }

    This->active_outputs = 0;

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_createBuffers, (LPWINEASIO iface, ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    ASIOBufferInfo * info = bufferInfos;
    int i;
    TRACE("(%p, %p, %ld, %ld, %p)\n", iface, bufferInfos, numChannels, bufferSize, callbacks);

    This->active_inputs = 0;
    This->active_outputs = 0;
    This->block_frames = bufferSize;
    This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);

    for (i = 0; i < numChannels; i++, info++)
    {
        if (info->isInput)
        {
            if (info->channelNum < 0 || info->channelNum >= This->num_inputs)
            {
                WARN("invalid input channel: %ld\n", info->channelNum);
                goto ERROR_PARAM;
            }

            if (This->active_inputs >= This->num_inputs)
            {
                WARN("too many inputs\n");
                goto ERROR_PARAM;
            }

            This->in_map[This->active_inputs] = info->channelNum;
            This->input_buffers[This->active_inputs][0] = HeapAlloc(GetProcessHeap(), 0, This->block_frames * sizeof(short));
            This->input_buffers[This->active_inputs][1] = HeapAlloc(GetProcessHeap(), 0, This->block_frames * sizeof(short));
            if (This->input_buffers[This->active_inputs][0] && This->input_buffers[This->active_inputs][1])
            {
                info->buffers[0] = This->input_buffers[This->active_inputs][0];
                info->buffers[1] = This->input_buffers[This->active_inputs][1];
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, This->input_buffers[This->active_inputs][0]);
                info->buffers[0] = 0;
                info->buffers[1] = 0;
                WARN("no input buffer memory\n");
                goto ERROR_MEM;
            }
            This->active_inputs++;
        }
        else
        {
            if (info->channelNum < 0 || info->channelNum >= This->num_outputs)
            {
                WARN("invalid output channel: %ld\n", info->channelNum);
                goto ERROR_PARAM;
            }

            if (This->active_outputs >= This->num_outputs)
            {
                WARN("too many outputs\n");
                goto ERROR_PARAM;
            }

            This->out_map[This->active_outputs] = info->channelNum;
            This->output_buffers[This->active_outputs][0] = HeapAlloc(GetProcessHeap(), 0, This->block_frames * sizeof(short));
            This->output_buffers[This->active_outputs][1] = HeapAlloc(GetProcessHeap(), 0, This->block_frames * sizeof(short));
            if (This->output_buffers[This->active_outputs][0] && This->output_buffers[This->active_outputs][1])
            {
                info->buffers[0] = This->output_buffers[This->active_outputs][0];
                info->buffers[1] = This->output_buffers[This->active_outputs][1];
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, This->output_buffers[This->active_outputs][0]);
                info->buffers[0] = 0;
                info->buffers[1] = 0;
                WARN("no output buffer memory\n");
                goto ERROR_MEM;
            }
            This->active_outputs++;
        }
    }

    This->callbacks = callbacks;

    if (This->callbacks->asioMessage)
    {
        if (This->callbacks->asioMessage(kAsioSupportsTimeInfo, 0, 0, 0))
        {
            This->time_info_mode = TRUE;
            This->asio_time.timeInfo.speed = 1;
            This->asio_time.timeInfo.systemTime.hi = 0;
            This->asio_time.timeInfo.systemTime.lo = 0;
            This->asio_time.timeInfo.samplePosition.hi = 0;
            This->asio_time.timeInfo.samplePosition.lo = 0;
            This->asio_time.timeInfo.sampleRate = This->sample_rate;
            This->asio_time.timeInfo. flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;

            This->asio_time.timeCode.speed = 1;
            This->asio_time.timeCode.timeCodeSamples.hi = 0;
            This->asio_time.timeCode.timeCodeSamples.lo = 0;
            This->asio_time.timeCode.flags = kTcValid | kTcRunning;
        }
        else
            This->time_info_mode = FALSE;
    }
    else
    {
        This->time_info_mode = FALSE;
        WARN("asioMessage callback not supplied\n");
        goto ERROR_PARAM;
    }

    return ASE_OK;

ERROR_MEM:
    __wrapped_IWineASIOImpl_disposeBuffers(iface);
    WARN("no memory\n");
    return ASE_NoMemory;

ERROR_PARAM:
    __wrapped_IWineASIOImpl_disposeBuffers(iface);
    WARN("invalid parameter\n");
    return ASE_InvalidParameter;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_controlPanel, (LPWINEASIO iface))
{
    TRACE("(%p) stub!\n", iface);

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_future, (LPWINEASIO iface, long selector, void *opt))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %ld, %p)\n", iface, selector, opt);

    switch (selector)
    {
    case kAsioEnableTimeCodeRead:
        This->tc_read = TRUE;
        return ASE_SUCCESS;
    case kAsioDisableTimeCodeRead:
        This->tc_read = FALSE;
        return ASE_SUCCESS;
    case kAsioSetInputMonitor:
        return ASE_SUCCESS;
    case kAsioCanInputMonitor:
        return ASE_SUCCESS;
    case kAsioCanTimeInfo:
        return ASE_SUCCESS;
    case kAsioCanTimeCode:
        return ASE_SUCCESS;
    }

    return ASE_NotPresent;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_outputReady, (LPWINEASIO iface))
{
    TRACE("(%p)\n", iface);

    return ASE_NotPresent;
}

static const IWineASIOVtbl WineASIO_Vtbl =
{
    IWineASIOImpl_QueryInterface,
    IWineASIOImpl_AddRef,
    IWineASIOImpl_Release,
    IWineASIOImpl_init,
    IWineASIOImpl_getDriverName,
    IWineASIOImpl_getDriverVersion,
    IWineASIOImpl_getErrorMessage,
    IWineASIOImpl_start,
    IWineASIOImpl_stop,
    IWineASIOImpl_getChannels,
    IWineASIOImpl_getLatencies,
    IWineASIOImpl_getBufferSize,
    IWineASIOImpl_canSampleRate,
    IWineASIOImpl_getSampleRate,
    IWineASIOImpl_setSampleRate,
    IWineASIOImpl_getClockSources,
    IWineASIOImpl_setClockSource,
    IWineASIOImpl_getSamplePosition,
    IWineASIOImpl_getChannelInfo,
    IWineASIOImpl_createBuffers,
    IWineASIOImpl_disposeBuffers,
    IWineASIOImpl_controlPanel,
    IWineASIOImpl_future,
    IWineASIOImpl_outputReady,
};

BOOL init_jack()
{
    jackhandle = wine_dlopen(SONAME_LIBJACK, RTLD_NOW, NULL, 0);
    TRACE("SONAME_LIBJACK == %s\n", SONAME_LIBJACK);
    TRACE("jackhandle == %p\n", jackhandle);
    if (!jackhandle)
    {
        FIXME("error loading the jack library %s, please install this library to use jack\n", SONAME_LIBJACK);
        jackhandle = (void*)-1;
        return FALSE;
    }

    /* setup function pointers */
#define LOAD_FUNCPTR(f) if((fp_##f = wine_dlsym(jackhandle, #f, NULL, 0)) == NULL) goto sym_not_found;
    LOAD_FUNCPTR(jack_activate);
    LOAD_FUNCPTR(jack_deactivate);
    LOAD_FUNCPTR(jack_connect);
    LOAD_FUNCPTR(jack_client_open);
    LOAD_FUNCPTR(jack_client_close);
    LOAD_FUNCPTR(jack_transport_query);
    LOAD_FUNCPTR(jack_transport_start);
    LOAD_FUNCPTR(jack_set_process_callback);
    LOAD_FUNCPTR(jack_get_sample_rate);
    LOAD_FUNCPTR(jack_port_register);
    LOAD_FUNCPTR(jack_port_get_buffer);
    LOAD_FUNCPTR(jack_get_ports);
    LOAD_FUNCPTR(jack_port_name);
    LOAD_FUNCPTR(jack_get_buffer_size);
#undef LOAD_FUNCPTR

    return TRUE;

    /* error path for function pointer loading errors */
sym_not_found:
    WINE_MESSAGE("Wine cannot find certain functions that it needs inside the jack"
                 "library.  To enable Wine to use the jack audio server please "
                 "install libjack\n");
    wine_dlclose(jackhandle, NULL, 0);
    jackhandle = NULL;
    return FALSE;
}

HRESULT asioCreateInstance(REFIID riid, LPVOID *ppobj)
{
    IWineASIOImpl * pobj;
    TRACE("(%s, %p)\n", debugstr_guid(riid), ppobj);

    if (!init_jack())
        return ERROR_NOT_SUPPORTED;
 
    pobj = HeapAlloc(GetProcessHeap(), 0, sizeof(*pobj));
    if (pobj == NULL) {
        WARN("out of memory\n");
        return E_OUTOFMEMORY;
    }

    pobj->lpVtbl = &WineASIO_Vtbl;
    pobj->ref = 1;
    TRACE("pobj = %p\n", pobj);
    *ppobj = pobj;
    TRACE("return %p\n", *ppobj);
    return S_OK;
}

static void getNanoSeconds(ASIOTimeStamp* ts)
{
    double nanoSeconds = (double)((unsigned long)timeGetTime ()) * 1000000.;
    ts->hi = (unsigned long)(nanoSeconds / twoRaisedTo32);
    ts->lo = (unsigned long)(nanoSeconds - (ts->hi * twoRaisedTo32));
}

static int jack_process(jack_nframes_t nframes, void * arg)
{
    IWineASIOImpl * This = (IWineASIOImpl*)arg;
    int i, j;
    jack_default_audio_sample_t * in, *out;
    jack_transport_state_t ts;

    ts = fp_jack_transport_query(This->client, NULL);

    if (ts == JackTransportRolling)
    {
        if (This->client_state == Init)
            This->client_state = Run;

        This->toggle = This->toggle ? 0 : 1;
        This->sample_position += nframes;

        /* get the input data from JACK and copy it to the ASIO buffers */
        for (i = 0; i < This->active_inputs; i++)
        {
            short * buffer = This->input_buffers[i][This->toggle];

            in = fp_jack_port_get_buffer(This->input_port[i], nframes);

            for (j = 0; j < nframes; j++)
                buffer[j] = in[j] * 32767.0f;
        }

        /* call the ASIO user callback to read the input data and fill the output data */
        getNanoSeconds(&This->system_time);

        /* wake up the WIN32 thread so it can do the do it's callback */
        sem_post(&This->semaphore1);

        /* wait for the WIN32 thread to complete before continuing */
        sem_wait(&This->semaphore2);

        /* copy the ASIO data to JACK */
        for (i = 0; i < This->active_outputs; i++)
        {
            short * buffer = This->output_buffers[i][This->toggle];

            out = fp_jack_port_get_buffer(This->output_port[i], nframes);

            for (j = 0; j < nframes; j++)
                out[j] = buffer[j] / 32767.0f;
        }
    }
    else if (ts == JackTransportStopped)
    {
        if (This->client_state == Run)
            This->client_state = Exit;
        else
        {
            /* FIXME why is this needed ? */
            fp_jack_transport_start(This->client);
        }
    }

    return 0;
}

/*
 * The ASIO callback can make WIN32 calls which require a WIN32 thread.
 * Do the callback in this thread and then switch back to the Jack callback thread.
 */
static DWORD CALLBACK win32_callback(LPVOID arg)
{
    IWineASIOImpl * This = (IWineASIOImpl*)arg;
    TRACE("(%p)\n", arg);
    
    /* let IWineASIO_Init know we are alive */
    SetEvent(This->start_event);

    while (1)
    {
        /* wait to be woken up by the JAck callback thread */
        sem_wait(&This->semaphore1);

        /* check for termination */
        if (This->terminate)
        {
            SetEvent(This->stop_event);
            TRACE("terminated\n");
            return 0;
        }

        /* make sure we are in the run state */
        if (This->state == Run)
        {
            if (This->time_info_mode)
            {
                __wrapped_IWineASIOImpl_getSamplePosition((LPWINEASIO)This,
                    &This->asio_time.timeInfo.samplePosition, &This->asio_time.timeInfo.systemTime);
                if (This->tc_read)
                {
                    /* FIXME */
                    This->asio_time.timeCode.timeCodeSamples.lo = This->asio_time.timeInfo.samplePosition.lo;
                    This->asio_time.timeCode.timeCodeSamples.hi = 0;
                }
                This->callbacks->bufferSwitchTimeInfo(&This->asio_time, This->toggle, ASIOFalse);
                This->asio_time.timeInfo.flags &= ~(kSampleRateChanged | kClockSourceChanged);
            }
            else
                This->callbacks->bufferSwitch(This->toggle, ASIOFalse);
        }

        /* let the Jack thread know we are done */
        sem_post(&This->semaphore2);
    }

    return 0;
}

#else

HRESULT asioCreateInstance(REFIID riid, LPVOID *ppobj)
{
    WARN("(%s, %p) ASIO support not compiled into wine\n", debugstr_guid(riid), ppobj);
    return ERROR_NOT_SUPPORTED;
}

#endif
