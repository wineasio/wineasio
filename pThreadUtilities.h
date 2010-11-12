/*
	Copyright:	© Copyright 2002 Apple Computer, Inc. All rights
			reserved.

	Disclaimer:	IMPORTANT:  This Apple software is supplied to
			you by Apple Computer, Inc.  ("Apple") in
			consideration of your agreement to the
			following terms, and your use, installation,
			modification or redistribution of this Apple
			software constitutes acceptance of these
			terms.  If you do not agree with these terms,
			please do not use, install, modify or
			redistribute this Apple software.

			In consideration of your agreement to abide by
			the following terms, and subject to these
			terms, Apple grants you a personal,
			non-exclusive license, under Apple’s
			copyrights in this original Apple software
			(the "Apple Software"), to use, reproduce,
			modify and redistribute the Apple Software,
			with or without modifications, in source
			and/or binary forms; provided that if you
			redistribute the Apple Software in its
			entirety and without modifications, you must
			retain this notice and the following text and
			disclaimers in all such redistributions of the
			Apple Software.  Neither the name, trademarks,
			service marks or logos of Apple Computer,
			Inc. may be used to endorse or promote
			products derived from the Apple Software
			without specific prior written permission from
			Apple.  Except as expressly stated in this
			notice, no other rights or licenses, express
			or implied, are granted by Apple herein,
			including but not limited to any patent rights
			that may be infringed by your derivative works
			or by other works in which the Apple Software
			may be incorporated.

			The Apple Software is provided by Apple on an
			"AS IS" basis.  APPLE MAKES NO WARRANTIES,
			EXPRESS OR IMPLIED, INCLUDING WITHOUT
			LIMITATION THE IMPLIED WARRANTIES OF
			NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
			FOR A PARTICULAR PURPOSE, REGARDING THE APPLE
			SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
			COMBINATION WITH YOUR PRODUCTS.

			IN NO EVENT SHALL APPLE BE LIABLE FOR ANY
			SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL
			DAMAGES (INCLUDING, BUT NOT LIMITED TO,
			PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
			LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
			INTERRUPTION) ARISING IN ANY WAY OUT OF THE
			USE, REPRODUCTION, MODIFICATION AND/OR
			DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER
			CAUSED AND WHETHER UNDER THEORY OF CONTRACT,
			TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY
			OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED
			OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/* pThreadUtilities.h */

#ifndef __PTHREADUTILITIES_H__
#define __PTHREADUTILITIES_H__

#import "pthread.h"
#import <CoreServices/../Frameworks/CarbonCore.framework/Headers/MacTypes.h>

#define THREAD_SET_PRIORITY			0
#define THREAD_SCHEDULED_PRIORITY		1

#include <mach/mach_error.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <CoreAudio/HostTime.h>

static inline UInt32
_getThreadPriority(pthread_t inThread, int inWhichPriority)
{
    thread_basic_info_data_t	threadInfo;
    policy_info_data_t		thePolicyInfo;
    unsigned int		count;
    
    // get basic info
    count = THREAD_BASIC_INFO_COUNT;
    thread_info (pthread_mach_thread_np (inThread), THREAD_BASIC_INFO,
		 (thread_info_t)&threadInfo, &count);

    switch (threadInfo.policy) {
        case POLICY_TIMESHARE:
            count = POLICY_TIMESHARE_INFO_COUNT;
            thread_info(pthread_mach_thread_np (inThread),
			THREAD_SCHED_TIMESHARE_INFO,
			(thread_info_t)&(thePolicyInfo.ts), &count);
            if (inWhichPriority == THREAD_SCHEDULED_PRIORITY) {
                return thePolicyInfo.ts.cur_priority;
            } else {
                return thePolicyInfo.ts.base_priority;
            }
        break;
        
        case POLICY_FIFO:
            count = POLICY_FIFO_INFO_COUNT;
            thread_info(pthread_mach_thread_np (inThread),
			THREAD_SCHED_FIFO_INFO,
			(thread_info_t)&(thePolicyInfo.fifo), &count);
            if ( (thePolicyInfo.fifo.depressed)
		 && (inWhichPriority == THREAD_SCHEDULED_PRIORITY) ) {
                return thePolicyInfo.fifo.depress_priority;
            }
            return thePolicyInfo.fifo.base_priority;
        break;
        
        case POLICY_RR:
            count = POLICY_RR_INFO_COUNT;
            thread_info(pthread_mach_thread_np (inThread),
			THREAD_SCHED_RR_INFO,
			(thread_info_t)&(thePolicyInfo.rr), &count);
            if ( (thePolicyInfo.rr.depressed)
		 && (inWhichPriority == THREAD_SCHEDULED_PRIORITY) ) {
                return thePolicyInfo.rr.depress_priority;
            }
            return thePolicyInfo.rr.base_priority;
        break;
    }
    
    return 0;
}

// returns the thread's priority as it was last set by the API
static inline UInt32
getThreadSetPriority(pthread_t inThread)
{
    return _getThreadPriority (inThread, THREAD_SET_PRIORITY);
}

// returns the thread's priority as it was last scheduled by the Kernel
static inline UInt32
getThreadScheduledPriority(pthread_t inThread)
{
    return _getThreadPriority (inThread, THREAD_SCHEDULED_PRIORITY);
}


static inline void
setThreadToPriority(pthread_t inThread, UInt32 inPriority, Boolean inIsFixed,
		    UInt64 inHALIOProcCycleDurationInNanoseconds)
{
	if (inPriority == 96)
	{
            // REAL-TIME / TIME-CONSTRAINT THREAD
            thread_time_constraint_policy_data_t	theTCPolicy;
            UInt64					theComputeQuanta;
            UInt64					thePeriod;
            UInt64					thePeriodNanos;
		
            thePeriodNanos = inHALIOProcCycleDurationInNanoseconds;
            theComputeQuanta = AudioConvertNanosToHostTime ( thePeriodNanos * 0.15 );
            thePeriod = AudioConvertNanosToHostTime (thePeriodNanos);
        
            theTCPolicy.period = thePeriod;
            theTCPolicy.computation = theComputeQuanta;
            theTCPolicy.constraint = thePeriod;
            theTCPolicy.preemptible = true;
            thread_policy_set (pthread_mach_thread_np(inThread),
			       THREAD_TIME_CONSTRAINT_POLICY,
			       (thread_policy_t)&theTCPolicy,
			       THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	} else {
            // OTHER THREADS
            thread_extended_policy_data_t		theFixedPolicy;
            thread_precedence_policy_data_t		thePrecedencePolicy;
            SInt32					relativePriority;
        
            // [1] SET FIXED / NOT FIXED
            theFixedPolicy.timeshare = !inIsFixed;
            thread_policy_set (pthread_mach_thread_np(inThread),
			       THREAD_EXTENDED_POLICY,
			       (thread_policy_t)&theFixedPolicy,
			       THREAD_EXTENDED_POLICY_COUNT);
        
            // [2] SET PRECEDENCE N.B.: We expect that if thread A
            // created thread B, and the program wishes to change the
            // priority of thread B, then the call to change the
            // priority of thread B must be made by thread A.  This
            // assumption allows us to use pthread_self() to correctly
            // calculate the priority of the feeder thread (since
            // precedency policy's importance is relative to the
            // spawning thread's priority.)
            relativePriority = inPriority -
		    getThreadSetPriority (pthread_self());
        
            thePrecedencePolicy.importance = relativePriority;
            thread_policy_set (pthread_mach_thread_np(inThread),
			       THREAD_PRECEDENCE_POLICY,
			       (thread_policy_t)&thePrecedencePolicy,
			       THREAD_PRECEDENCE_POLICY_COUNT);
	}
}

#endif	/* __PTHREADUTILITIES_H__ */
