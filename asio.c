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

#include <stdbool.h>
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

#ifdef DEBUG
WINE_DEFAULT_DEBUG_CHANNEL(asio);
#endif

#define MAX_ENVIRONMENT_SIZE            6
#define WINEASIO_MAX_NAME_LENGTH        32
#define WINEASIO_MINIMUM_BUFFERSIZE     16
#define WINEASIO_MAXIMUM_BUFFERSIZE     8192
#define WINEASIO_PREFERRED_BUFFERSIZE   1024

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

#ifdef _WIN64
#define WINEASIO_CALLBACK CALLBACK
#else
#define WINEASIO_CALLBACK
#endif

typedef struct w_int64_t {
    ULONG hi;
    ULONG lo;
} w_int64_t;

typedef struct BufferInformation
{
    LONG isInputType;
    LONG channelNumber;
    void *audioBufferStart;
    void *audioBufferEnd;
} BufferInformation;

typedef struct TimeInformation
{
    LONG      _1[4];
    double    _2;
    w_int64_t timeStamp;
    w_int64_t numSamples;
    double    sampleRate;
    ULONG     flags;
    char      _3[12];
    double    speedForTimeCode;
    w_int64_t timeStampForTimeCode;
    ULONG     flagsForTimeCode;
    char      _4[64];
} TimeInformation;

typedef struct Callbacks
{
    void (WINEASIO_CALLBACK *swapBuffers) (LONG, LONG);
    void (WINEASIO_CALLBACK *sampleRateChanged) (double);
    LONG (WINEASIO_CALLBACK *sendNotification) (LONG, LONG, void*, double*);
    void* (WINEASIO_CALLBACK *swapBuffersWithTimeInfo) (TimeInformation*, LONG, LONG);
} Callbacks;

/*****************************************************************************
 * IWineAsio interface
 */

#define INTERFACE IWineASIO
DECLARE_INTERFACE_(IWineASIO,IUnknown)
{
    STDMETHOD_(HRESULT, QueryInterface)         (THIS_ IID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)                   (THIS) PURE;
    STDMETHOD_(ULONG, Release)                  (THIS) PURE;
    STDMETHOD_(LONG, Init)                      (THIS_ void *sysRef) PURE;
    STDMETHOD_(void, GetDriverName)             (THIS_ char *name) PURE;
    STDMETHOD_(LONG, GetDriverVersion)          (THIS) PURE;
    STDMETHOD_(void, GetErrorMessage)           (THIS_ char *string) PURE;
    STDMETHOD_(LONG, Start)                     (THIS) PURE;
    STDMETHOD_(LONG, Stop)                      (THIS) PURE;
    STDMETHOD_(LONG, GetChannels)               (THIS_ LONG *numInputChannels, LONG *numOutputChannels) PURE;
    STDMETHOD_(LONG, GetLatencies)              (THIS_ LONG *inputLatency, LONG *outputLatency) PURE;
    STDMETHOD_(LONG, GetBufferSize)             (THIS_ LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity) PURE;
    STDMETHOD_(LONG, CanSampleRate)             (THIS_ double sampleRate) PURE;
    STDMETHOD_(LONG, GetSampleRate)             (THIS_ double *sampleRate) PURE;
    STDMETHOD_(LONG, SetSampleRate)             (THIS_ double sampleRate) PURE;
    STDMETHOD_(LONG, GetClockSources)           (THIS_ void *clocks, LONG *numSources) PURE;
    STDMETHOD_(LONG, SetClockSource)            (THIS_ LONG index) PURE;
    STDMETHOD_(LONG, GetSamplePosition)         (THIS_ w_int64_t *sPos, w_int64_t *tStamp) PURE;
    STDMETHOD_(LONG, GetChannelInfo)            (THIS_ void *info) PURE;
    STDMETHOD_(LONG, CreateBuffers)             (THIS_ BufferInformation *bufferInfo, LONG numChannels, LONG bufferSize, Callbacks *callbacks) PURE;
    STDMETHOD_(LONG, DisposeBuffers)            (THIS) PURE;
    STDMETHOD_(LONG, ControlPanel)              (THIS) PURE;
    STDMETHOD_(LONG, Future)                    (THIS_ LONG selector,void *opt) PURE;
    STDMETHOD_(LONG, OutputReady)               (THIS) PURE;
};
#undef INTERFACE

typedef struct IWineASIO *LPWINEASIO;

typedef struct IOChannel
{
    jack_default_audio_sample_t *audio_buffer;
    char                        port_name[WINEASIO_MAX_NAME_LENGTH];
    jack_port_t                 *port;
    bool                        active;
} IOChannel;

typedef struct IWineASIOImpl
{
    /* COM stuff */
    const IWineASIOVtbl         *lpVtbl;
    LONG                        ref;

    /* The app's main window handle on windows, 0 on OS/X */
    HWND                        sys_ref;

    /* Host stuff */
    LONG                        host_active_inputs;
    LONG                        host_active_outputs;
    BOOL                        host_buffer_index;
    Callbacks                  *host_callbacks;
    BOOL                        host_can_time_code;
    LONG                        host_current_buffersize;
    INT                         host_driver_state;
    w_int64_t                   host_num_samples;
    double                      host_sample_rate;
    TimeInformation             host_time;
    BOOL                        host_time_info_mode;
    w_int64_t                   host_time_stamp;
    LONG                        host_version;

    /* WineASIO configuration options */
    int                         wineasio_number_inputs;
    int                         wineasio_number_outputs;
    BOOL                        wineasio_autostart_server;
    BOOL                        wineasio_connect_to_hardware;
    BOOL                        wineasio_fixed_buffersize;
    LONG                        wineasio_preferred_buffersize;

    /* JACK stuff */
    jack_client_t               *jack_client;
    char                        jack_client_name[WINEASIO_MAX_NAME_LENGTH];
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

HIDDEN HRESULT STDMETHODCALLTYPE      QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject);
HIDDEN ULONG   STDMETHODCALLTYPE      AddRef(LPWINEASIO iface);
HIDDEN ULONG   STDMETHODCALLTYPE      Release(LPWINEASIO iface);
HIDDEN LONG    STDMETHODCALLTYPE      Init(LPWINEASIO iface, void *sysRef);
HIDDEN void    STDMETHODCALLTYPE      GetDriverName(LPWINEASIO iface, char *name);
HIDDEN LONG    STDMETHODCALLTYPE      GetDriverVersion(LPWINEASIO iface);
HIDDEN void    STDMETHODCALLTYPE      GetErrorMessage(LPWINEASIO iface, char *string);
HIDDEN LONG    STDMETHODCALLTYPE      Start(LPWINEASIO iface);
HIDDEN LONG    STDMETHODCALLTYPE      Stop(LPWINEASIO iface);
HIDDEN LONG    STDMETHODCALLTYPE      GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels);
HIDDEN LONG    STDMETHODCALLTYPE      GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency);
HIDDEN LONG    STDMETHODCALLTYPE      GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
HIDDEN LONG    STDMETHODCALLTYPE      CanSampleRate(LPWINEASIO iface, double sampleRate);
HIDDEN LONG    STDMETHODCALLTYPE      GetSampleRate(LPWINEASIO iface, double *sampleRate);
HIDDEN LONG    STDMETHODCALLTYPE      SetSampleRate(LPWINEASIO iface, double sampleRate);
HIDDEN LONG    STDMETHODCALLTYPE      GetClockSources(LPWINEASIO iface, void *clocks, LONG *numSources);
HIDDEN LONG    STDMETHODCALLTYPE      SetClockSource(LPWINEASIO iface, LONG index);
HIDDEN LONG    STDMETHODCALLTYPE      GetSamplePosition(LPWINEASIO iface, w_int64_t *sPos, w_int64_t *tStamp);
HIDDEN LONG    STDMETHODCALLTYPE      GetChannelInfo(LPWINEASIO iface, void *info);
HIDDEN LONG    STDMETHODCALLTYPE      CreateBuffers(LPWINEASIO iface, BufferInformation *bufferInfo, LONG numChannels, LONG bufferSize, Callbacks *callbacks);
HIDDEN LONG    STDMETHODCALLTYPE      DisposeBuffers(LPWINEASIO iface);
HIDDEN LONG    STDMETHODCALLTYPE      ControlPanel(LPWINEASIO iface);
HIDDEN LONG    STDMETHODCALLTYPE      Future(LPWINEASIO iface, LONG selector, void *opt);
HIDDEN LONG    STDMETHODCALLTYPE      OutputReady(LPWINEASIO iface);

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
 *  Implies:    Stop() and DisposeBuffers()
 */

HIDDEN ULONG STDMETHODCALLTYPE Release(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    ULONG            ref = InterlockedDecrement(&This->ref);

    TRACE("iface: %p, ref count is %d\n", iface, ref);

    if (This->host_driver_state == Running)
        Stop(iface);
    if (This->host_driver_state == Prepared)
        DisposeBuffers(iface);

    if (This->host_driver_state == Initialized)
    {
        /* just for good measure we deinitialize IOChannel structures and unregister JACK ports */
        for (int i = 0; i < This->wineasio_number_inputs; i++)
        {
            jack_port_unregister (This->jack_client, This->input_channel[i].port);
            This->input_channel[i].active = false;
            This->input_channel[i].port = NULL;
        }
        for (int i = 0; i < This->wineasio_number_outputs; i++)
        {
            jack_port_unregister (This->jack_client, This->output_channel[i].port);
            This->output_channel[i].active = false;
            This->output_channel[i].port = NULL;
        }
        This->host_active_inputs = This->host_active_outputs = 0;
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
 * LONG Init (void *sysRef);
 *  Function:   Initialize the driver
 *  Parameters: Pointer to "This"
 *              sysHanle is 0 on OS/X and on windows it contains the applications main window handle
 *  Returns:    0 on error, and 1 on success
 */

DEFINE_THISCALL_WRAPPER(Init,8)
HIDDEN LONG STDMETHODCALLTYPE Init(LPWINEASIO iface, void *sysRef)
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
        return 0;
    }
    TRACE("JACK client opened as: '%s'\n", jack_get_client_name(This->jack_client));

    This->host_sample_rate = jack_get_sample_rate(This->jack_client);
    This->host_current_buffersize = jack_get_buffer_size(This->jack_client);

    /* Allocate IOChannel structures */
    This->input_channel = HeapAlloc(GetProcessHeap(), 0, (This->wineasio_number_inputs + This->wineasio_number_outputs) * sizeof(IOChannel));
    if (!This->input_channel)
    {
        jack_client_close(This->jack_client);
        ERR("Unable to allocate IOChannel structures for %i channels\n", This->wineasio_number_inputs);
        return 0;
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
        This->input_channel[i].active = false;
        This->input_channel[i].port = NULL;
        snprintf(This->input_channel[i].port_name, WINEASIO_MAX_NAME_LENGTH, "in_%i", i + 1);
        This->input_channel[i].port = jack_port_register(This->jack_client,
            This->input_channel[i].port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, i);
        /* TRACE("IOChannel structure initialized for input %d: '%s'\n", i, This->input_channel[i].port_name); */
    }
    for (i = 0; i < This->wineasio_number_outputs; i++)
    {
        This->output_channel[i].active = false;
        This->output_channel[i].port = NULL;
        snprintf(This->output_channel[i].port_name, WINEASIO_MAX_NAME_LENGTH, "out_%i", i + 1);
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
        return 0;
    }
    
    if (jack_set_latency_callback(This->jack_client, jack_latency_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK latency callback\n");
        return 0;
    }

    if (jack_set_process_callback(This->jack_client, jack_process_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK process callback\n");
        return 0;
    }

    if (jack_set_sample_rate_callback (This->jack_client, jack_sample_rate_callback, This))
    {
        jack_client_close(This->jack_client);
        HeapFree(GetProcessHeap(), 0, This->input_channel);
        ERR("Unable to register JACK sample rate change callback\n");
        return 0;
    }

    This->host_driver_state = Initialized;
    TRACE("WineASIO 0.%.1f initialized\n",(float) This->host_version / 10);
    return 1;
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
    return This->host_version;
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
 * LONG Start(void);
 *  Function:    Start JACK IO processing and reset the sample counter to zero
 *  Returns:     -1000 if IO is missing
 *               -999 if JACK fails to start
 */

DEFINE_THISCALL_WRAPPER(Start,4)
HIDDEN LONG STDMETHODCALLTYPE Start(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    int             i;
    DWORD           time;

    TRACE("iface: %p\n", iface);

    if (This->host_driver_state != Prepared)
        return -1000;

    /* Zero the audio buffer */
    for (i = 0; i < (This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->host_current_buffersize; i++)
        This->callback_audio_buffer[i] = 0;

    /* prime the callback by preprocessing one outbound host bufffer */
    This->host_buffer_index =  0;
    This->host_num_samples.hi = This->host_num_samples.lo = 0;

    time = timeGetTime();
    This->host_time_stamp.lo = time * 1000000;
    This->host_time_stamp.hi = ((unsigned long long) time * 1000000) >> 32;

    if (This->host_time_info_mode) /* use the newer swapBuffersWithTimeInfo method if supported */
    {
        This->host_time.numSamples.lo = This->host_time.numSamples.hi = 0;
        This->host_time.timeStamp.lo = This->host_time_stamp.lo;
        This->host_time.timeStamp.hi = This->host_time_stamp.hi;
        This->host_time.sampleRate = This->host_sample_rate;
        This->host_time.flags = 0x7;

        if (This->host_can_time_code) /* addionally use time code if supported */
        {
            This->host_time.speedForTimeCode = 1; /* FIXME */
            This->host_time.timeStampForTimeCode.lo = This->host_time_stamp.lo;
            This->host_time.timeStampForTimeCode.hi = This->host_time_stamp.hi;
            This->host_time.flagsForTimeCode = ~(0x3);
        }
        This->host_callbacks->swapBuffersWithTimeInfo(&This->host_time, This->host_buffer_index, 1);
    } 
    else
    { /* use the old swapBuffers method */
        This->host_callbacks->swapBuffers(This->host_buffer_index, 1);
    }

    /* switch host buffer */
    This->host_buffer_index = This->host_buffer_index ? 0 : 1;

    This->host_driver_state = Running;
    TRACE("WineASIO successfully loaded\n");
    return 0;
}

/*
 * LONG Stop(void);
 *  Function:   Stop JACK IO processing
 *  Returns:    -1000 on missing IO
 *  Note:       swapBuffers() must not called after returning
 */

DEFINE_THISCALL_WRAPPER(Stop,4)
HIDDEN LONG STDMETHODCALLTYPE Stop(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);

    if (This->host_driver_state != Running)
        return -1000;

    This->host_driver_state = Prepared;

    return 0;
}

/*
 * LONG GetChannels(LONG *numInputChannels, LONG *numOutputChannels);
 *  Function:   Report number of IO channels
 *  Parameters: numInputChannels and numOutputChannels will hold number of channels on returning
 *  Returns:    -1000 if no channels are available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetChannels,12)
HIDDEN LONG STDMETHODCALLTYPE GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    if (!numInputChannels || !numOutputChannels)
        return -998;

    *numInputChannels = This->wineasio_number_inputs;
    *numOutputChannels = This->wineasio_number_outputs;
    TRACE("iface: %p, inputs: %i, outputs: %i\n", iface, This->wineasio_number_inputs, This->wineasio_number_outputs);
    return 0;
}

/*
 * LONG GetLatencies(LONG *inputLatency, LONG *outputLatency);
 *  Function:   Return latency in frames
 *  Returns:    -1000 if no IO is available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetLatencies,12)
HIDDEN LONG STDMETHODCALLTYPE GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency)
{
    IWineASIOImpl           *This = (IWineASIOImpl*)iface;
    jack_latency_range_t    range;

    if (!inputLatency || !outputLatency)
        return -998;

    if (This->host_driver_state == Loaded)
        return -1000;

    jack_port_get_latency_range(This->input_channel[0].port, JackCaptureLatency, &range);
    *inputLatency = range.max;
    jack_port_get_latency_range(This->output_channel[0].port, JackPlaybackLatency, &range);
    *outputLatency = range.max;
    TRACE("iface: %p, input latency: %d, output latency: %d\n", iface, *inputLatency, *outputLatency);

    return 0;
}

/*
 * LONG GetBufferSize(LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
 *  Function:    Return minimum, maximum, preferred buffer sizes, and granularity
 *               At the moment return all the same, and granularity 0
 *  Returns:    -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetBufferSize,20)
HIDDEN LONG STDMETHODCALLTYPE GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, minSize: %p, maxSize: %p, preferredSize: %p, granularity: %p\n", iface, minSize, maxSize, preferredSize, granularity);

    if (!minSize || !maxSize || !preferredSize || !granularity)
        return -998;

    if (This->wineasio_fixed_buffersize)
    {
        *minSize = *maxSize = *preferredSize = This->host_current_buffersize;
        *granularity = 0;
        TRACE("Buffersize fixed at %i\n", This->host_current_buffersize);
        return 0;
    }

    *minSize = WINEASIO_MINIMUM_BUFFERSIZE;
    *maxSize = WINEASIO_MAXIMUM_BUFFERSIZE;
    *preferredSize = This->wineasio_preferred_buffersize;
    *granularity = -1;
    TRACE("The host can control buffersize\nMinimum: %i, maximum: %i, preferred: %i, granularity: %i, current: %i\n",
          *minSize, *maxSize, *preferredSize, *granularity, This->host_current_buffersize);
    return 0;
}

/*
 * LONG CanSampleRate(double sampleRate);
 *  Function:   Ask if specific SR is available
 *  Returns:    -995 if SR isn't available, -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(CanSampleRate,12)
HIDDEN LONG STDMETHODCALLTYPE CanSampleRate(LPWINEASIO iface, double sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Samplerate = %li, requested samplerate = %li\n", iface, (long) This->host_sample_rate, (long) sampleRate);

    if (sampleRate != This->host_sample_rate)
        return -995;
    return 0;
}

/*
 * LONG GetSampleRate(double *currentRate);
 *  Function:   Return current SR
 *  Parameters: currentRate will hold SR on return, 0 if unknown
 *  Returns:    -995 if SR is unknown, -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetSampleRate,8)
HIDDEN LONG STDMETHODCALLTYPE GetSampleRate(LPWINEASIO iface, double *sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate is %i\n", iface, (int) This->host_sample_rate);

    if (!sampleRate)
        return -998;

    *sampleRate = This->host_sample_rate;
    return 0;
}

/*
 * LONG SetSampleRate(double sampleRate);
 *  Function:   Set requested SR, enable external sync if SR == 0
 *  Returns:    -995 if unknown SR
 *              -997 if current clock is external and SR != 0
 *              -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(SetSampleRate,12)
HIDDEN LONG STDMETHODCALLTYPE SetSampleRate(LPWINEASIO iface, double sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate %f requested\n", iface, sampleRate);

    if (sampleRate != This->host_sample_rate)
        return -995;
    return 0;
}

/*
 * LONG GetClockSources(void *clocks, LONG *numSources);
 *  Function:   Return available clock sources
 *  Parameters: clocks - a pointer to an array of clock source structures.
 *              numSources - when called: number of allocated members
 *                         - on return: number of clock sources, the minimum is 1 - the internal clock
 *  Returns:    -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetClockSources,12)
HIDDEN LONG STDMETHODCALLTYPE GetClockSources(LPWINEASIO iface, void *clocks, LONG *numSources)
{
    LONG *lclocks = (LONG*)clocks;

    TRACE("iface: %p, clocks: %p, numSources: %p\n", iface, clocks, numSources);

    if (!clocks || !numSources)
        return -998;

    *lclocks++ = 0;
    *lclocks++ = -1;
    *lclocks++ = -1;
    *lclocks++ = 1;
    strcpy((char*)lclocks, "Internal");
    *numSources = 1;
    return 0;
}

/*
 * LONG SetClockSource(LONG index);
 *  Function:   Set clock source
 *  Parameters: index returned by GetClockSources()
 *  Returns:    -1000 on missing IO
 *              -997 may be returned if a clock can't be selected
 *              -995 should not be returned
 */

DEFINE_THISCALL_WRAPPER(SetClockSource,8)
HIDDEN LONG STDMETHODCALLTYPE SetClockSource(LPWINEASIO iface, LONG index)
{
    TRACE("iface: %p, index: %i\n", iface, index);

    if (index != 0)
        return -1000;
    return 0;
}

/*
 * LONG GetSamplePosition (w_int64_t *sPos, w_int64_t *tStamp);
 *  Function:   Return sample position and timestamp
 *  Parameters: sPos holds the position on return, reset to 0 on Start()
 *              tStamp holds the system time of sPos
 *  Return:     -1000 on missing IO
 *              -996 on missing clock
 */

DEFINE_THISCALL_WRAPPER(GetSamplePosition,12)
HIDDEN LONG STDMETHODCALLTYPE GetSamplePosition(LPWINEASIO iface, w_int64_t *sPos, w_int64_t *tStamp)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, sPos: %p, tStamp: %p\n", iface, sPos, tStamp);

    if (!sPos || !tStamp)
        return -998;

    tStamp->lo = This->host_time_stamp.lo;
    tStamp->hi = This->host_time_stamp.hi;
    sPos->lo = This->host_num_samples.lo;
    sPos->hi = 0; /* FIXME */

    return 0;
}

/*
 * LONG GetChannelInfo (void *info);
 *  Function:   Retrive channel info
 *  Returns:    -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetChannelInfo,8)
HIDDEN LONG STDMETHODCALLTYPE GetChannelInfo(LPWINEASIO iface, void *info)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    LONG *linfo = (LONG*)info;

    const LONG channelNumber = *linfo++;
    const LONG isInputType = *linfo++;

    /* TRACE("(iface: %p, info: %p\n", iface, info); */

    if (channelNumber < 0 || (isInputType ? channelNumber >= This->wineasio_number_inputs : channelNumber >= This->wineasio_number_outputs))
        return -998;

    *linfo++ = (isInputType ? This->input_channel : This->output_channel)[channelNumber].active;
    *linfo++ = 0;
    *linfo++ = 19;
    memcpy(linfo, (isInputType ? This->input_channel : This->output_channel)[channelNumber].port_name, WINEASIO_MAX_NAME_LENGTH);

    return 0;
}

/*
 * LONG CreateBuffers(BufferInformation *bufferInfo, LONG numChannels, LONG bufferSize, Callbacks *callbacks);
 *  Function:   Allocate buffers for IO channels
 *  Parameters: bufferInfo   - pointer to an array of BufferInformation structures
 *              numChannels  - the total number of IO channels to be allocated
 *              bufferSize   - one of the buffer sizes retrieved with GetBufferSize()
 *              callbacks    - pointer to a Callbacks structure
 *  Returns:    -994 if impossible to allocate enough memory
 *              -997 on unsupported bufferSize or invalid bufferInfo data
 *              -1000 on missing IO
 */

DEFINE_THISCALL_WRAPPER(CreateBuffers,20)
HIDDEN LONG STDMETHODCALLTYPE CreateBuffers(LPWINEASIO iface, BufferInformation *bufferInfo, LONG numChannels, LONG bufferSize, Callbacks *callbacks)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    BufferInformation  *bufferInfoPerChannel = bufferInfo;
    int             i, j, k;

    TRACE("iface: %p, bufferInfo: %p, numChannels: %i, bufferSize: %i, callbacks: %p\n", iface, bufferInfo, (int)numChannels, (int)bufferSize, callbacks);

    if (This->host_driver_state != Initialized)
        return -1000;

    if (!bufferInfo || !callbacks)
        return -997;

    /* Check for invalid channel numbers */
    for (i = j = k = 0; i < numChannels; i++, bufferInfoPerChannel++)
    {
        if (bufferInfoPerChannel->isInputType)
        {
            if (j++ >= This->wineasio_number_inputs)
            {
                WARN("Invalid input channel requested\n");
                return -997;
            }
        }
        else
        {
            if (k++  >= This->wineasio_number_outputs)
            {
                WARN("Invalid output channel requested\n");
                return -997;
            }
        }
    }

    /* set buf_size */
    if (This->wineasio_fixed_buffersize)
    {
        if (This->host_current_buffersize != bufferSize)
            return -997;
        TRACE("Buffersize fixed at %i\n", (int)This->host_current_buffersize);
    }
    else
    { /* fail if not a power of two and if out of range */
        if (!(bufferSize > 0 && !(bufferSize&(bufferSize-1))
                && bufferSize >= WINEASIO_MINIMUM_BUFFERSIZE
                && bufferSize <= WINEASIO_MAXIMUM_BUFFERSIZE))
        {
            WARN("Invalid buffersize %i requested\n", (int)bufferSize);
            return -997;
        }
        else
        {
            if (This->host_current_buffersize == bufferSize)
            {
                TRACE("Buffer size already set to %i\n", (int)This->host_current_buffersize);
            }
            else
            {
                This->host_current_buffersize = bufferSize;
                if (jack_set_buffer_size(This->jack_client, This->host_current_buffersize))
                {
                    WARN("JACK is unable to set buffersize to %i\n", (int)This->host_current_buffersize);
                    return -999;
                }
                TRACE("Buffer size changed to %i\n", (int)This->host_current_buffersize);
            }
        }
    }

    This->host_callbacks = callbacks;
    This->host_time_info_mode = This->host_can_time_code = FALSE;

    if (This->host_callbacks->sendNotification(7, 0, 0, 0))
    {
        This->host_time_info_mode = TRUE;
        if (This->host_callbacks->sendNotification(8, 0, 0, 0))
            This->host_can_time_code = TRUE;
    }

    /* Allocate audio buffers */

    This->callback_audio_buffer = HeapAlloc(GetProcessHeap(), 0,
        (This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->host_current_buffersize * sizeof(jack_default_audio_sample_t));
    if (!This->callback_audio_buffer)
    {
        ERR("Unable to allocate %i audio buffers\n", This->wineasio_number_inputs + This->wineasio_number_outputs);
        return -994;
    }
    TRACE("%i audio buffers allocated (%i kB)\n", This->wineasio_number_inputs + This->wineasio_number_outputs,
          (int) ((This->wineasio_number_inputs + This->wineasio_number_outputs) * 2 * This->host_current_buffersize * sizeof(jack_default_audio_sample_t) / 1024));

    for (i = 0; i < This->wineasio_number_inputs; i++)
        This->input_channel[i].audio_buffer = This->callback_audio_buffer + (i * 2 * This->host_current_buffersize);
    for (i = 0; i < This->wineasio_number_outputs; i++)
        This->output_channel[i].audio_buffer = This->callback_audio_buffer + ((This->wineasio_number_inputs + i) * 2 * This->host_current_buffersize);

    /* initialize BufferInformation structures */
    bufferInfoPerChannel = bufferInfo;
    This->host_active_inputs = This->host_active_outputs = 0;

    for (i = 0; i < This->wineasio_number_inputs; i++) {
        This->input_channel[i].active = false;
    }
    for (i = 0; i < This->wineasio_number_outputs; i++) {
        This->output_channel[i].active = false;
    }

    for (i = 0; i < numChannels; i++, bufferInfoPerChannel++)
    {
        if (bufferInfoPerChannel->isInputType)
        {
            bufferInfoPerChannel->audioBufferStart = &This->input_channel[bufferInfoPerChannel->channelNumber].audio_buffer[0];
            bufferInfoPerChannel->audioBufferEnd = &This->input_channel[bufferInfoPerChannel->channelNumber].audio_buffer[This->host_current_buffersize];
            This->input_channel[bufferInfoPerChannel->channelNumber].active = true;
            This->host_active_inputs++;
            /* TRACE("ASIO audio buffer for channel %i as input %li created\n", i, This->host_active_inputs); */
        }
        else
        {
            bufferInfoPerChannel->audioBufferStart = &This->output_channel[bufferInfoPerChannel->channelNumber].audio_buffer[0];
            bufferInfoPerChannel->audioBufferEnd = &This->output_channel[bufferInfoPerChannel->channelNumber].audio_buffer[This->host_current_buffersize];
            This->output_channel[bufferInfoPerChannel->channelNumber].active = true;
            This->host_active_outputs++;
            /* TRACE("ASIO audio buffer for channel %i as output %li created\n", i, This->host_active_outputs); */
        }
    }
    TRACE("%i audio channels initialized\n", This->host_active_inputs + This->host_active_outputs);

    if (jack_activate(This->jack_client))
        return -1000;

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
    This->host_driver_state = Prepared;
    return 0;
}

/*
 * LONG DisposeBuffers(void);
 *  Function:   Release allocated buffers
 *  Returns:    -997 if no buffers were previously allocated
 *              -1000 on missing IO
 *  Implies:    Stop()
 */

DEFINE_THISCALL_WRAPPER(DisposeBuffers,4)
HIDDEN LONG STDMETHODCALLTYPE DisposeBuffers(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    int             i;

    TRACE("iface: %p\n", iface);

    if (This->host_driver_state == Running)
        Stop (iface);
    if (This->host_driver_state != Prepared)
        return -1000;

    if (jack_deactivate(This->jack_client))
        return -1000;

    This->host_callbacks = NULL;

    for (i = 0; i < This->wineasio_number_inputs; i++)
    {
        This->input_channel[i].audio_buffer = NULL;
        This->input_channel[i].active = false;
    }
    for (i = 0; i < This->wineasio_number_outputs; i++)
    {
        This->output_channel[i].audio_buffer = NULL;
        This->output_channel[i].active = false;
    }
    This->host_active_inputs = This->host_active_outputs = 0;

    if (This->callback_audio_buffer)
        HeapFree(GetProcessHeap(), 0, This->callback_audio_buffer);

    This->host_driver_state = Initialized;
    return 0;
}

/*
 * LONG ControlPanel(void);
 *  Function:   Open a control panel for driver settings
 *  Returns:    -1000 if no control panel exists.  Actually return code should be ignored
 *  Note:       Call sendNotification if something has changed
 */

DEFINE_THISCALL_WRAPPER(ControlPanel,4)
HIDDEN LONG STDMETHODCALLTYPE ControlPanel(LPWINEASIO iface)
{
    static char arg0[] = "wineasio-settings\0";
    static char *arg_list[] = { arg0, NULL };

    TRACE("iface: %p\n", iface);

    if (vfork() == 0)
    {
        execvp (arg0, arg_list);
        _exit(1);
    }
    return 0;
}

/*
 * LONG Future(LONG selector, void *opt);
 *  Function:   Various
 *  Returns:    Depends on the selector but in general -998 on invalid selector
 *              -998 if function is unsupported to disable further calls
 *              0x3f4847a0 on success, do not use 0
 */

DEFINE_THISCALL_WRAPPER(Future,12)
HIDDEN LONG STDMETHODCALLTYPE Future(LPWINEASIO iface, LONG selector, void *opt)
{
    IWineASIOImpl           *This = (IWineASIOImpl *) iface;

    TRACE("iface: %p, selector: %i, opt: %p\n", iface, selector, opt);

    switch (selector)
    {
        case 1:
            This->host_can_time_code = TRUE;
            TRACE("The host enabled TimeCode\n");
            return 0x3f4847a0;
        case 2:
            This->host_can_time_code = FALSE;
            TRACE("The host disabled TimeCode\n");
            return 0x3f4847a0;
        case 3:
            TRACE("The driver denied request to set input monitor\n");
            return -1000;
        case 4:
            TRACE("The driver denied request for Transport control\n");
            return -998;
        case 5:
            TRACE("The driver denied request to set input gain\n");
            return -998;
        case 6:
            TRACE("The driver denied request to get input meter \n");
            return -998;
        case 7:
            TRACE("The driver denied request to set output gain\n");
            return -998;
        case 8:
            TRACE("The driver denied request to get output meter\n");
            return -998;
        case 9:
            TRACE("The driver does not support input monitor\n");
            return -998;
        case 10:
            TRACE("The driver supports TimeInfo\n");
            return 0x3f4847a0;
        case 11:
            TRACE("The driver supports TimeCode\n");
            return 0x3f4847a0;
        case 12:
            TRACE("The driver denied request for Transport\n");
            return -998;
        case 13:
            TRACE("The driver does not support input gain\n");
            return -998;
        case 14:
            TRACE("The driver does not support input meter\n");
            return -998;
        case 15:
            TRACE("The driver does not support output gain\n");
            return -998;
        case 16:
            TRACE("The driver does not support output meter\n");
            return -998;
        case 0x23111961:
            TRACE("The driver denied request to set DSD IO format\n");
            return -1000;
        case 0x23111983:
            TRACE("The driver denied request to get DSD IO format\n");
            return -1000;
        case 0x23112004:
            TRACE("The driver does not support DSD IO format\n");
            return -1000;
        default:
            TRACE("ASIOFuture() called with undocumented selector\n");
            return -998;
    }
}

/*
 * LONG OutputReady(void);
 *  Function:   Tells the driver that output bufffers are ready
 *  Returns:    0 if supported
 *              -1000 to disable
 */

DEFINE_THISCALL_WRAPPER(OutputReady,4)
HIDDEN LONG STDMETHODCALLTYPE OutputReady(LPWINEASIO iface)
{
    /* disabled to stop stand alone NI programs from spamming the console
    TRACE("iface: %p\n", iface); */
    return -1000;
}

/****************************************************************************
 *  JACK callbacks
 */

static inline int jack_buffer_size_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->host_driver_state != Running)
        return 0;

    if (This->host_callbacks->sendNotification(1, 3, 0, 0))
        This->host_callbacks->sendNotification(3, 0, 0, 0);
    return 0;
}

static inline void jack_latency_callback(jack_latency_callback_mode_t mode, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->host_driver_state != Running)
        return;

    if (This->host_callbacks->sendNotification(1, 6, 0, 0))
        This->host_callbacks->sendNotification(6, 0, 0, 0);

    return;
}

static inline int jack_process_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl               *This = (IWineASIOImpl*)arg;

    int                         i;
    jack_transport_state_t      jack_transport_state;
    jack_position_t             jack_position;
    DWORD                       time;

    /* output silence if the host callback isn't running yet */
    if (This->host_driver_state != Running)
    {
        for (i = 0; i < This->host_active_outputs; i++)
            bzero(jack_port_get_buffer(This->output_channel[i].port, nframes), sizeof (jack_default_audio_sample_t) * nframes);
        return 0;
    }

    /* copy jack to host buffers */
    for (i = 0; i < This->wineasio_number_inputs; i++)
        if (This->input_channel[i].active)
            memcpy (&This->input_channel[i].audio_buffer[nframes * This->host_buffer_index],
                    jack_port_get_buffer(This->input_channel[i].port, nframes),
                    sizeof (jack_default_audio_sample_t) * nframes);

    if (This->host_num_samples.lo > ULONG_MAX - nframes)
        This->host_num_samples.hi++;
    This->host_num_samples.lo += nframes;

    time = timeGetTime();
    This->host_time_stamp.lo = time * 1000000;
    This->host_time_stamp.hi = ((unsigned long long) time * 1000000) >> 32;

    if (This->host_time_info_mode) /* use the newer swapBuffersWithTimeInfo method if supported */
    {
        This->host_time.numSamples.lo = This->host_num_samples.lo;
        This->host_time.numSamples.hi = This->host_num_samples.hi;
        This->host_time.timeStamp.lo = This->host_time_stamp.lo;
        This->host_time.timeStamp.hi = This->host_time_stamp.hi;
        This->host_time.sampleRate = This->host_sample_rate;
        This->host_time.flags = 0x7;

        if (This->host_can_time_code) /* FIXME addionally use time code if supported */
        {
            jack_transport_state = jack_transport_query(This->jack_client, &jack_position);
            This->host_time.flagsForTimeCode = 0x1;
            if (jack_transport_state == JackTransportRolling)
                This->host_time.flagsForTimeCode |= 0x2;
        }
        This->host_callbacks->swapBuffersWithTimeInfo(&This->host_time, This->host_buffer_index, 1);
    }
    else
    { /* use the old swapBuffers method */
        This->host_callbacks->swapBuffers(This->host_buffer_index, 1);
    }

    /* copy host to jack buffers */
    for (i = 0; i < This->wineasio_number_outputs; i++)
        if (This->output_channel[i].active)
            memcpy(jack_port_get_buffer(This->output_channel[i].port, nframes),
                    &This->output_channel[i].audio_buffer[nframes * This->host_buffer_index],
                    sizeof (jack_default_audio_sample_t) * nframes);

    /* switch host buffer */
    This->host_buffer_index = This->host_buffer_index ? 0 : 1;
    return 0;
}

static inline int jack_sample_rate_callback(jack_nframes_t nframes, void *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)arg;

    if(This->host_driver_state != Running)
        return 0;

    This->host_sample_rate = nframes;
    This->host_callbacks->sampleRateChanged(nframes);
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
     * host_num_samples, host_time, & host_time_stamp are initialized in Start()
     * jack_num_input_ports & jack_num_output_ports are initialized in Init() */
    This->host_active_inputs = 0;
    This->host_active_outputs = 0;
    This->host_buffer_index = 0;
    This->host_callbacks = NULL;
    This->host_can_time_code = FALSE;
    This->host_current_buffersize = 0;
    This->host_driver_state = Loaded;
    This->host_sample_rate = 0;
    This->host_time_info_mode = FALSE;
    This->host_version = 92;

    This->wineasio_number_inputs = 16;
    This->wineasio_number_outputs = 16;
    This->wineasio_autostart_server = FALSE;
    This->wineasio_connect_to_hardware = TRUE;
    This->wineasio_fixed_buffersize = TRUE;
    This->wineasio_preferred_buffersize = WINEASIO_PREFERRED_BUFFERSIZE;

    This->jack_client = NULL;
    This->jack_client_name[0] = 0;
    This->jack_input_ports = NULL;
    This->jack_output_ports = NULL;
    This->callback_audio_buffer = NULL;
    This->input_channel = NULL;
    This->output_channel = NULL;

    /* create registry entries with defaults if not present */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, key_software_wine_wineasio, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);

    /* get/set number of wineasio inputs */
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

    /* get/set number of wineasio outputs */
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

    /* allow changing of wineasio buffer sizes */
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
    WideCharToMultiByte(CP_ACP, WC_SEPCHARS, application_name, -1, This->jack_client_name, WINEASIO_MAX_NAME_LENGTH, NULL, NULL);

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
    size = GetEnvironmentVariableA("WINEASIO_CLIENT_NAME", environment_variable, WINEASIO_MAX_NAME_LENGTH);
    if (size > 0 && size < WINEASIO_MAX_NAME_LENGTH)
        strcpy(This->jack_client_name, environment_variable);

    /* if wineasio_preferred_buffersize is not a power of two or if out of range, then set to WINEASIO_PREFERRED_BUFFERSIZE */
    if (!(This->wineasio_preferred_buffersize > 0 && !(This->wineasio_preferred_buffersize&(This->wineasio_preferred_buffersize-1))
            && This->wineasio_preferred_buffersize >= WINEASIO_MINIMUM_BUFFERSIZE
            && This->wineasio_preferred_buffersize <= WINEASIO_MAXIMUM_BUFFERSIZE))
        This->wineasio_preferred_buffersize = WINEASIO_PREFERRED_BUFFERSIZE;

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
