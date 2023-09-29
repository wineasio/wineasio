/*
 * Copyright (C) 2006 Robert Reif
 * Portions copyright (C) 2007 Ralf Beck
 * Portions copyright (C) 2007 Johnny Petrantoni
 * Portions copyright (C) 2007 Stephane Letz
 * Portions copyright (C) 2008 William Steidtmann
 * Portions copyright (C) 2010 Peter L Jones
 * Portions copyright (C) 2010 Torben Hohn
 * Portions copyright (C) 2010 Nedko Arnaudov
 * Portions copyright (C) 2013 Joakim Hernberg
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

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <jack/jack.h>
#include <jack/thread.h>

#ifdef DEBUG
#include "wine/debug.h"
#else
#define TRACE(...) {}
#define WARN(fmt, ...) {} fprintf(stdout, fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) {} fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

#include "objbase.h"
#include "mmsystem.h"
#include "winreg.h"
#ifdef WINE_WITH_UNICODE
#include "wine/unicode.h"
#endif

#define IEEE754_64FLOAT 1
#undef NATIVE_INT64
#include "asio.h"
#define NATIVE_INT64

#ifdef DEBUG
WINE_DEFAULT_DEBUG_CHANNEL(asio);
#endif

#define MAX_ENVIRONMENT_SIZE        6
#define ASIO_MAX_NAME_LENGTH        32
#define ASIO_MINIMUM_BUFFERSIZE     16
#define ASIO_MAXIMUM_BUFFERSIZE     8192
#define ASIO_PREFERRED_BUFFERSIZE   1024

/* ASIO drivers (breaking the COM specification) use the Microsoft variety of
 * thiscall calling convention which gcc is unable to produce.  These macros
 * add an extra layer to fixup the registers. Borrowed from config.h and the
 * wine source code.
 */

/* From config.h */
#define __ASM_DEFINE_FUNC(name,suffix,code) asm(".text\n\t.align 4\n\t.globl " #name suffix "\n\t.type " #name suffix ",@function\n" #name suffix ":\n\t.cfi_startproc\n\t" code "\n\t.cfi_endproc\n\t.previous");
#define __ASM_GLOBAL_FUNC(name,code) __ASM_DEFINE_FUNC(name,"",code)
#define __ASM_NAME(name) name
#define __ASM_STDCALL(args) ""

/* From wine source */
#ifdef __i386__  /* thiscall functions are i386-specific */

#define THISCALL(func) __thiscall_ ## func
#define THISCALL_NAME(func) __ASM_NAME("__thiscall_" #func)
#define __thiscall __stdcall
#define DEFINE_THISCALL_WRAPPER(func,args) \
    extern void THISCALL(func)(void); \
    __ASM_GLOBAL_FUNC(__thiscall_ ## func, \
                      "popl %eax\n\t" \
                      "pushl %ecx\n\t" \
                      "pushl %eax\n\t" \
                      "jmp " __ASM_NAME(#func) __ASM_STDCALL(args) )
#else /* __i386__ */

#define THISCALL(func) func
#define THISCALL_NAME(func) __ASM_NAME(#func)
#define __thiscall __stdcall
#define DEFINE_THISCALL_WRAPPER(func,args) /* nothing */

#endif /* __i386__ */

/* Hide ELF symbols for the COM members - No need to to export them */
#define HIDDEN __attribute__ ((visibility("hidden")))

/*****************************************************************************
 * IWineAsio interface
 */

#define INTERFACE IWineASIO
DECLARE_INTERFACE_(IWineASIO,IUnknown)
{
    STDMETHOD_(HRESULT, QueryInterface)         (THIS_ IID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)                   (THIS) PURE;
    STDMETHOD_(ULONG, Release)                  (THIS) PURE;
    STDMETHOD_(ASIOBool, Init)                  (THIS_ void *sysRef) PURE;
    STDMETHOD_(void, GetDriverName)             (THIS_ char *name) PURE;
    STDMETHOD_(LONG, GetDriverVersion)          (THIS) PURE;
    STDMETHOD_(void, GetErrorMessage)           (THIS_ char *string) PURE;
    STDMETHOD_(ASIOError, Start)                (THIS) PURE;
    STDMETHOD_(ASIOError, Stop)                 (THIS) PURE;
    STDMETHOD_(ASIOError, GetChannels)          (THIS_ LONG *numInputChannels, LONG *numOutputChannels) PURE;
    STDMETHOD_(ASIOError, GetLatencies)         (THIS_ LONG *inputLatency, LONG *outputLatency) PURE;
    STDMETHOD_(ASIOError, GetBufferSize)        (THIS_ LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity) PURE;
    STDMETHOD_(ASIOError, CanSampleRate)        (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, GetSampleRate)        (THIS_ ASIOSampleRate *sampleRate) PURE;
    STDMETHOD_(ASIOError, SetSampleRate)        (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, GetClockSources)      (THIS_ ASIOClockSource *clocks, LONG *numSources) PURE;
    STDMETHOD_(ASIOError, SetClockSource)       (THIS_ LONG index) PURE;
    STDMETHOD_(ASIOError, GetSamplePosition)    (THIS_ ASIOSamples *sPos, ASIOTimeStamp *tStamp) PURE;
    STDMETHOD_(ASIOError, GetChannelInfo)       (THIS_ ASIOChannelInfo *info) PURE;
    STDMETHOD_(ASIOError, CreateBuffers)        (THIS_ ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks) PURE;
    STDMETHOD_(ASIOError, DisposeBuffers)       (THIS) PURE;
    STDMETHOD_(ASIOError, ControlPanel)         (THIS) PURE;
    STDMETHOD_(ASIOError, Future)               (THIS_ LONG selector,void *opt) PURE;
    STDMETHOD_(ASIOError, OutputReady)          (THIS) PURE;
};
#undef INTERFACE

typedef struct IWineASIO *LPWINEASIO;

typedef struct IOChannel
{
    ASIOBool                    active;
    jack_default_audio_sample_t *audio_buffer;
    char                        port_name[ASIO_MAX_NAME_LENGTH];
    jack_port_t                 *port;
} IOChannel;

typedef struct IWineASIOImpl
{
    /* COM stuff */
    const IWineASIOVtbl         *lpVtbl;
    LONG                        ref;

    /* The app's main window handle on windows, 0 on OS/X */
    HWND                        sys_ref;

    /* ASIO stuff */
    LONG                        asio_active_inputs;
    LONG                        asio_active_outputs;
    BOOL                        asio_buffer_index;
    ASIOCallbacks               *asio_callbacks;
    BOOL                        asio_can_time_code;
    LONG                        asio_current_buffersize;
    INT                         asio_driver_state;
    ASIOSamples                 asio_sample_position;
    ASIOSampleRate              asio_sample_rate;
    ASIOTime                    asio_time;
    BOOL                        asio_time_info_mode;
    ASIOTimeStamp               asio_time_stamp;
    LONG                        asio_version;

    /* WineASIO configuration options */
    int                         wineasio_number_inputs;
    int                         wineasio_number_outputs;
    BOOL                        wineasio_autostart_server;
    BOOL                        wineasio_connect_to_hardware;
    BOOL                        wineasio_fixed_buffersize;
    LONG                        wineasio_preferred_buffersize;

    /* JACK stuff */
    jack_client_t               *jack_client;
    char                        jack_client_name[ASIO_MAX_NAME_LENGTH];
    int                         jack_num_input_ports;
    int                         jack_num_output_ports;
    const char                  **jack_input_ports;
    const char                  **jack_output_ports;

    /* jack process callback buffers */
    jack_default_audio_sample_t *callback_audio_buffer;
    IOChannel                   *input_channel;
    IOChannel                   *output_channel;
} IWineASIOImpl;

enum { Loaded, Initialized, Prepared, Running };

/****************************************************************************
 *  Interface Methods
 */

/*
 *  as seen from the WineASIO source
 */

HIDDEN HRESULT   STDMETHODCALLTYPE      QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject);
HIDDEN ULONG     STDMETHODCALLTYPE      AddRef(LPWINEASIO iface);
HIDDEN ULONG     STDMETHODCALLTYPE      Release(LPWINEASIO iface);
HIDDEN ASIOBool  STDMETHODCALLTYPE      Init(LPWINEASIO iface, void *sysRef);
HIDDEN void      STDMETHODCALLTYPE      GetDriverName(LPWINEASIO iface, char *name);
HIDDEN LONG      STDMETHODCALLTYPE      GetDriverVersion(LPWINEASIO iface);
HIDDEN void      STDMETHODCALLTYPE      GetErrorMessage(LPWINEASIO iface, char *string);
HIDDEN ASIOError STDMETHODCALLTYPE      Start(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      Stop(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels);
HIDDEN ASIOError STDMETHODCALLTYPE      GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency);
HIDDEN ASIOError STDMETHODCALLTYPE      GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
HIDDEN ASIOError STDMETHODCALLTYPE      CanSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      GetSampleRate(LPWINEASIO iface, ASIOSampleRate *sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      SetSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      GetClockSources(LPWINEASIO iface, ASIOClockSource *clocks, LONG *numSources);
HIDDEN ASIOError STDMETHODCALLTYPE      SetClockSource(LPWINEASIO iface, LONG index);
HIDDEN ASIOError STDMETHODCALLTYPE      GetSamplePosition(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp);
HIDDEN ASIOError STDMETHODCALLTYPE      GetChannelInfo(LPWINEASIO iface, ASIOChannelInfo *info);
HIDDEN ASIOError STDMETHODCALLTYPE      CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks);
HIDDEN ASIOError STDMETHODCALLTYPE      DisposeBuffers(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      ControlPanel(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      Future(LPWINEASIO iface, LONG selector, void *opt);
HIDDEN ASIOError STDMETHODCALLTYPE      OutputReady(LPWINEASIO iface);

/*
 * thiscall wrappers for the vtbl (as seen from app side 32bit)
 */

HIDDEN void __thiscall_Init(void);
HIDDEN void __thiscall_GetDriverName(void);
HIDDEN void __thiscall_GetDriverVersion(void);
HIDDEN void __thiscall_GetErrorMessage(void);
HIDDEN void __thiscall_Start(void);
HIDDEN void __thiscall_Stop(void);
HIDDEN void __thiscall_GetChannels(void);
HIDDEN void __thiscall_GetLatencies(void);
HIDDEN void __thiscall_GetBufferSize(void);
HIDDEN void __thiscall_CanSampleRate(void);
HIDDEN void __thiscall_GetSampleRate(void);
HIDDEN void __thiscall_SetSampleRate(void);
HIDDEN void __thiscall_GetClockSources(void);
HIDDEN void __thiscall_SetClockSource(void);
HIDDEN void __thiscall_GetSamplePosition(void);
HIDDEN void __thiscall_GetChannelInfo(void);
HIDDEN void __thiscall_CreateBuffers(void);
HIDDEN void __thiscall_DisposeBuffers(void);
HIDDEN void __thiscall_ControlPanel(void);
HIDDEN void __thiscall_Future(void);
HIDDEN void __thiscall_OutputReady(void);

/*
 *  Jack callbacks
 */

static inline int  jack_buffer_size_callback (jack_nframes_t nframes, void *arg);
static inline void jack_latency_callback(jack_latency_callback_mode_t mode, void *arg);
static inline int  jack_process_callback (jack_nframes_t nframes, void *arg);
static inline int  jack_sample_rate_callback (jack_nframes_t nframes, void *arg);

/*
 *  Support functions
 */

HRESULT WINAPI  WineASIOCreateInstance(REFIID riid, LPVOID *ppobj);
static  VOID    configure_driver(IWineASIOImpl *This);

static DWORD WINAPI jack_thread_creator_helper(LPVOID arg);
static int          jack_thread_creator(pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg);

/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static GUID const CLSID_WineASIO = {
0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };

static const IWineASIOVtbl WineASIO_Vtbl =
{
    (void *) QueryInterface,
    (void *) AddRef,
    (void *) Release,

    (void *) THISCALL(Init),
    (void *) THISCALL(GetDriverName),
    (void *) THISCALL(GetDriverVersion),
    (void *) THISCALL(GetErrorMessage),
    (void *) THISCALL(Start),
    (void *) THISCALL(Stop),
    (void *) THISCALL(GetChannels),
    (void *) THISCALL(GetLatencies),
    (void *) THISCALL(GetBufferSize),
    (void *) THISCALL(CanSampleRate),
    (void *) THISCALL(GetSampleRate),
    (void *) THISCALL(SetSampleRate),
    (void *) THISCALL(GetClockSources),
    (void *) THISCALL(SetClockSource),
    (void *) THISCALL(GetSamplePosition),
    (void *) THISCALL(GetChannelInfo),
    (void *) THISCALL(CreateBuffers),
    (void *) THISCALL(DisposeBuffers),
    (void *) THISCALL(ControlPanel),
    (void *) THISCALL(Future),
    (void *) THISCALL(OutputReady)
};

/* structure needed to create the JACK callback thread in the wine process context */
struct {
    void        *(*jack_callback_thread) (void*);
    void        *arg;
    pthread_t   jack_callback_pthread_id;
    HANDLE      jack_callback_thread_created;
} jack_thread_creator_privates;

/*****************************************************************************
 * Interface method definitions
 */


HIDDEN HRESULT STDMETHODCALLTYPE QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;

    TRACE("iface: %p, riid: %s, ppvObject: %p)\n", iface, debugstr_guid(riid), ppvObject);

    if (ppvObject == NULL)
        return E_INVALIDARG;

    if (IsEqualIID(&CLSID_WineASIO, riid))
    {
        AddRef(iface);
        *ppvObject = This;
        return S_OK;
    }

    return E_NOINTERFACE;
}

/*
 * ULONG STDMETHODCALLTYPE AddRef(LPWINEASIO iface);
 * Function: Increment the reference count on the object
 * Returns:  Ref count
 */

HIDDEN ULONG STDMETHODCALLTYPE AddRef(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    ULONG           ref = InterlockedIncrement(&(This->ref));

    TRACE("iface: %p, ref count is %d\n", iface, ref);
    return ref;
}

/*
 * ULONG Release (LPWINEASIO iface);
 *  Function:   Destroy the interface
 *  Returns:    Ref count
 *  Implies:    ASIOStop() and ASIODisposeBuffers()
 */

HIDDEN ULONG STDMETHODCALLTYPE Release(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    ULONG            ref = InterlockedDecrement(&This->ref);

    TRACE("iface: %p, ref count is %d\n", iface, ref);

    if (This->asio_driver_state == Running)
        Stop(iface);
    if (This->asio_driver_state == Prepared)
        DisposeBuffers(iface);

    if (This->asio_driver_state == Initialized)
    {
        /* just for good measure we deinitialize IOChannel structures and unregister JACK ports */
        for (int i = 0; i < This->wineasio_number_inputs; i++)
        {
            jack_port_unregister (This->jack_client, This->input_channel[i].port);
            This->input_channel[i].active = ASIOFalse;
            This->input_channel[i].port = NULL;
        }
        for (int i = 0; i < This->wineasio_number_outputs; i++)
        {
            jack_port_unregister (This->jack_client, This->output_channel[i].port);
            This->output_channel[i].active = ASIOFalse;
            This->output_channel[i].port = NULL;
        }
        This->asio_active_inputs = This->asio_active_outputs = 0;
        TRACE("%i IOChannel structures released\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

        jack_free (This->jack_output_ports);
        jack_free (This->jack_input_ports);
        jack_client_close(This->jack_client);
        if (This->input_channel)
            HeapFree(GetProcessHeap(), 0, This->input_channel);
    }
    TRACE("WineASIO terminated\n\n");
    if (ref == 0)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

/*
 * ASIOBool Init (void *sysRef);
 *  Function:   Initialize the driver
 *  Parameters: Pointer to "This"
 *              sysHanle is 0 on OS/X and on windows it contains the applications main window handle
 *  Returns:    ASIOFalse on error, and ASIOTrue on success
 */

DEFINE_THISCALL_WRAPPER(Init,8)
HIDDEN ASIOBool STDMETHODCALLTYPE Init(LPWINEASIO iface, void *sysRef)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    jack_status_t   jack_status;
    jack_options_t  jack_options = This->wineasio_autostart_server ? JackNullOption : JackNoStartServer;
    int             i;

    This->sys_ref = sysRef;
    mlockall(MCL_FUTURE);
    configure_driver(This);

    if (!(This->jack_client = jack_client_open(This->jack_client_name, jack_options, &jack_status)))
    {
        WARN("Unable to open a JACK client as: %s\n", This->jack_client_name);
        return ASIOFalse;
    }
    TRACE("JACK client opened as: '%s'\n", jack_get_client_name(This->jack_client));

    This->asio_sample_rate = jack_get_sample_rate(This->jack_client);
    This->asio_current_buffersize = jack_get_buffer_size(This->jack_client);

    /* Allocate IOChannel structures */
    This->input_channel = HeapAlloc(GetProcessHeap(), 0, (This->wineasio_number_inputs + This->wineasio_number_outputs) * sizeof(IOChannel));
    if (!This->input_channel)
    {
        jack_client_close(This->jack_client);
        ERR("Unable to allocate IOChannel structures for %i channels\n", This->wineasio_number_inputs);
        return ASIOFalse;
    }
    This->output_channel = This->input_channel + This->wineasio_number_inputs;
    TRACE("%i IOChannel structures allocated\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

    /* Get and count physical JACK ports */
    This->jack_input_ports = jack_get_ports(This->jack_client, NULL, NULL, JackPortIsPhysical | JackPortIsOutput);
    for (This->jack_num_input_ports = 0; This->jack_input_ports && This->jack_input_ports[This->jack_num_input_ports]; This->jack_num_input_ports++)
        ;
    This->jack_output_ports = jack_get_ports(This->jack_client, NULL, NULL, JackPortIsPhysical | JackPortIsInput);
    for (This->jack_num_output_ports = 0; This->jack_output_ports && This->jack_output_ports[This->jack_num_output_ports]; This->jack_num_output_ports++)
        ;

    /* Initialize IOChannel structures */
    for (i = 0; i < This->wineasio_number_inputs; i++)
    {
        This->input_channel[i].active = ASIOFalse;
        This->input_channel[i].port = NULL;
        snprintf(This->input_channel[i].port_name, ASIO_MAX_NAME_LENGTH, "in_%i", i + 1);
        This->input_channel[i].port = jack_port_register(This->jack_client,
            This->input_channel[i].port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, i);
        /* TRACE("IOChannel structure initialized for input %d: '%s'\n", i, This->input_channel[i].port_name); */
    }
    for (i = 0; i < This->wineasio_number_outputs; i++)
    {
        This->output_channel[i].active = ASIOFalse;
        This->output_channel[i].port = NULL;
        snprintf(This->output_channel[i].port_name, ASIO_MAX_NAME_LENGTH, "out_%i", i + 1);
        This->output_channel[i].port = jack_port_register(This->jack_client,
            This->output_channel[i].port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, i);
        /* TRACE("IOChannel structure initialized for output %d: '%s'\n", i, This->output_channel[i].port_name); */
    }
    TRACE("%i IOChannel structures initialized\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

    jack_set_thread_creator(jack_thread_creator);

    if (jack_set_buffer_size_callback(This->jack_client, jack_buffer_size_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK buffer size change callback\n");
        return ASIOFalse;
    }
    
    if (jack_set_latency_callback(This->jack_client, jack_latency_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK latency callback\n");
        return ASIOFalse;
    }


    if (jack_set_process_callback(This->jack_client, jack_process_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK process callback\n");
        return ASIOFalse;
    }

    if (jack_set_sample_rate_callback (This->jack_client, jack_sample_rate_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK sample rate change callback\n");
        return ASIOFalse;
    }

    This->asio_driver_state = Initialized;
    TRACE("WineASIO 0.%.1f initialized\n",(float) This->asio_version / 10);
    return ASIOTrue;
}

/*
 * void GetDriverName(char *name);
 *  Function:    Returns the driver name in name
 */

DEFINE_THISCALL_WRAPPER(GetDriverName,8)
HIDDEN void STDMETHODCALLTYPE GetDriverName(LPWINEASIO iface, char *name)
{
    TRACE("iface: %p, name: %p\n", iface, name);
    strcpy(name, "WineASIO");
    return;
}

/*
 * LONG GetDriverVersion (void);
 *  Function:    Returns the driver version number
 */

DEFINE_THISCALL_WRAPPER(GetDriverVersion,4)
HIDDEN LONG STDMETHODCALLTYPE GetDriverVersion(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);
    return This->asio_version;
}

/*
 * void GetErrorMessage(char *string);
 *  Function:    Returns an error message for the last occured error in string
 */

DEFINE_THISCALL_WRAPPER(GetErrorMessage,8)
HIDDEN void STDMETHODCALLTYPE GetErrorMessage(LPWINEASIO iface, char *string)
{
    TRACE("iface: %p, string: %p)\n", iface, string);
    strcpy(string, "WineASIO does not return error messages\n");
    return;
}

/*
 * ASIOError Start(void);
 *  Function:    Start JACK IO processing and reset the sample counter to zero
 *  Returns:     ASE_NotPresent if IO is missing
 *               ASE_HWMalfunction if JACK fails to start
 */

DEFINE_THISCALL_WRAPPER(Start,4)
HIDDEN ASIOError STDMETHODCALLTYPE Start(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    int             i;
    DWORD           time;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state != Prepared)
        return ASE_NotPresent;

    /* Zero the audio buffer */
    for (i = 0; i < (This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->asio_current_buffersize; i++)
        This->callback_audio_buffer[i] = 0;

    /* prime the callback by preprocessing one outbound ASIO bufffer */
    This->asio_buffer_index =  0;
    This->asio_sample_position.hi = This->asio_sample_position.lo = 0;

    time = timeGetTime();
    This->asio_time_stamp.lo = time * 1000000;
    This->asio_time_stamp.hi = ((unsigned long long) time * 1000000) >> 32;

    if (This->asio_time_info_mode) /* use the newer bufferSwitchTimeInfo method if supported */
    {
        This->asio_time.timeInfo.samplePosition.lo = This->asio_time.timeInfo.samplePosition.hi = 0;
        This->asio_time.timeInfo.systemTime.lo = This->asio_time_stamp.lo;
        This->asio_time.timeInfo.systemTime.hi = This->asio_time_stamp.hi;
        This->asio_time.timeInfo.sampleRate = This->asio_sample_rate;
        This->asio_time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;

        if (This->asio_can_time_code) /* addionally use time code if supported */
        {
            This->asio_time.timeCode.speed = 1; /* FIXME */
            This->asio_time.timeCode.timeCodeSamples.lo = This->asio_time_stamp.lo;
            This->asio_time.timeCode.timeCodeSamples.hi = This->asio_time_stamp.hi;
            This->asio_time.timeCode.flags = ~(kTcValid | kTcRunning);
        }
        This->asio_callbacks->bufferSwitchTimeInfo(&This->asio_time, This->asio_buffer_index, ASIOTrue);
    } 
    else
    { /* use the old bufferSwitch method */
        This->asio_callbacks->bufferSwitch(This->asio_buffer_index, ASIOTrue);
    }

    /* swith asio buffer */
    This->asio_buffer_index = This->asio_buffer_index ? 0 : 1;

    This->asio_driver_state = Running;
    TRACE("WineASIO successfully loaded\n");
    return ASE_OK;
}

/*
 * ASIOError Stop(void);
 *  Function:   Stop JACK IO processing
 *  Returns:    ASE_NotPresent on missing IO
 *  Note:       BufferSwitch() must not called after returning
 */

DEFINE_THISCALL_WRAPPER(Stop,4)
HIDDEN ASIOError STDMETHODCALLTYPE Stop(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state != Running)
        return ASE_NotPresent;

    This->asio_driver_state = Prepared;

    return ASE_OK;
}

/*
 * ASIOError GetChannels(LONG *numInputChannels, LONG *numOutputChannels);
 *  Function:   Report number of IO channels
 *  Parameters: numInputChannels and numOutputChannels will hold number of channels on returning
 *  Returns:    ASE_NotPresent if no channels are available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetChannels,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    if (!numInputChannels || !numOutputChannels)
        return ASE_InvalidParameter;

    *numInputChannels = This->wineasio_number_inputs;
    *numOutputChannels = This->wineasio_number_outputs;
    TRACE("iface: %p, inputs: %i, outputs: %i\n", iface, This->wineasio_number_inputs, This->wineasio_number_outputs);
    return ASE_OK;
}

/*
 * ASIOError GetLatencies(LONG *inputLatency, LONG *outputLatency);
 *  Function:   Return latency in frames
 *  Returns:    ASE_NotPresent if no IO is available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetLatencies,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency)
{
    IWineASIOImpl           *This = (IWineASIOImpl*)iface;
    jack_latency_range_t    range;

    if (!inputLatency || !outputLatency)
        return ASE_InvalidParameter;

    if (This->asio_driver_state == Loaded)
        return ASE_NotPresent;

    jack_port_get_latency_range(This->input_channel[0].port, JackCaptureLatency, &range);
    *inputLatency = range.max;
    jack_port_get_latency_range(This->output_channel[0].port, JackPlaybackLatency, &range);
    *outputLatency = range.max;
    TRACE("iface: %p, input latency: %d, output latency: %d\n", iface, *inputLatency, *outputLatency);

    return ASE_OK;
}

/*
 * ASIOError GetBufferSize(LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
 *  Function:    Return minimum, maximum, preferred buffer sizes, and granularity
 *               At the moment return all the same, and granularity 0
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetBufferSize,20)
HIDDEN ASIOError STDMETHODCALLTYPE GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, minSize: %p, maxSize: %p, preferredSize: %p, granularity: %p\n", iface, minSize, maxSize, preferredSize, granularity);

    if (!minSize || !maxSize || !preferredSize || !granularity)
        return ASE_InvalidParameter;

    if (This->wineasio_fixed_buffersize)
    {
        *minSize = *maxSize = *preferredSize = This->asio_current_buffersize;
        *granularity = 0;
        TRACE("Buffersize fixed at %i\n", This->asio_current_buffersize);
        return ASE_OK;
    }

    *minSize = ASIO_MINIMUM_BUFFERSIZE;
    *maxSize = ASIO_MAXIMUM_BUFFERSIZE;
    *preferredSize = This->wineasio_preferred_buffersize;
    *granularity = -1;
    TRACE("The ASIO host can control buffersize\nMinimum: %i, maximum: %i, preferred: %i, granularity: %i, current: %i\n",
          *minSize, *maxSize, *preferredSize, *granularity, This->asio_current_buffersize);
    return ASE_OK;
}

/*
 * ASIOError CanSampleRate(ASIOSampleRate sampleRate);
 *  Function:   Ask if specific SR is available
 *  Returns:    ASE_NoClock if SR isn't available, ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(CanSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE CanSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Samplerate = %li, requested samplerate = %li\n", iface, (long) This->asio_sample_rate, (long) sampleRate);

    if (sampleRate != This->asio_sample_rate)
        return ASE_NoClock;
    return ASE_OK;
}

/*
 * ASIOError GetSampleRate(ASIOSampleRate *currentRate);
 *  Function:   Return current SR
 *  Parameters: currentRate will hold SR on return, 0 if unknown
 *  Returns:    ASE_NoClock if SR is unknown, ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetSampleRate,8)
HIDDEN ASIOError STDMETHODCALLTYPE GetSampleRate(LPWINEASIO iface, ASIOSampleRate *sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate is %i\n", iface, (int) This->asio_sample_rate);

    if (!sampleRate)
        return ASE_InvalidParameter;

    *sampleRate = This->asio_sample_rate;
    return ASE_OK;
}

/*
 * ASIOError SetSampleRate(ASIOSampleRate sampleRate);
 *  Function:   Set requested SR, enable external sync if SR == 0
 *  Returns:    ASE_NoClock if unknown SR
 *              ASE_InvalidMode if current clock is external and SR != 0
 *              ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(SetSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE SetSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate %f requested\n", iface, sampleRate);

    if (sampleRate != This->asio_sample_rate)
        return ASE_NoClock;
    return ASE_OK;
}

/*
 * ASIOError GetClockSources(ASIOClockSource *clocks, LONG *numSources);
 *  Function:   Return available clock sources
 *  Parameters: clocks - a pointer to an array of ASIOClockSource structures.
 *              numSources - when called: number of allocated members
 *                         - on return: number of clock sources, the minimum is 1 - the internal clock
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetClockSources,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetClockSources(LPWINEASIO iface, ASIOClockSource *clocks, LONG *numSources)
{
    TRACE("iface: %p, clocks: %p, numSources: %p\n", iface, clocks, numSources);

    if (!clocks || !numSources)
        return ASE_InvalidParameter;

    clocks->index = 0;
    clocks->associatedChannel = -1;
    clocks->associatedGroup = -1;
    clocks->isCurrentSource = ASIOTrue;
    strcpy(clocks->name, "Internal");
    *numSources = 1;
    return ASE_OK;
}

/*
 * ASIOError SetClockSource(LONG index);
 *  Function:   Set clock source
 *  Parameters: index returned by ASIOGetClockSources() - See asio.h for more details
 *  Returns:    ASE_NotPresent on missing IO
 *              ASE_InvalidMode may be returned if a clock can't be selected
 *              ASE_NoClock should not be returned
 */

DEFINE_THISCALL_WRAPPER(SetClockSource,8)
HIDDEN ASIOError STDMETHODCALLTYPE SetClockSource(LPWINEASIO iface, LONG index)
{
    TRACE("iface: %p, index: %i\n", iface, index);

    if (index != 0)
        return ASE_NotPresent;
    return ASE_OK;
}

/*
 * ASIOError GetSamplePosition (ASIOSamples *sPos, ASIOTimeStamp *tStamp);
 *  Function:   Return sample position and timestamp
 *  Parameters: sPos holds the position on return, reset to 0 on ASIOStart()
 *              tStamp holds the system time of sPos
 *  Return:     ASE_NotPresent on missing IO
 *              ASE_SPNotAdvancing on missing clock
 */

DEFINE_THISCALL_WRAPPER(GetSamplePosition,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetSamplePosition(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, sPos: %p, tStamp: %p\n", iface, sPos, tStamp);

    if (!sPos || !tStamp)
        return ASE_InvalidParameter;

    tStamp->lo = This->asio_time_stamp.lo;
    tStamp->hi = This->asio_time_stamp.hi;
    sPos->lo = This->asio_sample_position.lo;
    sPos->hi = 0; /* FIXME */

    return ASE_OK;
}

/*
 * ASIOError GetChannelInfo (ASIOChannelInfo *info);
 *  Function:   Retrive channel info. - See asio.h for more detail
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetChannelInfo,8)
HIDDEN ASIOError STDMETHODCALLTYPE GetChannelInfo(LPWINEASIO iface, ASIOChannelInfo *info)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    /* TRACE("(iface: %p, info: %p\n", iface, info); */

    if (info->channel < 0 || (info->isInput ? info->channel >= This->wineasio_number_inputs : info->channel >= This->wineasio_number_outputs))
        return ASE_InvalidParameter;

    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB;

    if (info->isInput)
    {
        info->isActive = This->input_channel[info->channel].active;
        memcpy(info->name, This->input_channel[info->channel].port_name, ASIO_MAX_NAME_LENGTH);
    }
    else
    {
        info->isActive = This->output_channel[info->channel].active;
        memcpy(info->name, This->output_channel[info->channel].port_name, ASIO_MAX_NAME_LENGTH);
    }
    return ASE_OK;
}

/*
 * ASIOError CreateBuffers(ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks);
 *  Function:   Allocate buffers for IO channels
 *  Parameters: bufferInfo      - pointer to an array of ASIOBufferInfo structures
 *              numChannels     - the total number of IO channels to be allocated
 *              bufferSize      - one of the buffer sizes retrieved with ASIOGetBufferSize()
 *              asioCallbacks   - pointer to an ASIOCallbacks structure
 *              See asio.h for more detail
 *  Returns:    ASE_NoMemory if impossible to allocate enough memory
 *              ASE_InvalidMode on unsupported bufferSize or invalid bufferInfo data
 *              ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(CreateBuffers,20)
HIDDEN ASIOError STDMETHODCALLTYPE CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    ASIOBufferInfo  *buffer_info = bufferInfo;
    int             i, j, k;

    TRACE("iface: %p, bufferInfo: %p, numChannels: %i, bufferSize: %i, asioCallbacks: %p\n", iface, bufferInfo, (int)numChannels, (int)bufferSize, asioCallbacks);

    if (This->asio_driver_state != Initialized)
        return ASE_NotPresent;

    if (!bufferInfo || !asioCallbacks)
        return ASE_InvalidMode;

    /* Check for invalid channel numbers */
    for (i = j = k = 0; i < numChannels; i++, buffer_info++)
    {
        if (buffer_info->isInput)
        {
            if (j++ >= This->wineasio_number_inputs)
            {
                WARN("Invalid input channel requested\n");
                return ASE_InvalidMode;
            }
        }
        else
        {
            if (k++  >= This->wineasio_number_outputs)
            {
                WARN("Invalid output channel requested\n");
                return ASE_InvalidMode;
            }
        }
    }

    /* set buf_size */
    if (This->wineasio_fixed_buffersize)
    {
        if (This->asio_current_buffersize != bufferSize)
            return ASE_InvalidMode;
        TRACE("Buffersize fixed at %i\n", (int)This->asio_current_buffersize);
    }
    else
    { /* fail if not a power of two and if out of range */
        if (!(bufferSize > 0 && !(bufferSize&(bufferSize-1))
                && bufferSize >= ASIO_MINIMUM_BUFFERSIZE
                && bufferSize <= ASIO_MAXIMUM_BUFFERSIZE))
        {
            WARN("Invalid buffersize %i requested\n", (int)bufferSize);
            return ASE_InvalidMode;
        }
        else
        {
            if (This->asio_current_buffersize == bufferSize)
            {
                TRACE("Buffer size already set to %i\n", (int)This->asio_current_buffersize);
            }
            else
            {
                This->asio_current_buffersize = bufferSize;
                if (jack_set_buffer_size(This->jack_client, This->asio_current_buffersize))
                {
                    WARN("JACK is unable to set buffersize to %i\n", (int)This->asio_current_buffersize);
                    return ASE_HWMalfunction;
                }
                TRACE("Buffer size changed to %i\n", (int)This->asio_current_buffersize);
            }
        }
    }

    /* print/discover ASIO host capabilities */
    This->asio_callbacks = asioCallbacks;
    This->asio_time_info_mode = This->asio_can_time_code = FALSE;

    TRACE("The ASIO host supports ASIO v%i: ", This->asio_callbacks->asioMessage(kAsioEngineVersion, 0, 0, 0));
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioBufferSizeChange, 0 , 0))
        TRACE("kAsioBufferSizeChange ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResetRequest, 0 , 0))
        TRACE("kAsioResetRequest ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResyncRequest, 0 , 0))
        TRACE("kAsioResyncRequest ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioLatenciesChanged, 0 , 0))
        TRACE("kAsioLatenciesChanged ");

    if (This->asio_callbacks->asioMessage(kAsioSupportsTimeInfo, 0, 0, 0))
    {
        TRACE("bufferSwitchTimeInfo ");
        This->asio_time_info_mode = TRUE;
        if (This->asio_callbacks->asioMessage(kAsioSupportsTimeCode,  0, 0, 0))
        {
            TRACE("TimeCode");
            This->asio_can_time_code = TRUE;
        }
    }
    else
        TRACE("BufferSwitch");
    TRACE("\n");

    /* Allocate audio buffers */

    This->callback_audio_buffer = HeapAlloc(GetProcessHeap(), 0,
        (This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->asio_current_buffersize * sizeof(jack_default_audio_sample_t));
    if (!This->callback_audio_buffer)
    {
        ERR("Unable to allocate %i ASIO audio buffers\n", This->wineasio_number_inputs + This->wineasio_number_outputs);
        return ASE_NoMemory;
    }
    TRACE("%i ASIO audio buffers allocated (%i kB)\n", This->wineasio_number_inputs + This->wineasio_number_outputs,
          (int) ((This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->asio_current_buffersize * sizeof(jack_default_audio_sample_t) / 1024));

    for (i = 0; i < This->wineasio_number_inputs; i++)
        This->input_channel[i].audio_buffer = This->callback_audio_buffer + (i * 2 * This->asio_current_buffersize);
    for (i = 0; i < This->wineasio_number_outputs; i++)
        This->output_channel[i].audio_buffer = This->callback_audio_buffer + ((This->wineasio_number_inputs + i) * 2 * This->asio_current_buffersize);

    /* initialize ASIOBufferInfo structures */
    buffer_info = bufferInfo;
    This->asio_active_inputs = This->asio_active_outputs = 0;

    for (i = 0; i < This->wineasio_number_inputs; i++) {
        This->input_channel[i].active = ASIOFalse;
    }
    for (i = 0; i < This->wineasio_number_outputs; i++) {
        This->output_channel[i].active = ASIOFalse;
    }

    for (i = 0; i < numChannels; i++, buffer_info++)
    {
        if (buffer_info->isInput)
        {
            buffer_info->buffers[0] = &This->input_channel[buffer_info->channelNum].audio_buffer[0];
            buffer_info->buffers[1] = &This->input_channel[buffer_info->channelNum].audio_buffer[This->asio_current_buffersize];
            This->input_channel[buffer_info->channelNum].active = ASIOTrue;
            This->asio_active_inputs++;
            /* TRACE("ASIO audio buffer for channel %i as input %li created\n", i, This->asio_active_inputs); */
        }
        else
        {
            buffer_info->buffers[0] = &This->output_channel[buffer_info->channelNum].audio_buffer[0];
            buffer_info->buffers[1] = &This->output_channel[buffer_info->channelNum].audio_buffer[This->asio_current_buffersize];
            This->output_channel[buffer_info->channelNum].active = ASIOTrue;
            This->asio_active_outputs++;
            /* TRACE("ASIO audio buffer for channel %i as output %li created\n", i, This->asio_active_outputs); */
        }
    }
    TRACE("%i audio channels initialized\n", This->asio_active_inputs + This->asio_active_outputs);

    if (jack_activate(This->jack_client))
        return ASE_NotPresent;

    /* connect to the hardware io */
    if (This->wineasio_connect_to_hardware)
    {
        for (i = 0; i < This->jack_num_input_ports && i < This->wineasio_number_inputs; i++)
            if (strstr(jack_port_type(jack_port_by_name(This->jack_client, This->jack_input_ports[i])), "audio"))
                jack_connect(This->jack_client, This->jack_input_ports[i], jack_port_name(This->input_channel[i].port));
        for (i = 0; i < This->jack_num_output_ports && i < This->wineasio_number_outputs; i++)
            if (strstr(jack_port_type(jack_port_by_name(This->jack_client, This->jack_output_ports[i])), "audio"))
                jack_connect(This->jack_client, jack_port_name(This->output_channel[i].port), This->jack_output_ports[i]);
    }

    /* at this point all the connections are made and the jack process callback is outputting silence */
    This->asio_driver_state = Prepared;
    return ASE_OK;
}

/*
 * ASIOError DisposeBuffers(void);
 *  Function:   Release allocated buffers
 *  Returns:    ASE_InvalidMode if no buffers were previously allocated
 *              ASE_NotPresent on missing IO
 *  Implies:    ASIOStop()
 */

DEFINE_THISCALL_WRAPPER(DisposeBuffers,4)
HIDDEN ASIOError STDMETHODCALLTYPE DisposeBuffers(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    int             i;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state == Running)
        Stop (iface);
    if (This->asio_driver_state != Prepared)
        return ASE_NotPresent;

    if (jack_deactivate(This->jack_client))
        return ASE_NotPresent;

    This->asio_callbacks = NULL;

    for (i = 0; i < This->wineasio_number_inputs; i++)
    {
        This->input_channel[i].audio_buffer = NULL;
        This->input_channel[i].active = ASIOFalse;
    }
    for (i = 0; i < This->wineasio_number_outputs; i++)
    {
        This->output_channel[i].audio_buffer = NULL;
        This->output_channel[i].active = ASIOFalse;
    }
    This->asio_active_inputs = This->asio_active_outputs = 0;

    if (This->callback_audio_buffer)
        HeapFree(GetProcessHeap(), 0, This->callback_audio_buffer);

    This->asio_driver_state = Initialized;
    return ASE_OK;
}

/*
 * ASIOError ControlPanel(void);
 *  Function:   Open a control panel for driver settings
 *  Returns:    ASE_NotPresent if no control panel exists.  Actually return code should be ignored
 *  Note:       Call the asioMessage callback if something has changed
 */

DEFINE_THISCALL_WRAPPER(ControlPanel,4)
HIDDEN ASIOError STDMETHODCALLTYPE ControlPanel(LPWINEASIO iface)
{
    static char arg0[] = "wineasio-settings\0";
    static char *arg_list[] = { arg0, NULL };

    TRACE("iface: %p\n", iface);

    if (vfork() == 0)
    {
        execvp (arg0, arg_list);
        _exit(1);
    }
    return ASE_OK;
}

/*
 * ASIOError Future(LONG selector, void *opt);
 *  Function:   Various, See asio.h for more detail
 *  Returns:    Depends on the selector but in general ASE_InvalidParameter on invalid selector
 *              ASE_InvalidParameter if function is unsupported to disable further calls
 *              ASE_SUCCESS on success, do not use AES_OK
 */

DEFINE_THISCALL_WRAPPER(Future,12)
HIDDEN ASIOError STDMETHODCALLTYPE Future(LPWINEASIO iface, LONG selector, void *opt)
{
    IWineASIOImpl           *This = (IWineASIOImpl *) iface;

    TRACE("iface: %p, selector: %i, opt: %p\n", iface, selector, opt);

    switch (selector)
    {
        case kAsioEnableTimeCodeRead:
            This->asio_can_time_code = TRUE;
            TRACE("The ASIO host enabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioDisableTimeCodeRead:
            This->asio_can_time_code = FALSE;
            TRACE("The ASIO host disabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioSetInputMonitor:
            TRACE("The driver denied request to set input monitor\n");
            return ASE_NotPresent;
        case kAsioTransport:
            TRACE("The driver denied request for ASIO Transport control\n");
            return ASE_InvalidParameter;
        case kAsioSetInputGain:
            TRACE("The driver denied request to set input gain\n");
            return ASE_InvalidParameter;
        case kAsioGetInputMeter:
            TRACE("The driver denied request to get input meter \n");
            return ASE_InvalidParameter;
        case kAsioSetOutputGain:
            TRACE("The driver denied request to set output gain\n");
            return ASE_InvalidParameter;
        case kAsioGetOutputMeter:
            TRACE("The driver denied request to get output meter\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMonitor:
            TRACE("The driver does not support input monitor\n");
            return ASE_InvalidParameter;
        case kAsioCanTimeInfo:
            TRACE("The driver supports TimeInfo\n");
            return ASE_SUCCESS;
        case kAsioCanTimeCode:
            TRACE("The driver supports TimeCode\n");
            return ASE_SUCCESS;
        case kAsioCanTransport:
            TRACE("The driver denied request for ASIO Transport\n");
            return ASE_InvalidParameter;
        case kAsioCanInputGain:
            TRACE("The driver does not support input gain\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMeter:
            TRACE("The driver does not support input meter\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputGain:
            TRACE("The driver does not support output gain\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputMeter:
            TRACE("The driver does not support output meter\n");
            return ASE_InvalidParameter;
        case kAsioSetIoFormat:
            TRACE("The driver denied request to set DSD IO format\n");
            return ASE_NotPresent;
        case kAsioGetIoFormat:
            TRACE("The driver denied request to get DSD IO format\n");
            return ASE_NotPresent;
        case kAsioCanDoIoFormat:
            TRACE("The driver does not support DSD IO format\n");
            return ASE_NotPresent;
        default:
            TRACE("ASIOFuture() called with undocumented selector\n");
            return ASE_InvalidParameter;
    }
}

/*
 * ASIOError OutputReady(void);
 *  Function:   Tells the driver that output bufffers are ready
 *  Returns:    ASE_OK if supported
 *              ASE_NotPresent to disable
 */

DEFINE_THISCALL_WRAPPER(OutputReady,4)
HIDDEN ASIOError STDMETHODCALLTYPE OutputReady(LPWINEASIO iface)
{
    /* disabled to stop stand alone NI programs from spamming the console
    TRACE("iface: %p\n", iface); */
    return ASE_NotPresent;
}

/****************************************************************************
 *  JACK callbacks
 */

static inline int jack_buffer_size_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->asio_driver_state != Running)
        return 0;

    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResetRequest, 0 , 0))
        This->asio_callbacks->asioMessage(kAsioResetRequest, 0, 0, 0);
    return 0;
}

static inline void jack_latency_callback(jack_latency_callback_mode_t mode, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->asio_driver_state != Running)
        return;

    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioLatenciesChanged, 0 , 0))
        This->asio_callbacks->asioMessage(kAsioLatenciesChanged, 0, 0, 0);

    return;
}

static inline int jack_process_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl               *This = (IWineASIOImpl*)arg;

    int                         i;
    jack_transport_state_t      jack_transport_state;
    jack_position_t             jack_position;
    DWORD                       time;

    /* output silence if the ASIO callback isn't running yet */
    if (This->asio_driver_state != Running)
    {
        for (i = 0; i < This->asio_active_outputs; i++)
            bzero(jack_port_get_buffer(This->output_channel[i].port, nframes), sizeof (jack_default_audio_sample_t) * nframes);
        return 0;
    }

    /* copy jack to asio buffers */
    for (i = 0; i < This->wineasio_number_inputs; i++)
        if (This->input_channel[i].active == ASIOTrue)
            memcpy (&This->input_channel[i].audio_buffer[nframes * This->asio_buffer_index],
                    jack_port_get_buffer(This->input_channel[i].port, nframes),
                    sizeof (jack_default_audio_sample_t) * nframes);

    if (This->asio_sample_position.lo > ULONG_MAX - nframes)
        This->asio_sample_position.hi++;
    This->asio_sample_position.lo += nframes;

    time = timeGetTime();
    This->asio_time_stamp.lo = time * 1000000;
    This->asio_time_stamp.hi = ((unsigned long long) time * 1000000) >> 32;

    if (This->asio_time_info_mode) /* use the newer bufferSwitchTimeInfo method if supported */
    {
        This->asio_time.timeInfo.samplePosition.lo = This->asio_sample_position.lo;
        This->asio_time.timeInfo.samplePosition.hi = This->asio_sample_position.hi;
        This->asio_time.timeInfo.systemTime.lo = This->asio_time_stamp.lo;
        This->asio_time.timeInfo.systemTime.hi = This->asio_time_stamp.hi;
        This->asio_time.timeInfo.sampleRate = This->asio_sample_rate;
        This->asio_time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;

        if (This->asio_can_time_code) /* FIXME addionally use time code if supported */
        {
            jack_transport_state = jack_transport_query(This->jack_client, &jack_position);
            This->asio_time.timeCode.flags = kTcValid;
            if (jack_transport_state == JackTransportRolling)
                This->asio_time.timeCode.flags |= kTcRunning;
        }
        This->asio_callbacks->bufferSwitchTimeInfo(&This->asio_time, This->asio_buffer_index, ASIOTrue);
    }
    else
    { /* use the old bufferSwitch method */
        This->asio_callbacks->bufferSwitch(This->asio_buffer_index, ASIOTrue);
    }

    /* copy asio to jack buffers */
    for (i = 0; i < This->wineasio_number_outputs; i++)
        if (This->output_channel[i].active == ASIOTrue)
            memcpy(jack_port_get_buffer(This->output_channel[i].port, nframes),
                    &This->output_channel[i].audio_buffer[nframes * This->asio_buffer_index],
                    sizeof (jack_default_audio_sample_t) * nframes);

    /* swith asio buffer */
    This->asio_buffer_index = This->asio_buffer_index ? 0 : 1;
    return 0;
}

static inline int jack_sample_rate_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->asio_driver_state != Running)
        return 0;

    This->asio_sample_rate = nframes;
    This->asio_callbacks->sampleRateDidChange(nframes);
    return 0;
}

/*****************************************************************************
 *  Support functions
 */

#ifndef WINE_WITH_UNICODE
/* Funtion required as unicode.h no longer in WINE */
static WCHAR *strrchrW(const WCHAR* str, WCHAR ch)
{
    WCHAR *ret = NULL;
    do { if (*str == ch) ret = (WCHAR *)(ULONG_PTR)str; } while (*str++);
    return ret;
}
#endif

/* Function called by JACK to create a thread in the wine process context,
 *  uses the global structure jack_thread_creator_privates to communicate with jack_thread_creator_helper() */
static int jack_thread_creator(pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg)
{
    TRACE("arg: %p, thread_id: %p, attr: %p, function: %p\n", arg, thread_id, attr, function);

    jack_thread_creator_privates.jack_callback_thread = function;
    jack_thread_creator_privates.arg = arg;
    jack_thread_creator_privates.jack_callback_thread_created = CreateEventW(NULL, FALSE, FALSE, NULL);
    CreateThread( NULL, 0, jack_thread_creator_helper, arg, 0,0 );
    WaitForSingleObject(jack_thread_creator_privates.jack_callback_thread_created, INFINITE);
    *thread_id = jack_thread_creator_privates.jack_callback_pthread_id;
    return 0;
}

/* internal helper function for returning the posix thread_id of the newly created callback thread */
static DWORD WINAPI jack_thread_creator_helper(LPVOID arg)
{
    TRACE("arg: %p\n", arg);

    jack_thread_creator_privates.jack_callback_pthread_id = pthread_self();
    SetEvent(jack_thread_creator_privates.jack_callback_thread_created);
    jack_thread_creator_privates.jack_callback_thread(jack_thread_creator_privates.arg);
    return 0;
}

static VOID configure_driver(IWineASIOImpl *This)
{
    HKEY    hkey;
    LONG    result, value;
    DWORD   type, size;
    WCHAR   application_path [MAX_PATH];
    WCHAR   *application_name;
    char    environment_variable[MAX_ENVIRONMENT_SIZE];

    /* Unicode strings used for the registry */
    static const WCHAR key_software_wine_wineasio[] =
        { 'S','o','f','t','w','a','r','e','\\',
          'W','i','n','e','\\',
          'W','i','n','e','A','S','I','O',0 };
    static const WCHAR value_wineasio_number_inputs[] =
        { 'N','u','m','b','e','r',' ','o','f',' ','i','n','p','u','t','s',0 };
    static const WCHAR value_wineasio_number_outputs[] =
        { 'N','u','m','b','e','r',' ','o','f',' ','o','u','t','p','u','t','s',0 };
    static const WCHAR value_wineasio_fixed_buffersize[] =
        { 'F','i','x','e','d',' ','b','u','f','f','e','r','s','i','z','e',0 };
    static const WCHAR value_wineasio_preferred_buffersize[] =
        { 'P','r','e','f','e','r','r','e','d',' ','b','u','f','f','e','r','s','i','z','e',0 };
    static const WCHAR wineasio_autostart_server[] =
        { 'A','u','t','o','s','t','a','r','t',' ','s','e','r','v','e','r',0 };
    static const WCHAR value_wineasio_connect_to_hardware[] =
        { 'C','o','n','n','e','c','t',' ','t','o',' ','h','a','r','d','w','a','r','e',0 };

    /* Initialise most member variables,
     * asio_sample_position, asio_time, & asio_time_stamp are initialized in Start()
     * jack_num_input_ports & jack_num_output_ports are initialized in Init() */
    This->asio_active_inputs = 0;
    This->asio_active_outputs = 0;
    This->asio_buffer_index = 0;
    This->asio_callbacks = NULL;
    This->asio_can_time_code = FALSE;
    This->asio_current_buffersize = 0;
    This->asio_driver_state = Loaded;
    This->asio_sample_rate = 0;
    This->asio_time_info_mode = FALSE;
    This->asio_version = 92;

    This->wineasio_number_inputs = 16;
    This->wineasio_number_outputs = 16;
    This->wineasio_autostart_server = FALSE;
    This->wineasio_connect_to_hardware = TRUE;
    This->wineasio_fixed_buffersize = TRUE;
    This->wineasio_preferred_buffersize = ASIO_PREFERRED_BUFFERSIZE;

    This->jack_client = NULL;
    This->jack_client_name[0] = 0;
    This->jack_input_ports = NULL;
    This->jack_output_ports = NULL;
    This->callback_audio_buffer = NULL;
    This->input_channel = NULL;
    This->output_channel = NULL;

    /* create registry entries with defaults if not present */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, key_software_wine_wineasio, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);

    /* get/set number of asio inputs */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_wineasio_number_inputs, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_number_inputs = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_number_inputs;
        result = RegSetValueExW(hkey, value_wineasio_number_inputs, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* get/set number of asio outputs */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_wineasio_number_outputs, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_number_outputs = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_number_outputs;
        result = RegSetValueExW(hkey, value_wineasio_number_outputs, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* allow changing of asio buffer sizes */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_wineasio_fixed_buffersize, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_fixed_buffersize = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_fixed_buffersize;
        result = RegSetValueExW(hkey, value_wineasio_fixed_buffersize, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* preferred buffer size (if changing buffersize is allowed) */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_wineasio_preferred_buffersize, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_preferred_buffersize = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_preferred_buffersize;
        result = RegSetValueExW(hkey, value_wineasio_preferred_buffersize, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* get/set JACK autostart */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, wineasio_autostart_server, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_autostart_server = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_autostart_server;
        result = RegSetValueExW(hkey, wineasio_autostart_server, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* get/set JACK connect to physical io */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_wineasio_connect_to_hardware, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_connect_to_hardware = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_connect_to_hardware;
        result = RegSetValueExW(hkey, value_wineasio_connect_to_hardware, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* get client name by stripping path and extension */
    GetModuleFileNameW(0, application_path, MAX_PATH);
    application_name = strrchrW(application_path, L'.');
    *application_name = 0;
    application_name = strrchrW(application_path, L'\\');
    application_name++;
    WideCharToMultiByte(CP_ACP, WC_SEPCHARS, application_name, -1, This->jack_client_name, ASIO_MAX_NAME_LENGTH, NULL, NULL);

    RegCloseKey(hkey);

    /* Look for environment variables to override registry config values */

    if (GetEnvironmentVariableA("WINEASIO_NUMBER_INPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_inputs = result;
    }

    if (GetEnvironmentVariableA("WINEASIO_NUMBER_OUTPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_outputs = result;
    }

    if (GetEnvironmentVariableA("WINEASIO_AUTOSTART_SERVER", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        if (!strcasecmp(environment_variable, "on"))
            This->wineasio_autostart_server = TRUE;
        else if (!strcasecmp(environment_variable, "off"))
            This->wineasio_autostart_server = FALSE;
    }

    if (GetEnvironmentVariableA("WINEASIO_CONNECT_TO_HARDWARE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        if (!strcasecmp(environment_variable, "on"))
            This->wineasio_connect_to_hardware = TRUE;
        else if (!strcasecmp(environment_variable, "off"))
            This->wineasio_connect_to_hardware = FALSE;
    }

    if (GetEnvironmentVariableA("WINEASIO_FIXED_BUFFERSIZE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        if (!strcasecmp(environment_variable, "on"))
            This->wineasio_fixed_buffersize = TRUE;
        else if (!strcasecmp(environment_variable, "off"))
            This->wineasio_fixed_buffersize = FALSE;
    }

    if (GetEnvironmentVariableA("WINEASIO_PREFERRED_BUFFERSIZE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_preferred_buffersize = result;
    }

    /* over ride the JACK client name gotten from the application name */
    size = GetEnvironmentVariableA("WINEASIO_CLIENT_NAME", environment_variable, ASIO_MAX_NAME_LENGTH);
    if (size > 0 && size < ASIO_MAX_NAME_LENGTH)
        strcpy(This->jack_client_name, environment_variable);

    /* if wineasio_preferred_buffersize is not a power of two or if out of range, then set to ASIO_PREFERRED_BUFFERSIZE */
    if (!(This->wineasio_preferred_buffersize > 0 && !(This->wineasio_preferred_buffersize&(This->wineasio_preferred_buffersize-1))
            && This->wineasio_preferred_buffersize >= ASIO_MINIMUM_BUFFERSIZE
            && This->wineasio_preferred_buffersize <= ASIO_MAXIMUM_BUFFERSIZE))
        This->wineasio_preferred_buffersize = ASIO_PREFERRED_BUFFERSIZE;

    return;
}

/* Allocate the interface pointer and associate it with the vtbl/WineASIO object */
HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj)
{
    IWineASIOImpl   *pobj;

    /* TRACE("riid: %s, ppobj: %p\n", debugstr_guid(riid), ppobj); */

    pobj = HeapAlloc(GetProcessHeap(), 0, sizeof(*pobj));
    if (pobj == NULL)
    {
        WARN("out of memory\n");
        return E_OUTOFMEMORY;
    }

    pobj->lpVtbl = &WineASIO_Vtbl;
    pobj->ref = 1;
    TRACE("pobj = %p\n", pobj);
    *ppobj = pobj;
    /* TRACE("return %p\n", *ppobj); */
    return S_OK;
}
