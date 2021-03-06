/*---------------------------------------------------------------
 * Copyright (c) 1999,2000,2001,2002,2003
 * The Board of Trustees of the University of Illinois
 * All Rights Reserved.
 *---------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software (Iperf) and associated
 * documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 *
 * Redistributions of source code must retain the above
 * copyright notice, this list of conditions and
 * the following disclaimers.
 *
 *
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimers in the documentation and/or other materials
 * provided with the distribution.
 *
 *
 * Neither the names of the University of Illinois, NCSA,
 * nor the names of its contributors may be used to endorse
 * or promote products derived from this Software without
 * specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTIBUTORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ________________________________________________________________
 * National Laboratory for Applied Network Research
 * National Center for Supercomputing Applications
 * University of Illinois at Urbana-Champaign
 * http://www.ncsa.uiuc.edu
 * ________________________________________________________________
 *
 * Client.cpp
 * by Mark Gates <mgates@nlanr.net>
 * -------------------------------------------------------------------
 * A client thread initiates a connect to the server and handles
 * sending and receiving data, then closes the socket.
 * ------------------------------------------------------------------- */

#include <time.h>
#include "headers.h"
#include "Client.hpp"
#include "Thread.h"
#include "SocketAddr.h"
#include "PerfSocket.hpp"
#include "Extractor.h"
#include "delay.h"
#include "util.h"
#include "Locale.h"
#include "isochronous.hpp"
#include "pdfs.h"
#include "version.h"
#include "List.h"
#include "payloads.h"

// const double kSecs_to_usecs = 1e6;
const double kSecs_to_nsecs = 1e9;
const int    kBytes_to_Bits = 8;

#define VARYLOAD_PERIOD 0.1 // recompute the variable load every n seconds
#define MAXUDPBUF 1470

#ifndef INITIAL_PACKETID
# define INITIAL_PACKETID 0
#endif

Client::Client( thread_Settings *inSettings ) {
#ifdef HAVE_THREAD_DEBUG
  thread_debug("Client thread started in constructor (%x/%x)", inSettings->flags, inSettings->flags_extend);
#endif

    mSettings = inSettings;
    mBuf = NULL;
    myJob = NULL;
    mySocket = isServerReverse(inSettings) ? inSettings->mSock : INVALID_SOCKET;
    double ct = -1.0;
    connected = isServerReverse(mSettings);

    if (isCompat(inSettings) && isPeerVerDetect(inSettings)) {
	fprintf(stderr, "%s", warn_compat_and_peer_exchange);
	unsetPeerVerDetect(inSettings);
    }
    if (isUDP(inSettings) && !isCompat(inSettings)) {
	if ((isPeerVerDetect(inSettings) || (inSettings->mMode != kTest_Normal)) && (inSettings->mBufLen < SIZEOF_UDPHDRMSG)) {
	    mSettings->mBufLen = SIZEOF_UDPHDRMSG;
	    fprintf( stderr, warn_buffer_too_small, "Client", mSettings->mBufLen);
	} else if (mSettings->mBufLen < (int) sizeof( UDP_datagram ) ) {
	    mSettings->mBufLen = sizeof( UDP_datagram );
	    fprintf( stderr, warn_buffer_too_small, "Client", mSettings->mBufLen );
	}
    } else {
	if ((isPeerVerDetect(inSettings) || (inSettings->mMode != kTest_Normal)) && (inSettings->mBufLen < SIZEOF_TCPHDRMSG)) {
	    mSettings->mBufLen = SIZEOF_TCPHDRMSG;
	    fprintf( stderr, warn_buffer_too_small, "Client", mSettings->mBufLen);
	}
    }
    // initialize buffer
    if (isTripTime(mSettings) && (mSettings->mBufLen < (int) (sizeof(struct TCP_datagram)))) {
        mSettings->mBufLen = sizeof(struct TCP_datagram);
        fprintf( stderr, warn_buffer_too_small, "Client", mSettings->mBufLen);
    }
    mBuf = new char[((mSettings->mBufLen > MAXUDPBUF) ? mSettings->mBufLen : MAXUDPBUF)];
    FAIL_errno( mBuf == NULL, "No memory for buffer\n", mSettings );
    pattern( mBuf, ((mSettings->mBufLen > MAXUDPBUF) ? mSettings->mBufLen : MAXUDPBUF));
    if ( isFileInput( mSettings ) ) {
        if ( !isSTDIN( mSettings ) )
            Extractor_Initialize( mSettings->mFileName, mSettings->mBufLen, mSettings );
        else
            Extractor_InitializeFile( stdin, mSettings->mBufLen, mSettings );

        if ( !Extractor_canRead( mSettings ) ) {
            unsetFileInput( mSettings );
        }
    }
    framecounter = NULL;
    if (isIsochronous(mSettings)) {
	FAIL_errno( !(mSettings->mFPS > 0.0), "Invalid value for frames per second in the isochronous settings\n", mSettings );
    }

    // ServerReverse traffic threads don't need a new connect()
    // as they use the session created by the client's connect()
    if (!isServerReverse(mSettings)) {
	// let the reporter thread go first in the case of -P greater than 1
        Condition_Lock(reporter_state.await);
	while (!reporter_state.ready) {
	    Condition_TimedWait(&reporter_state.await, 1);
	}
        Condition_Unlock(reporter_state.await);
	ct = Connect( );
    }

    if (isConnected()) {
	//  Tests that don't pass packet stats to the reporter thread
	//  are Connect only or Reverse only
	if (isConnectOnly(mSettings) || (isReverse(mSettings) && !isBidir(mSettings))) {
	    if (!mSettings->reporthdr) {
		InitConnectionReport(mSettings);
		if (mSettings->reporthdr) {
		    mSettings->reporthdr->report.connection.connecttime = ct;
		}
	    }
	    // Post the settings report, the connection report will be posted later
	    if (isReport(mSettings)) {
		struct ReportHeader *tmp = ReportSettings(mSettings);
		UpdateConnectionReport(mSettings, tmp);
		// Post a settings report now
		PostReport(tmp);
	    }
	    if (mSettings->reporthdr && isConnectionReport(mSettings))
		// post the connection report
		PostReport(mSettings->reporthdr);
	    // printf("posted reports\n");
	} else {
	    InitReport(mSettings);
	    // Squirrel this away so the destructor can free the memory
	    // even when mSettings has already destroyed
	    myJob = mSettings->reporthdr;
	    // Initialize things for the packet ring and the connec time
	    if (mSettings->reporthdr) {
		mSettings->reporthdr->report.connection.connecttime = ct;
		reportstruct = &mSettings->reporthdr->packetring->metapacket;
		reportstruct->packetID = (isPeerVerDetect(mSettings)) ? 1 : INITIAL_PACKETID;
		reportstruct->errwrite=WriteNoErr;
		reportstruct->emptyreport=0;
		reportstruct->socket = mSettings->mSock;
		reportstruct->packetLen = 0;
	    }
	    if (mSettings->reporthdr) {
		mSettings->reporthdr->report.connection.connecttime = ct;
	    }
	    // Post a settings report
	    if (isReport(mSettings)) {
		struct ReportHeader *tmp = ReportSettings(mSettings);
		UpdateConnectionReport(mSettings, tmp);
		PostReport(tmp);
	    }
	    // Finally, post this thread's "job report" which the reporter thread
	    // will continuously process as long as there are packets flowing
	    if (myJob && isDataReport(mSettings))
		PostReport(myJob);
	}
    } else if (isReport(mSettings)) {
        struct ReportHeader *tmp = ReportSettings(mSettings);
	// Post a settings report on a failed connect
	PostReport(tmp);
    }
} // end Client

/* -------------------------------------------------------------------
 * Destructor
 * ------------------------------------------------------------------- */
Client::~Client() {
#if HAVE_THREAD_DEBUG
    thread_debug("Client destructor sock=%d report=%p server-reverse=%s bidir=%s", \
		 mySocket, (void *) mSettings->reporthdr, \
		 (isServerReverse(mSettings) ? "true" : "false"), (isBidir(mSettings) ? "true" : "false"));
#endif
    if ((!isBidir(mSettings) || (myJob && !myJob->bidirreport)) && (mySocket != INVALID_SOCKET)) {
        int rc = close( mySocket );
	WARN_errno( rc == SOCKET_ERROR, "client close" );
	mySocket = INVALID_SOCKET;
    }
    if (isServerReverse(mSettings))
	Iperf_delete( &(mSettings->peer), &clients );

    DELETE_ARRAY(mBuf);
} // end ~Client


/* -------------------------------------------------------------------
 * Setup a socket connected to a server.
 * If inLocalhost is not null, bind to that address, specifying
 * which outgoing interface to use.
 * ------------------------------------------------------------------- */
double Client::Connect() {
    int rc;
    double connecttime = -1.0;

    SockAddr_remoteAddr( mSettings );

    // create an internet socket
    int type = ( isUDP( mSettings )  ?  SOCK_DGRAM : SOCK_STREAM);

    int domain = (SockAddr_isIPv6( &mSettings->peer ) ?
#ifdef HAVE_IPV6
                  AF_INET6
#else
                  AF_INET
#endif
                  : AF_INET);

    mSettings->mSock = socket( domain, type, 0 );
    WARN_errno( mSettings->mSock == INVALID_SOCKET, "socket" );
    // Socket is carried both by the object and the thread
    mySocket=mSettings->mSock;
    SetSocketOptions( mSettings );

    SockAddr_localAddr( mSettings );

    if ( mSettings->mLocalhost != NULL ) {
        // bind socket to local address
        rc = bind( mSettings->mSock, (sockaddr*) &mSettings->local,
                   SockAddr_get_sizeof_sockaddr( &mSettings->local ) );
        WARN_errno( rc == SOCKET_ERROR, "bind" );
    }

    // Bound the TCP connect() to the -t value (if it was given on the command line)
    // otherwise let TCP use its defaul timeouts fo the connect()
    if (isModeTime(mSettings) && !isUDP(mSettings)) {
	SetSocketOptionsSendTimeout(mSettings, (mSettings->mAmount * 10000));
    }

    // connect socket
    if (!isUDP(mSettings)) {
        // Synchronize prior to connect() only on a connect-only test
        // Tests with data xfer will sync after the connect()
        // and before the writes()
        if (isConnectOnly(mSettings))
	    StartSynch();

	connect_start.setnow();
	rc = connect( mSettings->mSock, (sockaddr*) &mSettings->peer,
		      SockAddr_get_sizeof_sockaddr( &mSettings->peer ));
	connect_done.setnow();
	connecttime = 1e3 * connect_done.subSec(connect_start);
    } else {
	rc = connect( mSettings->mSock, (sockaddr*) &mSettings->peer,
		      SockAddr_get_sizeof_sockaddr( &mSettings->peer ));
    }
    WARN_errno( rc == SOCKET_ERROR, "connect");
    if (rc != SOCKET_ERROR) {
	getsockname( mSettings->mSock, (sockaddr*) &mSettings->local,
		     &mSettings->size_local );
	getpeername( mSettings->mSock, (sockaddr*) &mSettings->peer,
		     &mSettings->size_peer );
	SockAddr_Ifrname(mSettings);
	connected = true;
    } else {
	connecttime = -1;
	if (mySocket != INVALID_SOCKET) {
	    int rc = close(mySocket);
	    WARN_errno( rc == SOCKET_ERROR, "client connect close" );
	    mySocket = INVALID_SOCKET;
	}
    }
    return connecttime;

} // end Connect

bool Client::isConnected(void) {
#ifdef HAVE_THREAD_DEBUG
  // thread_debug("Client is connected %d", connected);
#endif
    return connected;
}

// There are multiple startup synchronizations, this code
// handles them all. The caller decides to apply them
// either before connect() or after connect() and before writes()
void Client::StartSynch (void) {
#ifdef HAVE_THREAD_DEBUG
    thread_debug("Client start sync enterred");
#endif
    int barrier_needed = !isNoConnectSync(mSettings);
    // Perform delays, usually between connect() and data xfer though before connect
    // Two delays are supported:
    // o First is an absolute start time per unix epoch format
    // o Second is a holdback, a relative amount of seconds between the connect and data xfers
#if defined(HAVE_CLOCK_NANOSLEEP)
    // check for an epoch based start time
    if (isTxStartTime(mSettings)) {
	if (isIsochronous(mSettings)) {
	    Timestamp tmp;
	    tmp.set(mSettings->txstart_epoch.tv_sec, mSettings->txstart_epoch.tv_usec);
	    framecounter = new Isochronous::FrameCounter(mSettings->mFPS, tmp);
	} else {
	    timespec tmp;
	    tmp.tv_sec = mSettings->txstart_epoch.tv_sec;
	    tmp.tv_nsec = mSettings->txstart_epoch.tv_usec * 1000;
	    int rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tmp, NULL);
	    if (rc) {
		fprintf(stderr, "txstart failed clock_nanosleep()=%d\n", rc);
		fflush(stderr);
	    } else {
		barrier_needed = 0;
	    }
	}
    } else if (isTxHoldback(mSettings) && !isConnectOnly(mSettings)) {
	timespec tmp;
	tmp.tv_sec = mSettings->txholdback_timer.tv_sec;
	tmp.tv_nsec = mSettings->txholdback_timer.tv_usec * 1000;
	// See if this a delay between connect and data
	int rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &tmp, NULL);
	if (rc) {
	    fprintf(stderr, "txholdback failed clock_nanosleep()=%d\n", rc);
	} else
	    barrier_needed = 0;
    }
#endif
    if (!isServerReverse(mSettings) && mSettings->multihdr && \
	barrier_needed) {
	BarrierClient(mSettings->connects_done);
    }
    SetReportStartTime();
#ifdef HAVE_THREAD_DEBUG
    thread_debug("Client start sync exited");
#endif
}

void Client::SetReportStartTime (void) {
  struct ReportHeader *reporthdr = myJob;
  //
  // Now the reports are allocated and somewhat initialized,
  // set the report start times and next report times
  //
  if (reporthdr && TimeZero(reporthdr->report.startTime)) {
    // Note: multireport times can be used here because the barrier
    // is the only writer per that mutex
    if (reporthdr->multireport && !TimeZero(reporthdr->multireport->report.startTime)) {
      reporthdr->report.startTime.tv_sec = reporthdr->multireport->report.startTime.tv_sec;
      reporthdr->report.startTime.tv_usec = reporthdr->multireport->report.startTime.tv_usec;
    } else {
      //
      // Can't set multi or bidir report starttimes here, will be set by the reporter thread
      //
      // Possible feature add - optionally use connect_start if the report timing should include
      // the TCP 3WHS
      Timestamp now;
      reporthdr->report.startTime.tv_sec = now.getSecs();
      reporthdr->report.startTime.tv_usec = now.getUsecs();
    }
    // Now that start times are set, set the next times if interval reporting is requested
    if (!TimeZero(reporthdr->report.intervalTime)) {
      reporthdr->report.nextTime = reporthdr->report.startTime;
      TimeAdd(reporthdr->report.nextTime, reporthdr->report.intervalTime);
    }
  }
}

void Client::ConnectPeriodic (void) {
    Timestamp now;
    Timestamp end;
    unsigned int amount_usec = 1000000;
    if (isModeTime(mSettings)) {
	amount_usec = (mSettings->mAmount * 10000);
    }
    end.add(amount_usec); // add in micro seconds
    Timestamp next = now;
    setNoConnectSync(mSettings);
    while (!sInterupted && (mSettings->mInterval && isModeTime(mSettings) && now.before(end) && next.before(end))) {
	while (!now.before(next)) {
	    next.add(mSettings->mInterval);
	}
	if (next.before(end)) {
	    timespec tmp;
	    tmp.tv_sec = next.getSecs();
	    tmp.tv_nsec = next.getUsecs() * 1000;
#if defined(HAVE_CLOCK_NANOSLEEP)
	    int rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tmp, NULL);
	    if (rc) {
		fprintf(stderr, "ConnectPeriodic() failed clock_nanosleep()=%d\n", rc);
	    }
#else
	    now.setnow();
	    delay_loop(next.subUsec(now));
#endif
	    if (isConnected() && (mySocket != INVALID_SOCKET)) {
		int rc = close(mySocket);
		WARN_errno( rc == SOCKET_ERROR, "client close" );
		mySocket = INVALID_SOCKET;
		unsetReport(mSettings);
	    } else {
		unsetReport(mSettings);
	    }
	    if (!isConnected() && isReport(mSettings)) {
		struct ReportHeader *tmp = ReportSettings(mSettings);
		// Post a settings report now
		PostReport(tmp);
	    }
	    mSettings->reporthdr = NULL;
	    double ct = Connect();
	    InitConnectionReport(mSettings);
	    if (mSettings->reporthdr) {
		mSettings->reporthdr->report.connection.connecttime = ct;
		PostReport(mSettings->reporthdr);
	    }
	    now.setnow();
	}
    }
}
/* -------------------------------------------------------------------
 * Common traffic loop intializations
 * ------------------------------------------------------------------- */
void Client::InitTrafficLoop (void) {
    //  Enable socket write timeouts for responsive reporting
    //  Do this after the connection establishment
    //  and after Client::InitiateServer as during these
    //  default socket timeouts are preferred.
    int sosndtimer = 0;
    // sosndtimer units microseconds
    if ((mSettings->mIntervalMode == kInterval_Time) && mSettings->mInterval) {
      sosndtimer = mSettings->mInterval / 2;
    } else if (isModeTime(mSettings)) {
      sosndtimer = (mSettings->mAmount * 10000) / 2;
    }
    SetSocketOptionsSendTimeout(mSettings, sosndtimer);
    // set the lower bounds delay based of the socket timeout timer
    // units needs to be in nanoseconds
    delay_lower_bounds = (double) sosndtimer * -1e3;

    // set the total bytes sent to zero
    totLen = 0;

    /*
     * Set up common termination variables
     *
     * Terminate the thread by setitimer's alarm (if possible)
     * as the alarm will break a blocked syscall (i.e. the write)
     * and provide for accurate timing. Otherwise the thread cannot
     * terminate until the write completes or the socket SO_SNDTIMEO occurs.
     *
     * In the case of no setitimer we're just using the gettimeofday (or equivalent)
     * calls to determine if the loop time exceeds the request time
     * and the blocking writes will affect timing.  The socket has set
     * SO_SNDTIMEO to 1/2 the overall time (which should help limit
     * gross error) or 1/2 the report interval time (better precision)
     *
     * Side note: An advantage of not using interval reports w/TCP is that
     * the code path won't make any clock syscalls in the main loop
     *
     * For Dual and TradeOff tests we can't use itimer in the Client
     * thread because it is executed at both ends, conflicting with
     * the Server thread's itimer.  The Client process then rejects
     * the reverse connection, and the Server process exits early.  To
     * resolve this, only use the itimer mechanism for "Normal" tests.
     */

    if (isModeTime(mSettings)) {
        mEndTime.setnow();
        mEndTime.add( mSettings->mAmount / 100.0 );
    }

    lastPacketTime.setnow();
    readAt = mBuf;

    // Set up trip time values that don't change
    if (isTripTime(mSettings) || isIsochronous(mSettings)) {
      struct TCP_burst_payload * mBuf_burst = (struct TCP_burst_payload *) mBuf;
      mBuf_burst->typelen.type = htonl(CLIENTTCPHDR);
      mBuf_burst->typelen.length =  htonl(sizeof(struct TCP_burst_payload));
      mBuf_burst->flags = htonl(HEADER_TRIPTIME | HEADER_SEQNO64B);
      mBuf_burst->start_tv_sec = htonl(myJob->report.startTime.tv_sec);
      mBuf_burst->start_tv_usec = htonl(myJob->report.startTime.tv_usec);
    }
}


/* -------------------------------------------------------------------
 * Run the appropriate send loop between
 *
 * 1) TCP without rate limiting
 * 2) TCP with rate limiting
 * 3) UDP
 * 4) UDP isochronous w/vbr
 *
 * ------------------------------------------------------------------- */
void Client::Run( void ) {

    if (isConnectOnly(mSettings))
        return;

    // Peform common traffic setup
    InitTrafficLoop();

    /*
     * UDP specific setup
     */
    if (isUDP(mSettings)) {
	// Preset any UDP fields in the mBuf, a non-zero
	// return indicates some udptests were set
	int udptests = Settings_GenerateClientHdr(mSettings, (client_hdr *) (mBuf + sizeof(struct UDP_datagram)));

	if ( isFileInput( mSettings ) ) {
	    // Due to the UDP timestamps etc, included
	    // reduce the read size by an amount
	    // equal to the header size
	    if ( isCompat( mSettings ) ) {
		Extractor_reduceReadSize( sizeof(struct UDP_datagram), mSettings );
		readAt += sizeof(struct UDP_datagram);
	    } else {
		if (udptests) {
		    Extractor_reduceReadSize(sizeof(client_hdr_udp_tests), mSettings );
		    readAt += sizeof(client_hdr_udp_tests);
		} else {
		    Extractor_reduceReadSize( sizeof(struct UDP_datagram) +
					      sizeof(struct client_hdr), mSettings );
		    readAt += sizeof(struct UDP_datagram) + sizeof(struct client_hdr);
		}
	    }
	}
	// Launch the approprate UDP traffic loop
	if (isIsochronous(mSettings)) {
	    RunUDPIsochronous();
	} else {
	    RunUDP();
	}
    } else {
	// Launch the approprate TCP traffic loop
	if (mSettings->mUDPRate > 0)
	    RunRateLimitedTCP();
	else
	    RunTCP();
    }
}

/*
 * TCP send loop
 */


void Client::RunTCP( void ) {
    int burst_size = (mSettings->mWriteAckLen > 0) ? mSettings->mWriteAckLen : mSettings->mBufLen;
    int burst_remaining = 0;
    int burst_id = 1;

    // RJM, consider moving this into the constructor
    if (isIsochronous(mSettings)) {
	framecounter = new Isochronous::FrameCounter(mSettings->mFPS);
    }

    while (InProgress()) {
        if (isModeAmount(mSettings)) {
	    reportstruct->packetLen = ((mSettings->mAmount < (unsigned) mSettings->mBufLen) ? mSettings->mAmount : mSettings->mBufLen);
	} else {
	    reportstruct->packetLen = mSettings->mBufLen;
	}

        int n = 0;
	if (isTripTime(mSettings) || isWriteAck(mSettings) || isIsochronous(mSettings)) {
	    if (burst_remaining == 0) {
		if (framecounter) {
		    burst_size = (int) (lognormal(mSettings->mMean,mSettings->mVariance)) / (mSettings->mFPS * 8);
		    burst_id =  framecounter->wait_tick();
		}
		// RJM fix below, consider using the timer value vs re-reading the clock
		// the choice depends on the schedulding latency per clock_nanosleep()
		now.setnow();
		reportstruct->packetTime.tv_sec = now.getSecs();
		reportstruct->packetTime.tv_usec = now.getUsecs();
	        WriteTcpTxHdr(reportstruct, burst_size, burst_id++);
		reportstruct->sentTime = reportstruct->packetTime;
		burst_remaining = burst_size;
		// perform write
		n = writen(mSettings->mSock, mBuf, sizeof(struct TCP_burst_payload));
		WARN(n != sizeof(struct TCP_burst_payload), "burst hdr write failed");
		burst_remaining -= n;
		reportstruct->packetLen -= n;
		// thread_debug("***write burst header %d id=%d", burst_size, (burst_id - 1));
	    }
	    if (reportstruct->packetLen > burst_remaining) {
		reportstruct->packetLen = burst_remaining;
	    }
	}
	// printf("pl=%ld\n",reportstruct->packetLen);
	// perform write
	WARN(reportstruct->packetLen <= 0, "invalid write req size");
	int len = write(mSettings->mSock, mBuf, reportstruct->packetLen);
        if (len < 0) {
	    if (NONFATALTCPWRITERR(errno)) {
	        reportstruct->errwrite=WriteErrAccount;
	    } else if (FATALTCPWRITERR(errno)) {
	        reportstruct->errwrite=WriteErrFatal;
	        WARN_errno( 1, "write" );
		break;
	    } else {
	        reportstruct->errwrite=WriteErrNoAccount;
	    }
	    len = 0;
	} else {
	    totLen += len + n;
	    reportstruct->errwrite=WriteNoErr;
#ifdef HAVE_THREAD_DEBUG
	    {
		if (len != reportstruct->packetLen)
		    thread_debug("write size mismatch req=%ld, actual=%d", reportstruct->packetLen, len);
	    }
#endif
	}
	if (isTripTime(mSettings) || isWriteAck(mSettings) || isIsochronous(mSettings))
	    burst_remaining -= len;
	reportstruct->packetLen = len + n;
	now.setnow();
	reportstruct->packetTime.tv_sec = now.getSecs();
	reportstruct->packetTime.tv_usec = now.getUsecs();
	reportstruct->sentTime = reportstruct->packetTime;
	if ((mSettings->mIntervalMode == kInterval_Time) || isEnhanced(mSettings)) {
            ReportPacket(mSettings->reporthdr, reportstruct);
        }
        if (isModeAmount(mSettings)) {
            /* mAmount may be unsigned, so don't let it underflow! */
	    if( mSettings->mAmount >= (unsigned long) (reportstruct->packetLen) ) {
                mSettings->mAmount -= (unsigned long) (reportstruct->packetLen);
            } else {
		mSettings->mAmount = 0;
            }
        }
    }

    FinishTrafficActions();
}

/*
 * A version of the transmit loop that supports TCP rate limiting using a token bucket
 */
void Client::RunRateLimitedTCP ( void ) {
    double tokens = 0;
    Timestamp time1, time2;
    int burst_size = (mSettings->mWriteAckLen > 0) ? mSettings->mWriteAckLen : mSettings->mBufLen;
    int burst_remaining = 0;
    int burst_id = 1;

    long var_rate = mSettings->mUDPRate;
    int fatalwrite_err = 0;
    while (InProgress() && !fatalwrite_err) {
	// Add tokens per the loop time
	// clock_gettime is much cheaper than gettimeofday() so
	// use it if possible.
	time2.setnow();
        if (isVaryLoad(mSettings)) {
	    static Timestamp time3;
	    if (time2.subSec(time3) >= VARYLOAD_PERIOD) {
		var_rate = lognormal(mSettings->mUDPRate,mSettings->mVariance);
		time3 = time2;
		if (var_rate < 0)
		    var_rate = 0;
	    }
	}
	tokens += time2.subSec(time1) * (var_rate / 8.0);
	time1 = time2;
	if (tokens >= 0.0) {
	    if (isModeAmount(mSettings)) {
	        reportstruct->packetLen = ((mSettings->mAmount < (unsigned) mSettings->mBufLen) ? mSettings->mAmount : mSettings->mBufLen);
	    } else {
	        reportstruct->packetLen = mSettings->mBufLen;
	    }
	    // perform write
	    int n = 0;
	    if (isTripTime(mSettings) || isWriteAck(mSettings)) {
		if (burst_remaining == 0) {
		    now.setnow();
		    reportstruct->packetTime.tv_sec = now.getSecs();
		    reportstruct->packetTime.tv_usec = now.getUsecs();
		    WriteTcpTxHdr(reportstruct, burst_size, burst_id++);
		    reportstruct->sentTime = reportstruct->packetTime;
		    burst_remaining = burst_size;
		    // perform write
		    n = writen(mSettings->mSock, mBuf, sizeof(struct TCP_burst_payload));
		    WARN(n != sizeof(struct TCP_burst_payload), "burst hdr write failed");
		    burst_remaining -= n;
		    reportstruct->packetLen -= n;
		    // thread_debug("***write burst header %d id=%d", burst_size, (burst_id - 1));
		} else if (reportstruct->packetLen > burst_remaining) {
		    reportstruct->packetLen = burst_remaining;
		}
	    }

	    int len = write( mSettings->mSock, mBuf, reportstruct->packetLen);
	    if ( len < 0 ) {
	        if (NONFATALTCPWRITERR(errno)) {
		    reportstruct->errwrite=WriteErrAccount;
		} else if (FATALTCPWRITERR(errno)) {
		    reportstruct->errwrite=WriteErrFatal;
		    WARN_errno( 1, "write" );
		    fatalwrite_err = 1;
		    break;
		} else {
		    reportstruct->errwrite=WriteErrNoAccount;
	        }
		len = 0;
	    } else {
		// Consume tokens per the transmit
	        tokens -= (len + n);
	        totLen += (len + n);;
		reportstruct->errwrite=WriteNoErr;
	    }
	    if (isTripTime(mSettings) || isWriteAck(mSettings))
		burst_remaining -= len;

	    time2.setnow();
	    reportstruct->packetLen = len + n;
	    reportstruct->packetTime.tv_sec = time2.getSecs();
	    reportstruct->packetTime.tv_usec = time2.getUsecs();
	    reportstruct->sentTime = reportstruct->packetTime;

	    if (isEnhanced(mSettings) || (mSettings->mIntervalMode == kInterval_Time)) {
		ReportPacket( mSettings->reporthdr, reportstruct );
	    }

	    if (isModeAmount(mSettings)) {
		/* mAmount may be unsigned, so don't let it underflow! */
		if( mSettings->mAmount >= (unsigned long) reportstruct->packetLen ) {
		    mSettings->mAmount -= (unsigned long) reportstruct->packetLen;
		} else {
		    mSettings->mAmount = 0;
		}
	    }
        } else {
	    // Use a 4 usec delay to fill tokens
	    delay_loop(4);
	}
    }

    FinishTrafficActions();
}

/*
 * UDP send loop
 */
void Client::RunUDP( void ) {
    struct UDP_datagram* mBuf_UDP = (struct UDP_datagram*) mBuf;
    int currLen;

    double delay_target = 0;
    double delay = 0;
    double adjust = 0;

    // compute delay target in units of nanoseconds
    if (mSettings->mUDPRateUnits == kRate_BW) {
	// compute delay for bandwidth restriction, constrained to [0,1] seconds
	delay_target = (double) ( mSettings->mBufLen * ((kSecs_to_nsecs * kBytes_to_Bits)
							/ mSettings->mUDPRate) );
    } else {
	delay_target = 1e9 / mSettings->mUDPRate;
    }
    if ( delay_target < 0  ||
	 delay_target > 1.0 * kSecs_to_nsecs ) {
	fprintf( stderr, warn_delay_large, delay_target / kSecs_to_nsecs );
	delay_target = 1.0 * kSecs_to_nsecs;
    }

    // Set this to > 0 so first loop iteration will delay the IPG
    currLen = 1;
    double variance = mSettings->mVariance;

    while (InProgress()) {
        // Test case: drop 17 packets and send 2 out-of-order:
        // sequence 51, 52, 70, 53, 54, 71, 72
        //switch( datagramID ) {
        //  case 53: datagramID = 70; break;
        //  case 71: datagramID = 53; break;
        //  case 55: datagramID = 71; break;
        //  default: break;
        //}
	now.setnow();
	reportstruct->packetTime.tv_sec = now.getSecs();
	reportstruct->packetTime.tv_usec = now.getUsecs();
	reportstruct->sentTime = reportstruct->packetTime;
        if (isVaryLoad(mSettings) && mSettings->mUDPRateUnits == kRate_BW) {
	    static Timestamp time3;
	    if (now.subSec(time3) >= VARYLOAD_PERIOD) {
		long var_rate = lognormal(mSettings->mUDPRate,variance);
		if (var_rate < 0)
		    var_rate = 0;

		delay_target = (double) ( mSettings->mBufLen * ((kSecs_to_nsecs * kBytes_to_Bits)
								/ var_rate) );
		time3 = now;
	    }
	}
	// store datagram ID into buffer
	WritePacketID(reportstruct->packetID++);
	mBuf_UDP->tv_sec  = htonl(reportstruct->packetTime.tv_sec);
	mBuf_UDP->tv_usec = htonl(reportstruct->packetTime.tv_usec);

	// Adjustment for the running delay
	// o measure how long the last loop iteration took
	// o calculate the delay adjust
	//   - If write succeeded, adjust = target IPG - the loop time
	//   - If write failed, adjust = the loop time
	// o then adjust the overall running delay
	// Note: adjust units are nanoseconds,
	//       packet timestamps are microseconds
	if (currLen > 0)
	    adjust = delay_target + \
		(1000.0 * lastPacketTime.subUsec( reportstruct->packetTime ));
	else
	    adjust = 1000.0 * lastPacketTime.subUsec( reportstruct->packetTime );

	lastPacketTime.set( reportstruct->packetTime.tv_sec,
			    reportstruct->packetTime.tv_usec );
	// Since linux nanosleep/busyloop can exceed delay
	// there are two possible equilibriums
	//  1)  Try to perserve inter packet gap
	//  2)  Try to perserve requested transmit rate
	// The latter seems preferred, hence use a running delay
	// that spans the life of the thread and constantly adjust.
	// A negative delay means the iperf app is behind.
	delay += adjust;
	// Don't let delay grow unbounded
	if (delay < delay_lower_bounds) {
	    delay = delay_target;
	}

	reportstruct->errwrite = WriteNoErr;
	reportstruct->emptyreport = 0;

	// perform write
	if (isModeAmount(mSettings)) {
	    currLen = write( mSettings->mSock, mBuf, (mSettings->mAmount < (unsigned) mSettings->mBufLen) ? mSettings->mAmount : mSettings->mBufLen);
	} else {
	    currLen = write( mSettings->mSock, mBuf, mSettings->mBufLen);
	}
	if ( currLen < 0 ) {
	    reportstruct->packetID--;
	    if (FATALUDPWRITERR(errno)) {
	        reportstruct->errwrite = WriteErrFatal;
	        WARN_errno( 1, "write" );
		break;
	    } else {
	        reportstruct->errwrite = WriteErrAccount;
	        currLen = 0;
	    }
	    reportstruct->emptyreport = 1;
	}

	if (isModeAmount(mSettings)) {
	    /* mAmount may be unsigned, so don't let it underflow! */
	    if( mSettings->mAmount >= (unsigned long) currLen ) {
	        mSettings->mAmount -= (unsigned long) currLen;
	    } else {
	        mSettings->mAmount = 0;
	    }
	}

	// report packets
	reportstruct->packetLen = (unsigned long) currLen;
	ReportPacket( mSettings->reporthdr, reportstruct );
	// Insert delay here only if the running delay is greater than 100 usec,
	// otherwise don't delay and immediately continue with the next tx.
	if ( delay >= 100000 ) {
	    // Convert from nanoseconds to microseconds
	    // and invoke the microsecond delay
	    delay_loop((unsigned long) (delay / 1000));
	}
    }
    FinishTrafficActions();
}

/*
 * UDP isochronous send loop
 */
void Client::RunUDPIsochronous (void) {
    struct UDP_datagram* mBuf_UDP = (struct UDP_datagram*) mBuf;
    // skip over the UDP datagram (seq no, timestamp) to reach the isoch fields
    struct client_hdr_udp_isoch_tests *testhdr = (client_hdr_udp_isoch_tests *)(mBuf + sizeof(client_hdr_v1) + sizeof(UDP_datagram));
    struct UDP_isoch_payload* mBuf_isoch = &(testhdr->isoch);

    double delay_target = mSettings->mBurstIPG * 1000000;  // convert from milliseconds to nanoseconds
    double delay = 0;
    double adjust = 0;
    int currLen = 1;
    int frameid=0;
    Timestamp t1;
    int bytecntmin;
    // make sure the packet can carry the isoch payload
    if (isModeTime(mSettings)) {
	bytecntmin = sizeof(UDP_datagram) + sizeof(client_hdr_v1) + sizeof(struct client_hdr_udp_isoch_tests);
    } else {
	bytecntmin = 1;
    }
    if (!framecounter) {
	framecounter = new Isochronous::FrameCounter(mSettings->mFPS);
    }
    mBuf_isoch->burstperiod = htonl(framecounter->period_us());

    int initdone = 0;
    int fatalwrite_err = 0;
    while (InProgress() && !fatalwrite_err) {
	int bytecnt = (int) (lognormal(mSettings->mMean,mSettings->mVariance)) / (mSettings->mFPS * 8);
	if (bytecnt < bytecntmin)
	    bytecnt = bytecntmin;
	delay = 0;

	// printf("bits=%d\n", (int) (mSettings->mFPS * bytecnt * 8));
	mBuf_isoch->burstsize  = htonl(bytecnt);
	mBuf_isoch->prevframeid  = htonl(frameid);
	reportstruct->burstsize=bytecnt;
	frameid =  framecounter->wait_tick();
	mBuf_isoch->frameid  = htonl(frameid);
	lastPacketTime.setnow();
	if (!initdone) {
	    initdone = 1;
	    mBuf_isoch->start_tv_sec = htonl(framecounter->getSecs());
	    mBuf_isoch->start_tv_usec = htonl(framecounter->getUsecs());
	}

	while ((bytecnt > 0) && InProgress()) {
	    t1.setnow();
	    reportstruct->packetTime.tv_sec = t1.getSecs();
	    reportstruct->packetTime.tv_usec = t1.getUsecs();
	    reportstruct->sentTime = reportstruct->packetTime;
	    mBuf_UDP->tv_sec  = htonl(reportstruct->packetTime.tv_sec);
	    mBuf_UDP->tv_usec = htonl(reportstruct->packetTime.tv_usec);
	    WritePacketID(reportstruct->packetID++);

	    // Adjustment for the running delay
	    // o measure how long the last loop iteration took
	    // o calculate the delay adjust
	    //   - If write succeeded, adjust = target IPG - the loop time
	    //   - If write failed, adjust = the loop time
	    // o then adjust the overall running delay
	    // Note: adjust units are nanoseconds,
	    //       packet timestamps are microseconds
	    if (currLen > 0)
		adjust = delay_target + \
		    (1000.0 * lastPacketTime.subUsec( reportstruct->packetTime ));
	    else
		adjust = 1000.0 * lastPacketTime.subUsec( reportstruct->packetTime );

	    lastPacketTime.set( reportstruct->packetTime.tv_sec,
				reportstruct->packetTime.tv_usec );
	    // Since linux nanosleep/busyloop can exceed delay
	    // there are two possible equilibriums
	    //  1)  Try to perserve inter packet gap
	    //  2)  Try to perserve requested transmit rate
	    // The latter seems preferred, hence use a running delay
	    // that spans the life of the thread and constantly adjust.
	    // A negative delay means the iperf app is behind.
	    delay += adjust;
	    // Don't let delay grow unbounded
	    // if (delay < delay_lower_bounds) {
	    //	  delay = delay_target;
	    // }

	    reportstruct->errwrite = WriteNoErr;
	    reportstruct->emptyreport = 0;

	    // perform write
	    if (isModeAmount(mSettings) && (mSettings->mAmount < (unsigned) mSettings->mBufLen)) {
	        mBuf_isoch->remaining = htonl(mSettings->mAmount);
		reportstruct->remaining=mSettings->mAmount;
	        currLen = write(mSettings->mSock, mBuf, mSettings->mAmount);
	    } else {
	        mBuf_isoch->remaining = htonl(bytecnt);
		reportstruct->remaining=bytecnt;
	        currLen = write(mSettings->mSock, mBuf, (bytecnt < mSettings->mBufLen) ? bytecnt : mSettings->mBufLen);
	    }

	    if (currLen < 0) {
	        reportstruct->packetID--;
		reportstruct->emptyreport = 1;
		currLen = 0;
		if (FATALUDPWRITERR(errno)) {
	            reportstruct->errwrite = WriteErrFatal;
	            WARN_errno( 1, "write" );
		    fatalwrite_err = 1;
	        } else {
		    reportstruct->errwrite = WriteErrAccount;
		}
	    } else {
		bytecnt -= currLen;
		// adjust bytecnt so last packet of burst is greater or equal to min packet
		if ((bytecnt > 0) && (bytecnt < bytecntmin)) {
		    bytecnt = bytecntmin;
		    mBuf_isoch->burstsize  = htonl(bytecnt);
		    reportstruct->burstsize=bytecnt;
		}
	    }

	    if (isModeAmount(mSettings)) {
	        /* mAmount may be unsigned, so don't let it underflow! */
	        if( mSettings->mAmount >= (unsigned long) currLen ) {
		    mSettings->mAmount -= (unsigned long) currLen;
		} else {
		    mSettings->mAmount = 0;
		}
	    }
	    // report packets

	    reportstruct->frameID=frameid;
	    reportstruct->packetLen = (unsigned long) currLen;
	    ReportPacket( mSettings->reporthdr, reportstruct );

	    // Insert delay here only if the running delay is greater than 1 usec,
	    // otherwise don't delay and immediately continue with the next tx.
	    if ( delay >= 1000 ) {
		// Convert from nanoseconds to microseconds
		// and invoke the microsecond delay
		delay_loop((unsigned long) (delay / 1000));
	    }
	}
    }

    FinishTrafficActions();
    DELETE_PTR(framecounter);
}
// end RunUDPIsoch



inline void Client::WritePacketID (intmax_t packetID) {
    struct UDP_datagram * mBuf_UDP = (struct UDP_datagram *) mBuf;
    // store datagram ID into buffer
#ifdef HAVE_INT64_T
    // Pack signed 64bit packetID into unsigned 32bit id1 + unsigned
    // 32bit id2.  A legacy server reading only id1 will still be able
    // to reconstruct a valid signed packet ID number up to 2^31.
    uint32_t id1, id2;
    id1 = packetID & 0xFFFFFFFFLL;
    id2 = (packetID  & 0xFFFFFFFF00000000LL) >> 32;

    mBuf_UDP->id = htonl(id1);
    mBuf_UDP->id2 = htonl(id2);

#ifdef SHOW_PACKETID
    printf("id %" PRIdMAX " (0x%" PRIxMAX ") -> 0x%x, 0x%x\n",
	   packetID, packetID, id1, id2);
#endif
#else
    mBuf_UDP->id = htonl((reportstruct->packetID));
#endif
}

inline void Client::WriteTcpTxHdr (ReportStruct *reportstruct, int burst_size, int burst_id) {
    struct TCP_burst_payload * mBuf_burst = (struct TCP_burst_payload *) mBuf;
    // store packet ID into buffer
    reportstruct->packetID += burst_size;
#ifdef HAVE_INT64_T
    // Pack signed 64bit packetID into unsigned 32bit id1 + unsigned
    // 32bit id2.  A legacy server reading only id1 will still be able
    // to reconstruct a valid signed packet ID number up to 2^31.
    uint32_t id1, id2;
    id1 = reportstruct->packetID & 0xFFFFFFFFLL;
    id2 = (reportstruct->packetID  & 0xFFFFFFFF00000000LL) >> 32;

    mBuf_burst->seqno_lower = htonl(id1);
    mBuf_burst->seqno_upper = htonl(id2);

#ifdef SHOW_PACKETID
    printf("id %" PRIdMAX " (0x%" PRIxMAX ") -> 0x%x, 0x%x\n",
	   packetID, packetID, id1, id2);
#endif
#else
    mBuf_burst->seqno_lower = htonl((reportstruct->packetID));
    mBuf_burst->seqno_upper = htonl(0x0);
#endif
    mBuf_burst->send_tt.write_tv_sec  = htonl(reportstruct->packetTime.tv_sec);
    mBuf_burst->send_tt.write_tv_usec  = htonl(reportstruct->packetTime.tv_usec);
    mBuf_burst->burst_id  = htonl((uint32_t)burst_id);
    mBuf_burst->burst_size  = htonl((uint32_t)burst_size);
    mBuf_burst->burst_period_s  = htonl(0x0);
    mBuf_burst->burst_period_us  = htonl(0x0);
    return;
}

inline bool Client::InProgress (void) {
    // Read the next data block from
    // the file if it's file input
    if (isFileInput(mSettings)) {
	Extractor_getNextDataBlock( readAt, mSettings );
        if (Extractor_canRead(mSettings) != 0)
	    return true;
	else
	    return false;
    }

    if (sInterupted ||
	(isModeTime(mSettings) &&  mEndTime.before(reportstruct->packetTime))  ||
	(isModeAmount(mSettings) && (mSettings->mAmount <= 0)))
	return false;
    return true;
}

/*
 * Common things to do to finish a traffic thread
 */
void Client::FinishTrafficActions(void) {
    // Shutdown the TCP socket's writes as the event for the server to end its traffic loop
    if (!isUDP(mSettings) && (mySocket != INVALID_SOCKET) && isConnected()) {
        int rc = shutdown(mySocket, SHUT_WR);
#ifdef HAVE_THREAD_DEBUG
        thread_debug("Client calls shutdown() SHUTW_WR on tcp socket %d", mySocket);
#endif
        WARN_errno( rc == SOCKET_ERROR, "shutdown" );
    }

    // stop timing
    now.setnow();
    reportstruct->packetTime.tv_sec = now.getSecs();
    reportstruct->packetTime.tv_usec = now.getUsecs();
    reportstruct->sentTime = reportstruct->packetTime;
    /*
     *  For UDP, there is a final handshake between the client and the server,
     *  do that now (unless requested no to)
     */
    if (isUDP(mSettings)) {
	FinalUDPHandshake();
    }
    /*
     *  For TCP and if not doing interval or enhanced reporting (needed for write accounting),
     *  then report the entire transfer as one big packet
     *
     */
    if(!isUDP(mSettings) && !isEnhanced(mSettings) && (mSettings->mIntervalMode != kInterval_Time)) {
	reportstruct->packetLen = totLen;
	ReportPacket(mSettings->reporthdr, reportstruct);
	reportstruct->packetLen = 0;
    }
    CloseReport( mSettings->reporthdr, reportstruct);
    EndReport( mSettings->reporthdr );
}


/* -------------------------------------------------------------------
 * Send a datagram on the socket. The datagram's contents should signify
 * a FIN to the application. Keep re-transmitting until an
 * acknowledgement datagram is received.
 * ------------------------------------------------------------------- */
void Client::FinalUDPHandshake(void) {
    struct UDP_datagram * mBuf_UDP = (struct UDP_datagram *) mBuf;

    // send a final terminating datagram
    // Don't count in the mTotalLen. The server counts this one,
    // but didn't count our first datagram, so we're even now.
    // The negative datagram ID signifies termination to the server.
    WritePacketID(-reportstruct->packetID);
    mBuf_UDP->tv_usec = htonl( reportstruct->packetTime.tv_usec );
    write( mSettings->mSock, mBuf, mSettings->mBufLen );

    // Handle the acknowledgement and server report for
    // cases where it's wanted and possible
    if (!(isMulticast(mSettings) || isNoUDPfin(mSettings))) {
	// Unicast send and wait for acks
	write_UDP_FIN();
    }
}

void Client::write_UDP_FIN (void) {
    int rc;
    fd_set readSet;
    struct timeval timeout;

    int count = 10 ;
    while (--count >= 0) {
        // wait until the socket is readable, or our timeout expires
        FD_ZERO( &readSet );
        FD_SET( mSettings->mSock, &readSet );
        timeout.tv_sec  = 0;
        timeout.tv_usec = (count > 5) ? 5000 : 250000; // 5 millisecond or 0.25 second

        rc = select( mSettings->mSock+1, &readSet, NULL, NULL, &timeout );
        FAIL_errno( rc == SOCKET_ERROR, "select", mSettings );

        // rc= zero means select's read timed out
	if ( rc == 0 ) {
	    // decrement the packet count
	    //
	    // Note: a negative packet id is used to tell the server
	    // this UDP stream is terminating.  The server will remove
	    // the sign.  So a decrement will be seen as increments by
	    // the server (e.g, -1000, -1001, -1002 as 1000, 1001, 1002)
	    // If the retries weren't decrement here the server can get out
	    // of order packets per these retries actually being received
	    // by the server (e.g. -1000, -1000, -1000)
	    WritePacketID(-(++reportstruct->packetID));
	    // write data
	    write( mSettings->mSock, mBuf, mSettings->mBufLen );
            continue;
        } else {
            // socket ready to read, this packet size
	    // is set by the server.  Assume it's large enough
	    // to contain the final server packet
            rc = read( mSettings->mSock, mBuf, MAXUDPBUF);
	    WARN_errno( rc < 0, "read" );
	    if ( rc < 0 ) {
                break;
            } else if ( rc >= (int) (sizeof(UDP_datagram) + sizeof(server_hdr)) ) {
                ReportServerUDP( mSettings, (server_hdr*) ((UDP_datagram*)mBuf + 1) );
            }
            return;
        }
    }

    fprintf( stderr, warn_no_ack, mSettings->mSock, (isModeTime(mSettings) ? 10 : 1));
}
// end write_UDP_FIN


void Client::InitiateServer(void) {
    if (!isCompat(mSettings) && !isConnectOnly(mSettings)) {
	int flags = 0;
        client_hdr* temp_hdr;
        if ( isUDP( mSettings ) ) {
            UDP_datagram *UDPhdr = (UDP_datagram *)mBuf;
	    // skip over the UDP datagram (seq no, timestamp)
            temp_hdr = (client_hdr*)(UDPhdr + 1);
        } else {
            temp_hdr = (client_hdr*)mBuf;
        }
	flags = Settings_GenerateClientHdr( mSettings, temp_hdr );

	if (flags & (HEADER_EXTEND | HEADER_VERSION1)) {
	    //  This test requires the pre-test header messages
	    //  The extended headers require an exchange
	    //  between the client and server/listener
	    HdrXchange(flags);
	}
    }
}


void Client::HdrXchange(int flags) {
    int currLen = 0, len;

    if (flags & HEADER_EXTEND) {
	// Run compatability detection and test info exchange for tests that require it
	int optflag;
	if (isUDP(mSettings)) {
	    struct UDP_datagram* mBuf_UDP = (struct UDP_datagram*) mBuf;
	    Timestamp now;
	    len = mSettings->mBufLen;
	    // UDP header message must be mBufLen so server/Listener will read it
	    // because the Listener read length uses  mBufLen
	    if ((int) (sizeof(UDP_datagram) + sizeof(client_hdr)) > len) {
	        fprintf( stderr, warn_len_too_small_peer_exchange, "Client", len, (sizeof(UDP_datagram) + sizeof(client_hdr)));
	    }
	    // store datagram ID and timestamp into buffer
	    mBuf_UDP->id      = htonl(0);
	    mBuf_UDP->tv_sec  = htonl(now.getSecs());
	    mBuf_UDP->tv_usec = htonl(now.getUsecs());
	} else {
	    len = sizeof(client_hdr);
#ifdef TCP_NODELAY
	    // Disable Nagle to reduce latency of this intial message
	    optflag=1;
	    if(setsockopt( mSettings->mSock, IPPROTO_TCP, TCP_NODELAY, (char *)&optflag, sizeof(int)) < 0 )
		WARN_errno(0, "tcpnodelay" );
#endif
	}
	currLen = send( mSettings->mSock, mBuf, len, 0 );
	if ( currLen < 0 ) {
	    WARN_errno( currLen < 0, "send_hdr_v2" );
	} else {
	    int n;
	    client_hdr_ack ack;
	    int sotimer = 2; // 2 seconds
#ifdef WIN32
            // Windows SO_RCVTIMEO uses ms
	    DWORD timeout = (double) sotimer * 1e3;
#else
	    struct timeval timeout;
	    timeout.tv_sec = sotimer;
	    timeout.tv_usec = 0;
#endif
	    if (setsockopt( mSettings->mSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0 ) {
		WARN_errno( mSettings->mSock == SO_RCVTIMEO, "socket" );
	    }
	    /*
	     * Hang a TCP or UDP read and see if this is a header ack message
	     */
	    if ((n = recvn(mSettings->mSock, (char *)&ack, sizeof(client_hdr_ack), 0)) == sizeof(client_hdr_ack)) {
		if (ntohl(ack.typelen.type) == CLIENTHDRACK && ntohl(ack.typelen.length) == sizeof(client_hdr_ack)) {
		    reporter_peerversion (mSettings, ntohl(ack.version_u), ntohl(ack.version_l));
		} else {
		    sprintf(mSettings->peerversion, " (misformed server version)");
		}
	    } else {
		WARN_errno(1, "recvack" );
		sprintf(mSettings->peerversion, " (server version is old)");
	    }
	}
	if (!isUDP( mSettings ) && !isNoDelay(mSettings)) {
	    optflag = 0;
	    // Re-enable Nagle
	    if (setsockopt( mSettings->mSock, IPPROTO_TCP, TCP_NODELAY, (char *)&optflag, sizeof(int)) < 0 ) {
		WARN_errno(0, "tcpnodelay" );
	    }
	}
    } else if (flags & HEADER_VERSION1) {
	if (isUDP(mSettings)) {
	    if ((int) (sizeof(UDP_datagram) + sizeof(client_hdr_v1)) > mSettings->mBufLen) {
		fprintf( stderr, warn_len_too_small_peer_exchange, "Client", mSettings->mBufLen, (sizeof(UDP_datagram) + sizeof(client_hdr_v1)));
	    }
	    // UDP version1 header message is sent as part of normal traffic per Client::Run
	} else {
	    /*
	     * Really should not need this warning as TCP is a byte protocol so the mBufLen shouldn't cause
             * a problem.  Unfortunately, the ver 2.0.5 server didn't read() TCP properly and will fail
             * if the full V1 message does come in a single read.  This was fixed in 2.0.10 but go ahead
	     * and issue a warning in case the server is version 2.0.5
	     */
	    if (((int)sizeof(client_hdr_v1) - mSettings->mBufLen) > 0) {
		fprintf( stderr, warn_len_too_small_peer_exchange, "Client", mSettings->mBufLen, sizeof(client_hdr_v1));
	    }
	    // Send TCP version1 header message now
	    currLen = send( mSettings->mSock, mBuf, sizeof(client_hdr_v1), 0 );
	    WARN_errno( currLen < 0, "send_hdr_v1" );
	}
    }
}
