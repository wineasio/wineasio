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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "objbase.h"
#include "unknwn.h"

#ifdef DEBUG
#include "wine/debug.h"
#endif
/* WINE_DEFAULT_DEBUG_CHANNEL(asio); */

/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static GUID const CLSID_WineASIO = {
0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };

typedef struct {
    const IClassFactoryVtbl * lpVtbl;
    LONG ref;
} IClassFactoryImpl;

extern HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj);

/*******************************************************************************
 * ClassFactory
 */

static HRESULT WINAPI CF_QueryInterface(LPCLASSFACTORY iface, REFIID riid, LPVOID *ppobj)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    FIXME("(%p, %s, %p) stub!\n", This, debugstr_guid(riid), ppobj); */
    if (ppobj == NULL)
        return E_POINTER;
    return E_NOINTERFACE;
}

static ULONG WINAPI CF_AddRef(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    ULONG ref = InterlockedIncrement(&(This->ref));
    /* TRACE("iface: %p, ref has been set to %x\n", This, ref); */
    return ref;
}

static ULONG WINAPI CF_Release(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    ULONG ref = InterlockedDecrement(&(This->ref));
    /* TRACE("iface %p, ref has been set to %x\n", This, ref); */
    /* static class, won't be freed */
    return ref;
}

static HRESULT WINAPI CF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    TRACE("iface: %p, pOuter: %p, riid: %s, ppobj: %p)\n", This, pOuter, debugstr_guid(riid), ppobj); */

    if (pOuter)
        return CLASS_E_NOAGGREGATION;

    if (ppobj == NULL) {
        /* WARN("invalid parameter\n"); */
        return E_INVALIDARG;
    }

    *ppobj = NULL;
    /* TRACE("Creating the WineASIO object\n"); */
    return WineASIOCreateInstance(riid, ppobj);
}

static HRESULT WINAPI CF_LockServer(LPCLASSFACTORY iface, BOOL dolock)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    FIXME("iface: %p, dolock: %d) stub!\n", This, dolock); */
    return S_OK;
}

static const IClassFactoryVtbl CF_Vtbl = {
    CF_QueryInterface,
    CF_AddRef,
    CF_Release,
    CF_CreateInstance,
    CF_LockServer
};

static IClassFactoryImpl WINEASIO_CF = { &CF_Vtbl, 1 };

/*******************************************************************************
 * DllGetClassObject [DSOUND.@]
 * Retrieves class object from a DLL object
 *
 * NOTES
 *    Docs say returns STDAPI
 *
 * PARAMS
 *    rclsid [I] CLSID for the class object
 *    riid   [I] Reference to identifier of interface for class object
 *    ppv    [O] Address of variable to receive interface pointer for riid
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: CLASS_E_CLASSNOTAVAILABLE, E_OUTOFMEMORY, E_INVALIDARG,
 *             E_UNEXPECTED
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    /* TRACE("rclsid: %s, riid: %s, ppv: %p)\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv); */

    if (ppv == NULL) {
        /* WARN("invalid parameter\n"); */
        return E_INVALIDARG;
    }

    *ppv = NULL;

    if (!IsEqualIID(riid, &IID_IClassFactory) && !IsEqualIID(riid, &IID_IUnknown))
    {
        /* WARN("no interface for %s\n", debugstr_guid(riid)); */
        return E_NOINTERFACE;
    }

    if (IsEqualGUID(rclsid, &CLSID_WineASIO))
    {
        CF_AddRef((IClassFactory*) &WINEASIO_CF);
        *ppv = &WINEASIO_CF;
        return S_OK;
    }

    /* WARN("rclsid: %s, riid: %s, ppv: %p): no class found.\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv); */
    return CLASS_E_CLASSNOTAVAILABLE;
}


/*******************************************************************************
 * DllCanUnloadNow
 * Determines whether the DLL is in use.
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: S_FALSE
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    /* FIXME("(void): stub\n"); */
    return S_FALSE;
}

/***********************************************************************
 *           DllMain (ASIO.init)
 */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    /* TRACE("hInstDLL: %p, fdwReason: %x lpvReserved: %p)\n", hInstDLL, fdwReason, lpvReserved); */

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
/*        TRACE("DLL_PROCESS_ATTACH\n"); */
        break;
    case DLL_PROCESS_DETACH:
/*        TRACE("DLL_PROCESS_DETACH\n"); */
        break;
    case DLL_THREAD_ATTACH:
/*        TRACE("DLL_THREAD_ATTACH\n"); */
        break;
    case DLL_THREAD_DETACH:
/*        TRACE("DLL_THREAD_DETACH\n"); */
        break;
    default:
/*        TRACE("UNKNOWN REASON\n"); */
        break;
    }
    return TRUE;
}
