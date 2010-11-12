/*
 * Copyright (C) 2006 Robert Reif
 * Copyright (C) 2007 Ralf Beck
 * Copyright (C) 2007 Johnny Petrantoni
 * Copyright (C) 2007 Stephane Letz
 * Copyright (C) 2009 Joakim Hernberg
 * Copyright (C) 2010 Peter L Jones
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

#ifndef JackWASIO
#include "config.h"
#include "settings.h"
#else
#include "config.h.JackWASIO"
static const char* ENVVAR_INPORTNAMEPREFIX = "INPORTNAME";
static const char* ENVVAR_OUTPORTNAMEPREFIX = "OUTPORTNAME";
static const char* ENVVAR_INMAP = "INPORT";
static const char* ENVVAR_OUTMAP = "OUTPORT";
static const char* DEFAULT_PREFIX = "ASIO";
static const char* DEFAULT_INPORT = "Input";
static const char* DEFAULT_OUTPORT = "Output";
#endif
#include "port.h"

#include <stdio.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>

#include <wine/windows/windef.h>
#include <wine/windows/winbase.h>
#include <wine/windows/objbase.h>
#include <wine/windows/mmsystem.h>
#include <wine/windows/psapi.h>

#include <sched.h>
#include <pthread.h>
#include <semaphore.h>

#include "wine/library.h"
#include "wine/debug.h"

#ifndef JackWASIO
#include <jack/jack.h>
#include <jack/thread.h>
#include <jack/ringbuffer.h>
#else
#include <Jack/jack.h>
#include <Jack/thread.h>
#include <Jack/ringbuffer.h>
#include "pThreadUtilities.h"
#endif

#define IEEE754_64FLOAT 1
#include "asio.h"

WINE_DEFAULT_DEBUG_CHANNEL(asio);

/* JACK callback function */
static int jack_process(jack_nframes_t nframes, void * arg);

/* WIN32 callback function */
static DWORD CALLBACK win32_callback(LPVOID arg);

/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static GUID const CLSID_WineASIO = {
0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };

#define twoRaisedTo32           4294967296.0
#define twoRaisedTo32Reciprocal (1.0 / twoRaisedTo32)

/* ASIO drivers use the thiscall calling convention which only Microsoft compilers
 * produce.  These macros add an extra layer to fixup the registers properly for
 * this calling convention.
 */

#ifdef __i386__  /* thiscall functions are i386-specific */

#ifdef __GNUC__
/* GCC erroneously warns that the newly wrapped function
 * isn't used, let us help it out of its thinking
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

typedef struct _Channel {
   ASIOBool active;
   char* buffer;
   jack_ringbuffer_t *ring;
   const char  *port_name;
   jack_port_t *port;
} Channel;

typedef struct sched_param SCHED_PARAM;

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
    long                active_inputs;
    long                active_outputs;
    BOOL                time_info_mode;
    BOOL                tc_read;
    long                state;

    /* JACK stuff */
    char                *client_name;
    unsigned int        num_inputs;
    unsigned int        num_outputs;
    jack_client_t       *client;
    long                toggle;
    SCHED_PARAM         jack_client_priority;

    /* callback stuff */
    HANDLE              thread;
    HANDLE              start_event;
    HANDLE              stop_event;
    DWORD               thread_id;
    sem_t               semaphore1;
    sem_t               semaphore2;
    BOOL                terminate;

    Channel             *input;
    Channel             *output;
};

typedef struct IWineASIOImpl              IWineASIOImpl;

static ULONG WINAPI IWineASIOImpl_AddRef(LPWINEASIO iface)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    ULONG ref = InterlockedIncrement(&(This->ref));
    TRACE("(%p)\n", iface);
    TRACE("(%p) ref was %d\n", This, ref - 1);

    return ref;
}

static ULONG WINAPI IWineASIOImpl_Release(LPWINEASIO iface)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    ULONG ref = InterlockedDecrement(&(This->ref));
    int i;
    TRACE("(%p)\n", iface);
    TRACE("(%p) ref was %d\n", This, ref + 1);

    if (!ref) {
        TRACE("Entering state = Exit\n");
        This->state = Exit;

        jack_client_close(This->client);
        TRACE("JACK client closed\n");

        TRACE("Setting Win32 thread termination to TRUE\n");
        This->terminate = TRUE;
        sem_post(&This->semaphore1);

        WaitForSingleObject(This->stop_event, INFINITE);
        TRACE("Win32 thread terminated\n");
        CloseHandle(This->stop_event);
        This->stop_event = INVALID_HANDLE_VALUE;

        sem_destroy(&This->semaphore1);
        sem_destroy(&This->semaphore2);

        for (i = 0; i < This->num_inputs; i++)
        {
            jack_ringbuffer_free(This->input[i].ring);
            This->input[i].ring = NULL;
        }
        for (i = 0; i < This->num_outputs; i++)
        {
            jack_ringbuffer_free(This->output[i].ring);
            This->output[i].ring = NULL;
        }
        HeapFree(GetProcessHeap(),0,This);
        TRACE("(%p) released\n", This);
    }
    return ref;
}

static HRESULT WINAPI IWineASIOImpl_QueryInterface(LPWINEASIO iface, REFIID riid, void** ppvObject)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
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

#ifndef JackWASIO
static void read_config(IWineASIOImpl* This)
{
    char *usercfg = NULL;
    FILE *cfg;

    asprintf(&usercfg, "%s/%s", getenv("HOME"), USERCFG);
    cfg = fopen(usercfg, "r");
    if (cfg)
        TRACE("Config: %s\n", usercfg);
    else
    {
        cfg = fopen(SITECFG, "r");
        if (cfg)
            TRACE("Config: %s\n", SITECFG);
    }
    free(usercfg);

    if (cfg)
    {
        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        line = NULL;
        len = 0;
        while( (read = getline(&line, &len, cfg)) != -1)
        {
            while (isspace(line[--read])) line[read]='\0';
            if ((strstr(line, ENVVAR_INPUTS)
                || strstr(line, ENVVAR_OUTPUTS)
                || strstr(line, ENVVAR_INPORTNAMEPREFIX)
                || strstr(line, ENVVAR_OUTPORTNAMEPREFIX)
                || strstr(line, ENVVAR_INMAP)
                || strstr(line, ENVVAR_OUTMAP)
                || strstr(line, ENVVAR_AUTOCONNECT)
                || strstr(line, This->client_name) == line
                ) && strchr(line, '='))
                {
                    TRACE("(%p) env: '%s'\n", This, line);
                    putenv(line);
                }
            else
            {
                free(line);
            }
            line = NULL;
            len = 0;
        }

        fclose(cfg);
    }

    usercfg = getenv(This->client_name);
    if (usercfg != NULL) {
        free(This->client_name);
        This->client_name = strdup(usercfg);
    }
}

static int get_numChannels(IWineASIOImpl *This, const char* inout, int defval)
{
    int i = defval;
    char *envv = NULL, *envi;

    asprintf(&envv, "%s%s", This->client_name, inout);
    envi = getenv(envv);
    free(envv);
    if (envi == NULL) {
        asprintf(&envv, "%s%s", DEFAULT_PREFIX, inout);
        envi = getenv(envv);
        free(envv);
    }
    if (envi != NULL) i = atoi(envi);

    return i;
}

static BOOL get_autoconnect(IWineASIOImpl* This)
{
    char* envv = NULL, *envi;

    asprintf(&envv, "%s%s", This->client_name, ENVVAR_AUTOCONNECT);
    envi = getenv(envv);
    free(envv);
    if (envi == NULL) {
        asprintf(&envv, "%s%s", DEFAULT_PREFIX, ENVVAR_AUTOCONNECT);
        envi = getenv(envv);
        free(envv);
    }

    return (envi == NULL) ? DEFAULT_AUTOCONNECT : (strcasecmp(envi, "true") == 0);
}
#else
static int GetEXEName(DWORD dwProcessID, char* name) {
    DWORD aProcesses [1024], cbNeeded, cProcesses;
    unsigned int i;
    char szEXEName[MAX_PATH];
        
    /* Enumerate all processes */
    if(!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) return FALSE;

     Calculate how many process identifiers were returned.
    cProcesses = cbNeeded / sizeof(DWORD);

    /* Loop through all process to find the one that matches
       the one we are looking for */

    for (i = 0; i < cProcesses; i++) {
        if (aProcesses [i] == dwProcessID) {
            /* Get a handle to the process */
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
                              PROCESS_VM_READ, FALSE, dwProcessID);
        
            /* Get the process name */
            if (NULL != hProcess) {
                HMODULE hMod;
                DWORD cbNeeded;
            
                if(EnumProcessModules(hProcess, &hMod,sizeof(hMod), &cbNeeded)) {
                    int len;
                    /* Get the name of the exe file */
                    GetModuleBaseNameA(hProcess,hMod,szEXEName,sizeof(szEXEName)/sizeof(char));
                    len = strlen((char*)szEXEName) - 3; /* remove ".exe" */
                    lstrcpynA(name,(char*)szEXEName,len); 
                    name[len] = '\0';
                    return TRUE;
                 }
            }
        }    
    }

    return FALSE;
}

static int MAX_INPUTS = 2;
static int MAX_OUTPUTS = 2;
static int gAUTO_CONNECT = FALSE;
static void ReadJPPrefs() {
    char *envar;
    char path[256];
    FILE *prefFile;
    
    envar = getenv("HOME");

    sprintf(path, "%s/Library/Preferences/JAS.jpil", envar);
    if ((prefFile = fopen(path, "rt"))) {
        int nullo;
        int input, output, autoconnect, debug, default_input, default_output, default_system;
        fscanf(
                prefFile, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                &input,
                &nullo,
                &output,
                &nullo,
                &autoconnect,
                &nullo,
                &default_input, 
                &nullo,
                &default_output, 
                &nullo,
                &default_system,
                &nullo,
                &debug,
                &nullo,
                &nullo
            );

        fclose(prefFile);
                
        MAX_INPUTS = input;
        MAX_OUTPUTS = output;
        gAUTO_CONNECT = autoconnect;
    }
}
#endif

static char* get_targetname(IWineASIOImpl* This, const char* inout, int i)
{
    char* envv = NULL, *envi;

#ifndef JackWASIO
    asprintf(&envv, "%s%s%d", This->client_name, inout, i);
#else
    asprintf(&envv, "%s%d", inout, i);
#endif
    envi = getenv(envv);
    free(envv);
#ifndef JackWASIO
    if (envi == NULL) {
        asprintf(&envv, "%s%s%d", DEFAULT_PREFIX, inout, i);
        envi = getenv(envv);
        free(envv);
    }
#endif

    return envi;
}

static void set_clientname(IWineASIOImpl *This)
{
#ifndef JackWASIO
    FILE *cmd;
    char *cmdline = NULL;
    char *ptr, *line = NULL;
    size_t len = 0;

    asprintf(&cmdline, "/proc/%d/cmdline", getpid());
    cmd = fopen(cmdline, "r");
    free(cmdline);

    getline(&line, &len, cmd);
    fclose(cmd);

    ptr = line;
    while(strchr(ptr, '/') || strchr(ptr, '\\')) ++ptr;
    line = ptr;
    ptr = strcasestr(line, ".exe");
    if (ptr) {
        while (strcasestr(ptr, ".exe")) ++ptr;
        *(--ptr) = '\0';
    }

    asprintf(&This->client_name, "%s_%s", DEFAULT_PREFIX, line);
#else
    char proc_name[256];
    memset(&proc_name[0],0x0,sizeof(char)*256); /* size of char is redundant.. but dunno i like to write it:) */

    if(!GetEXEName(GetCurrentProcessId(),&proc_name[0])) {
        strcpy(&proc_name[0],"Wine_ASIO");
    }
    This->client_name = strdup(proc_name);
#endif
}

static void set_portname(IWineASIOImpl *This, const char* inout, const char* defname, int i, Channel c[])
{
    char *envv = NULL, *envi;

    asprintf(&envv, "%s_%s%d", This->client_name, inout, i);
    envi = getenv(envv);
    free(envv);
    if (envi == NULL)
    {
        envv = NULL;
        asprintf(&envv, "%s_%s%d", DEFAULT_PREFIX, inout, i);
        envi = getenv(envv);
        free(envv);
    }
    if (envi == NULL) asprintf(&envv, "%s%d", defname, i+1);
    else asprintf(&envv, "%s", envi);
    c[i].port_name = strdup(envv);
    free(envv);
}

WRAP_THISCALL( ASIOBool __stdcall, IWineASIOImpl_init, (LPWINEASIO iface, void *sysHandle))
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    jack_status_t status;
    int i;
    TRACE("(%p, %p)\n", iface, sysHandle);

    This->sample_rate = 48000.0;
    This->block_frames = 1024;
    This->input_latency = This->block_frames;
    This->output_latency = This->block_frames;
    This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);
    This->sample_position = 0;
    strcpy(This->error_message, "No Error");
    This->num_inputs = 0;
    This->num_outputs = 0;
    This->active_inputs = 0;
    This->active_outputs = 0;
    This->toggle = 0;
    This->callbacks = NULL;
    This->time_info_mode = FALSE;
    This->tc_read = FALSE;
    This->state = Init;

    set_clientname(This);

#ifndef JackWASIO
    /* uses This->client_name */
    read_config(This);
#else
    ReadJPPrefs();
#endif

    This->client = jack_client_open(This->client_name, JackNullOption, &status, NULL);
    if (This->client == NULL)
    {
        WARN("(%p) failed to open jack server\n", This);
        return ASIOFalse;
    }

    TRACE("JACK client opened, client name: '%s'; sample rate: %f\n", jack_get_client_name(This->client), This->sample_rate);

    if (status & JackServerStarted)
        TRACE("(%p) JACK server started\n", This);

    /* Thread initialisation */
    sem_init(&This->semaphore1, 0, 0);
    sem_init(&This->semaphore2, 0, 0);

    This->terminate = FALSE;
    This->jack_client_priority.sched_priority = -1;
    This->start_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    This->stop_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    This->thread = CreateThread(NULL, 0, win32_callback, (LPVOID)This, 0, &This->thread_id);
    if (This->thread)
    {
        TRACE("Wait for Win32 thread to start\n", This);
        WaitForSingleObject(This->start_event, INFINITE);
        TRACE("Win32 thread started\n", This);
        CloseHandle(This->start_event);
        This->start_event = INVALID_HANDLE_VALUE;
    }
    else
    {
        WARN("(%p) Couldn't create thread\n", This);
        return ASIOFalse;
    }

    jack_set_process_callback(This->client, jack_process, This);

    This->sample_rate = jack_get_sample_rate(This->client);
    This->block_frames = jack_get_buffer_size(This->client);

    This->miliseconds = (long)((double)(This->block_frames * 1000) / This->sample_rate);
    This->input_latency = This->block_frames;
    This->output_latency = This->block_frames;

    This->active_inputs = 0;
#ifndef JackWASIO
    This->num_inputs = get_numChannels(This, ENVVAR_INPUTS, DEFAULT_NUMINPUTS);
#else
    This->num_inputs = MAX_INPUTS;
#endif
    This->input = HeapAlloc(GetProcessHeap(), 0, sizeof(Channel) * This->num_inputs);
    if (!This->input)
    {
        MESSAGE("(%p) Not enough memory for %d input channels\n", This, This->num_inputs);
        return ASIOFalse;
    }
    TRACE("(%p) Max inputs: %d\n", This, This->num_inputs);
    for (i = 0; i < This->num_inputs; i++)
    {
        This->input[i].active = ASIOFalse;
        This->input[i].buffer = NULL;
        set_portname(This, ENVVAR_INPORTNAMEPREFIX, DEFAULT_INPORT, i, This->input);
        TRACE("(%p) input %d: '%s'\n", This, i, This->input[i].port_name);

        This->input[i].port = jack_port_register(This->client,
            This->input[i].port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (This->input[i].port)
            TRACE("(%p) Registered input port %i: '%s' (%p)\n", This, i, This->input[i].port_name, This->input[i].port);
        else {
            MESSAGE("(%p) Failed to register input port %i ('%s')\n", This, i, This->input[i].port_name);
            return ASE_NotPresent;
        }
        This->input[i].ring = NULL;
        This->input[i].ring = jack_ringbuffer_create(4 * This->block_frames * sizeof(float));
        if (!This->input[i].ring)
        {
            WARN("no input ringbuffer memory\n");
            return ASE_NotPresent;
        }
    }

    This->active_outputs = 0;
#ifndef JackWASIO
    This->num_outputs = get_numChannels(This, ENVVAR_OUTPUTS, DEFAULT_NUMOUTPUTS);
#else
    This->num_outputs = MAX_OUTPUTS;
#endif
    This->output =  HeapAlloc(GetProcessHeap(), 0, sizeof(Channel) * This->num_outputs);
    if (!This->output)
    {
        MESSAGE("(%p) Not enough memory for %d output channels\n", This, This->num_outputs);
        return ASIOFalse;
    }
    TRACE("(%p) Max outputs: %d\n", This, This->num_outputs);
    for (i = 0; i < This->num_outputs; i++)
    {
        This->output[i].active = ASIOFalse;
        This->output[i].buffer = NULL;
        set_portname(This, ENVVAR_OUTPORTNAMEPREFIX, DEFAULT_OUTPORT, i, This->output);
        TRACE("(%p) output %d: '%s'\n", This, i, This->output[i].port_name);

        This->output[i].port = jack_port_register(This->client, 
            This->output[i].port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (This->output[i].port)
            TRACE("(%p) Registered output port %i: '%s' (%p)\n", This, i, This->output[i].port_name, This->output[i].port);
        else {
            MESSAGE("(%p) Failed to register output port %i ('%s')\n", This, i, This->output[i].port_name);
            return ASE_NotPresent;
        }
        This->output[i].ring = NULL;
        This->output[i].ring = jack_ringbuffer_create(4 * This->block_frames * sizeof(float));
        if (!This->output[i].ring)
        {
            WARN("no output ringbuffer memory\n");
            return ASE_NotPresent;
        }
    }

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
    return 81; /* 0.8.1.x */
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
#ifndef JackWASIO
    BOOL autoconnect = get_autoconnect(This);
#else
    BOOL autoconnect = gAUTO_CONNECT;
#endif
    char *envi;
    const char ** ports;
    int numports;
    int i, j;
    TRACE("(%p)\n", iface);

    if (This->callbacks)
    {
        This->sample_position = 0;
        This->system_time.lo = 0;
        This->system_time.hi = 0;

        if (jack_activate(This->client))
        {
            WARN("couldn't activate client\n");
            return ASE_NotPresent;
        }

        /* get maximum reccomended client priority from JACK */
        This->jack_client_priority.sched_priority = jack_client_real_time_priority(This->client);

        /* Win32 thread should now be able to query jack for priority */
        sem_post(&This->semaphore1);

        /* get list of port names */
        ports = jack_get_ports(This->client, NULL, NULL, JackPortIsPhysical | JackPortIsOutput);
        for(numports = 0; ports && ports[numports]; numports++);
        TRACE("(%p) inputs desired: %d; JACK outputs: %d\n", This, This->num_inputs, numports);

        for (i = j = 0; i < This->num_inputs; i++)
        {
            if (This->input[i].active != ASIOTrue)
                continue;

            if (autoconnect) {
                /* Get the desired JACK output (source) name, if there is one, for this ASIO input */
                envi = get_targetname(This, ENVVAR_INMAP, i);
                envi = envi ? envi : j < numports ? ports[j++] : NULL;
                if (!envi) continue;

                TRACE("(%p) %d: Connect JACK output '%s' to my input '%s'\n", This, i
                    ,envi
                    ,jack_port_name(This->input[i].port)
                    );
                if (jack_connect(This->client
                    ,envi
                    ,jack_port_name(This->input[i].port)
                   ))
                {
                    MESSAGE("(%p) Connect failed\n", This);
                }
            }
        }
        if (ports)
            free(ports);

        /* get list of port names */
        ports = jack_get_ports(This->client, NULL, NULL, JackPortIsPhysical | JackPortIsInput);
        for(numports = 0; ports && ports[numports]; numports++);
        TRACE("(%p) JACK inputs: %d; outputs desired: %d\n", This, numports, This->num_outputs);

        for (i = j = 0; i < This->num_outputs; i++)
        {
            if (This->output[i].active != ASIOTrue)
                continue;

            if (autoconnect) {
                /* Get the desired JACK input (target) name, if there is one, for this ASIO output */
                envi = get_targetname(This, ENVVAR_OUTMAP, i);
                envi = envi ? envi : j < numports ? ports[j++] : NULL;

                TRACE("(%p) %d: Connect my output '%s' to JACK input '%s'\n", This, i
                    ,jack_port_name(This->output[i].port)
                    ,envi
                    );
                if (jack_connect(This->client
                    ,jack_port_name(This->output[i].port)
                    ,envi
                   ))
                {
                    MESSAGE("(%p) Connect failed\n", This);
                }
            }
        }
        if (ports)
            free(ports);

        TRACE("Entering state = Run\n");
        This->state = Run;

        return ASE_OK;
    }

    return ASE_NotPresent;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_stop, (LPWINEASIO iface))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p)\n", iface);

    if (jack_deactivate(This->client))
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

    TRACE("inputs: %d outputs: %d\n", This->num_inputs, This->num_outputs);

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

    TRACE("min: %ld max: %ld preferred: %ld granularity: 0\n", This->block_frames, This->block_frames, This->block_frames);

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

    TRACE("rate: %f\n", This->sample_rate);

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_setSampleRate, (LPWINEASIO iface, ASIOSampleRate sampleRate))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p, %f)\n", iface, sampleRate);

    if (sampleRate != This->sample_rate)
        return ASE_NoClock;

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
    TRACE("(%p, %p)\n", iface, info);

    if (info->channel < 0 || (info->isInput ? info->channel >= This->num_inputs : info->channel >= This->num_outputs))
        return ASE_InvalidParameter;

    info->type = ASIOSTFloat32LSB; /* info->type = This->sample_type; */
    info->channelGroup = 0;

    if (info->isInput)
    {
        info->isActive = This->input[info->channel].active;
#ifndef JackWASIO
        strcpy(info->name, This->input[info->channel].port_name);
#else
        asprintf(&info->name, "Input %ld", info->channel);
#endif
    }
    else
    {
        info->isActive = This->output[info->channel].active;
#ifndef JackWASIO
        strcpy(info->name, This->output[info->channel].port_name);
#else
        asprintf(&info->name, "Output %ld", info->channel);
#endif
    }

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_disposeBuffers, (LPWINEASIO iface))
{
    int i;
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    TRACE("(%p)\n", iface);

    This->callbacks = NULL;
    __wrapped_IWineASIOImpl_stop(iface);

    for (i = This->active_inputs; --i >= 0; )
    {
        if (This->input[i].active)
        {
            HeapFree(GetProcessHeap(), 0, This->input[i].buffer);
            This->input[i].buffer = NULL;
            This->input[i].active = ASIOFalse;
        }
        This->active_inputs--;
    }

    for (i = This->active_outputs; --i >= 0; )
    {
        if (This->output[i].active)
        {
            HeapFree(GetProcessHeap(), 0, This->output[i].buffer);
            This->output[i].buffer = NULL;
            This->output[i].active = ASIOFalse;
        }
        This->active_outputs--;
    }

    return ASE_OK;
}

WRAP_THISCALL( ASIOError __stdcall, IWineASIOImpl_createBuffers, (LPWINEASIO iface, ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks))
{
    IWineASIOImpl * This = (IWineASIOImpl*)iface;
    ASIOBufferInfo * info = bufferInfos;
    int i, j;
    TRACE("(%p, %p, %ld, %ld, %p)\n", iface, bufferInfos, numChannels, bufferSize, callbacks);

    /* Just to be on the safe side: */
    This->active_inputs = 0;
    for(i = 0; i < This->num_inputs; i++) This->input[i].active = ASIOFalse;
    This->active_outputs = 0;
    for(i = 0; i < This->num_outputs; i++) This->output[i].active = ASIOFalse;

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

            This->input[This->active_inputs].buffer = HeapAlloc(GetProcessHeap(), 0, 2 * This->block_frames * sizeof(float)); /* ASIOSTFloat32LSB support only */
            if (This->input[This->active_inputs].buffer)
            {
                info->buffers[0] = &This->input[This->active_inputs].buffer[0];
                info->buffers[1] = &This->input[This->active_inputs].buffer[This->block_frames];
                for (j = 0; j < This->block_frames * 2; j++)
                    This->input[This->active_inputs].buffer[j] = 0;
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, This->input[This->active_inputs].buffer);
                info->buffers[0] = 0;
                info->buffers[1] = 0;
                WARN("no input buffer memory\n");
                goto ERROR_MEM;
            }
            This->input[This->active_inputs].active = ASIOTrue;
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

            This->output[This->active_outputs].buffer = HeapAlloc(GetProcessHeap(), 0, 2 * This->block_frames * sizeof(float)); /* ASIOSTFloat32LSB support only */
            if (This->output[This->active_outputs].buffer)
            {
                info->buffers[0] = &This->output[This->active_outputs].buffer[0];
                info->buffers[1] = &This->output[This->active_outputs].buffer[This->block_frames];
                for (j = 0; j < This->block_frames * 2; j++)
                    This->output[This->active_outputs].buffer[j] = 0;
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, This->output[This->active_inputs].buffer);
                info->buffers[0] = 0;
                info->buffers[1] = 0;
                WARN("no input buffer memory\n");
                goto ERROR_MEM;
            }
            This->output[This->active_outputs].active = ASIOTrue;
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
    char* arg_list[] = { "qjackctl", NULL };

    TRACE ("Opening ASIO control panel\n");

    if (fork() == 0)
        execvp (arg_list[0], arg_list);
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

HRESULT asioCreateInstance(REFIID riid, LPVOID *ppobj)
{
    IWineASIOImpl * pobj;
    TRACE("(%s, %p)\n", debugstr_guid(riid), ppobj);

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

/*
 * The ASIO callback can make WIN32 calls which require a WIN32 thread.
 * Do the callback in this thread and then switch back to the Jack callback thread.
 */
static DWORD CALLBACK win32_callback(LPVOID arg)
{
    IWineASIOImpl * This = (IWineASIOImpl*)arg;
    TRACE("(%p) Win32 thread starting\n", arg);

    /* let IWineASIO_Init know we are alive */
    SetEvent(This->start_event);

    /* wait for JACK client to be connected and set sched_priority */
    sem_wait(&This->semaphore1);
    /* set the priority of the win32 callback thread as suggested by JACK */
#ifndef JackWASIO
    if (This->jack_client_priority.sched_priority != -1)   /* skip if not running realtime */
    {
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &This->jack_client_priority) == 0)
            TRACE ("win32 callback set to SCHED_FIFO priority %d\n", This->jack_client_priority.sched_priority);
        else
            TRACE ("Error trying to set realtime priority of win32 callback\n");
    }
    else
            TRACE ("Unable to set realtime priority of win32 callback, not running with realtime priority\n");
#else 
    setThreadToPriority(pthread_self(),96,TRUE,10000000);
#endif

    while (1)
    {
        /* wait to be woken up by the JACK callback thread */
        sem_wait(&This->semaphore1);

        if (This->terminate)
        {
            TRACE("Win32 thread terminating\n");
            SetEvent(This->stop_event);
            return 0;
        }

        if (This->state != Run)
            return 0;

        if (This->time_info_mode)
        {
            getNanoSeconds(&This->system_time);
            This->sample_position += This->block_frames;

            __wrapped_IWineASIOImpl_getSamplePosition((LPWINEASIO)This,
                &This->asio_time.timeInfo.samplePosition, &This->asio_time.timeInfo.systemTime);
            if (This->tc_read)
            {
                This->asio_time.timeCode.timeCodeSamples.lo = This->asio_time.timeInfo.samplePosition.lo;
                This->asio_time.timeCode.timeCodeSamples.hi = 0;
            }
            This->callbacks->bufferSwitchTimeInfo(&This->asio_time, This->toggle, ASIOTrue);
            This->asio_time.timeInfo.flags &= ~(kSampleRateChanged | kClockSourceChanged);
        }
        else
            This->callbacks->bufferSwitch(This->toggle, ASIOTrue);

        /* let the JACK thread know we are done */
        sem_post(&This->semaphore2);
    }

    return 0;
}

static int jack_process(jack_nframes_t nframes, void * arg)
{
    IWineASIOImpl * This = (IWineASIOImpl*)arg;
    int i;
/*
    jack_position_t transport;
    jack_transport_state ts = jack_transport_query(This->client, &transport);
    if (ts == JackTransportRolling)
        This->sample_position = transport.frame;
    else
 */
        This->sample_position += nframes;

    /* copy the JACK date to ASIO */
    for (i = 0; i < This->active_inputs; i++)
        if (This->input[i].active == ASIOTrue)
            memcpy(
                &This->input[i].buffer[This->block_frames * This->toggle], /* dest: ASIO */
                jack_port_get_buffer(This->input[i].port, nframes), /* src: JACK */
                This->block_frames * sizeof(float));

    /* wake up the WIN32 thread so it can do its callback */
    sem_post(&This->semaphore1);

    /* wait for the WIN32 thread to complete before continuing */
    sem_wait(&This->semaphore2);

    /* copy the ASIO data to JACK */
    for (i = 0; i < This->num_outputs; i++)
        if (This->output[i].active == ASIOTrue)
            memcpy(
                jack_port_get_buffer(This->output[i].port, nframes), /* dest: JACK */
                &This->output[i].buffer[This->block_frames * This->toggle], /* src: ASIO */
                This->block_frames * sizeof(float)
            );

    This->toggle = This->toggle ? 0 : 1;

    return 0;
}
