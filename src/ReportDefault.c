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
 * ReportDefault.c
 * by Kevin Gibbs <kgibbs@nlanr.net>
 *
 * ________________________________________________________________ */

#include <math.h>
#include "headers.h"
#include "Settings.hpp"
#include "util.h"
#include "Reporter.h"
#include "report_default.h"
#include "Thread.h"
#include "Locale.h"
#include "PerfSocket.hpp"
#include "SocketAddr.h"
#include "payloads.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETPOWERCONSTANT 1e-6
/*
 * Prints transfer reports in default style
 */

void reporter_printstats (struct TransferInfo *stats) {
    static char header_printed = 0;
    double bytesxfered;

    byte_snprintf( buffer, sizeof(buffer)/2, (double) stats->TotalLen,
                   toupper( (int)stats->mFormat));
    if (!stats->TotalLen || (stats->endTime < SMALLEST_INTERVAL_SEC)) {
        bytesxfered = 0;
    } else {
        bytesxfered = (double) stats->TotalLen;
    }
    byte_snprintf( &buffer[sizeof(buffer)/2], sizeof(buffer)/2,
                   (((stats->endTime - stats->startTime) > 0) ? (bytesxfered / (stats->endTime - stats->startTime)) : 0),
		   stats->mFormat);

    // TCP reports
    if (!stats->mUDP) {
        if (!stats->mEnhanced && !isTripTime(stats)) {
	    if( !header_printed ) {
		printf( "%s", report_bw_header);
		header_printed = 1;
	    }
	    printf(report_bw_format, stats->transferID,
		   stats->startTime, stats->endTime,
		   buffer, &buffer[sizeof(buffer)/2]);
	} else {
	    if( !header_printed ) {
	        if (!isTripTime(stats))
		    printf((stats->mTCP == (char)kMode_Server ? report_bw_read_enhanced_header : report_bw_write_enhanced_header), (stats->sock_callstats.read.binsize/1024.0));
		else
		    printf((stats->mTCP == (char)kMode_Server ? report_bw_read_enhanced_netpwr_header : report_bw_write_enhanced_header), (stats->sock_callstats.read.binsize/1024.0));
		header_printed = 1;
	    }
	    if (stats->mTCP == (char)kMode_Server) {
		char Lbuf[80];
		double meantransit = (stats->transit.sumTransit / stats->transit.cntTransit);
		// long burstavg =  (stats->transit.cntTransit ? ((int) bytesxfered / stats->transit.cntTransit) : 0);
		// byte_snprintf(Lbuf, sizeof(Lbuf), (double) bytesxfered / stats->arrivalSum, 'a');
		// printf("arrival rate = %f %s timesum = %f transit = %f\n", ((double) bytesxfered / stats->arrivalSum), Lbuf, stats->arrivalSum, meantransit);
		//
		//  Little's law is L = lambda * W, where L is queue depth,
		//  lambda the arrival rate and W is the processing time
		//
		double L  = ((stats->arrivalSum > 0.0) ? (((double) bytesxfered / stats->arrivalSum) * meantransit) : NAN);
		byte_snprintf(Lbuf, sizeof(Lbuf), L, 'A');
		printf(((!isTripTime(stats) || !bytesxfered) ? report_bw_read_enhanced_format : report_bw_read_enhanced_netpwr_format),
		       stats->transferID, stats->startTime, stats->endTime,
		       buffer, &buffer[sizeof(buffer)/2],
		       stats->sock_callstats.read.cntRead,
		       stats->sock_callstats.read.bins[0],
		       stats->sock_callstats.read.bins[1],
		       stats->sock_callstats.read.bins[2],
		       stats->sock_callstats.read.bins[3],
		       stats->sock_callstats.read.bins[4],
		       stats->sock_callstats.read.bins[5],
		       stats->sock_callstats.read.bins[6],
		       stats->sock_callstats.read.bins[7],
		       (meantransit * 1e3),
		       stats->transit.minTransit*1e3,
		       stats->transit.maxTransit*1e3,
		       (stats->transit.cntTransit < 2) ? 0 : sqrt(stats->transit.m2Transit / (stats->transit.cntTransit - 1)) / 1e3,
		       stats->transit.cntTransit,
		       stats->transit.cntTransit ? (long) (bytesxfered / (double) stats->transit.cntTransit) : 0,
		       Lbuf,
		       (meantransit > 0.0) ? (NETPOWERCONSTANT * ((double) bytesxfered) / (double) (stats->endTime - stats->startTime) / meantransit) : NAN);
		if (stats->framelatency_histogram) {
		    histogram_print(stats->framelatency_histogram, stats->startTime, stats->endTime, stats->free);
		}
	    } else {
#ifdef HAVE_STRUCT_TCP_INFO_TCPI_TOTAL_RETRANS
	        double netpower = 0;
		if (stats->sock_callstats.write.rtt > 0) {
		  netpower = NETPOWERCONSTANT * (((double) bytesxfered / (double) (stats->endTime - stats->startTime)) / (1e-6 * stats->sock_callstats.write.rtt));
	        }
	        if (stats->sock_callstats.write.cwnd > 0) {
		  printf(report_bw_write_enhanced_format,
			 stats->transferID, stats->startTime, stats->endTime,
			 buffer, &buffer[sizeof(buffer)/2],
			 stats->sock_callstats.write.WriteCnt,
			 stats->sock_callstats.write.WriteErr,
			 stats->sock_callstats.write.TCPretry,
			 stats->sock_callstats.write.cwnd,
			 stats->sock_callstats.write.rtt,
			 netpower);
		} else {
		  printf(report_bw_write_enhanced_nocwnd_format,
			 stats->transferID, stats->startTime, stats->endTime,
			 buffer, &buffer[sizeof(buffer)/2],
			 stats->sock_callstats.write.WriteCnt,
			 stats->sock_callstats.write.WriteErr,
			 stats->sock_callstats.write.TCPretry,
			 stats->sock_callstats.write.rtt,
			 netpower);
		}
#endif
	    }
	}
    } else if ( stats->mUDP == (char)kMode_Client ) {
	// UDP Client reporting
	if( !header_printed ) {
	    if (stats->mIsochronous)
		printf( "%s", (stats->mEnhanced ? report_bw_pps_enhanced_isoch_header : report_bw_header));
	    else
	        printf( "%s", (stats->mEnhanced ? report_bw_pps_enhanced_header : report_bw_header));
	    header_printed = 1;
	}
	if (stats->mIsochronous)
	    printf( stats->mEnhanced ? report_bw_pps_enhanced_isoch_format : report_bw_format, stats->transferID,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2],
		    stats->sock_callstats.write.WriteCnt,
		    stats->sock_callstats.write.WriteErr,
		    (stats->IPGcnt ? (stats->IPGcnt / stats->IPGsum) : 0.0),
		    stats->isochstats.framecnt,
		    stats->isochstats.framelostcnt, stats->isochstats.slipcnt);
	else
	    printf( stats->mEnhanced ? report_bw_pps_enhanced_format : report_bw_format, stats->transferID,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2],
		    stats->sock_callstats.write.WriteCnt,
		    stats->sock_callstats.write.WriteErr,
		    (stats->IPGcnt ? (stats->IPGcnt / stats->IPGsum) : 0.0));

    } else {
        // UDP Server Reporting
        if( !header_printed ) {
	    if (isTripTime(stats)) {
	        if (stats->mIsochronous)
		    printf("%s", report_bw_jitter_loss_enhanced_isoch_header);
		else
		    printf("%s", report_bw_jitter_loss_enhanced_header);
	    } else {
	        printf("%s", report_bw_jitter_loss_header);
	    }
            header_printed = 1;
        }
	if (isTripTime(stats)) {
	    if (stats->IPGcnt) {
		// If the min latency is out of bounds of a realistic value
		// assume the clocks are not synched and suppress the
		// latency output
		if ((stats->transit.minTransit > UNREALISTIC_LATENCYMINMAX) ||
		    (stats->transit.minTransit < UNREALISTIC_LATENCYMINMIN)) {
		    printf( report_bw_jitter_loss_suppress_enhanced_format, stats->transferID,
			    stats->startTime, stats->endTime,
			    buffer, &buffer[sizeof(buffer)/2],
			    stats->jitter*1000.0, stats->cntError, stats->cntDatagrams,
			    (100.0 * stats->cntError) / stats->cntDatagrams,
			    (stats->IPGcnt / stats->IPGsum));
		} else {
		    char Lbuf[80];
		    double meantransit = (stats->transit.sumTransit / stats->transit.cntTransit);
		    // long burstavg =  (stats->transit.cntTransit ? ((int) bytesxfered / stats->transit.cntTransit) : 0);
		    // byte_snprintf(Lbuf, sizeof(Lbuf), (double) bytesxfered / stats->arrivalSum, 'a');
		    // printf("arrival rate = %f %s timesum = %f transit = %f\n", ((double) bytesxfered / stats->arrivalSum), Lbuf, stats->arrivalSum, meantransit);
		    double L  = ((stats->arrivalSum > 0.0) ? (((double) bytesxfered / stats->arrivalSum) * meantransit) : NAN);
		    byte_snprintf(Lbuf, sizeof(Lbuf), L, 'A');
		    if (stats->mIsochronous) {
			printf( report_bw_jitter_loss_enhanced_isoch_format, stats->transferID,
				stats->startTime, stats->endTime,
				buffer, &buffer[sizeof(buffer)/2],
				stats->jitter*1e3, stats->cntError, stats->cntDatagrams,
				(100.0 * stats->cntError) / stats->cntDatagrams,
				(meantransit * 1e3),
				stats->transit.minTransit*1e3,
				stats->transit.maxTransit*1e3,
				(stats->transit.cntTransit < 2) ? 0 : sqrt(stats->transit.m2Transit / (stats->transit.cntTransit - 1)) / 1e3,
				(stats->IPGcnt / stats->IPGsum),
				((stats->arrivalSum > 0.0) ? Lbuf : 0),
				((meantransit > 0.0) ? (NETPOWERCONSTANT * ((double) bytesxfered) / (double) (stats->endTime - stats->startTime) / meantransit) : 0),
				stats->isochstats.framecnt, stats->isochstats.framelostcnt);
		    } else {
			    printf( report_bw_jitter_loss_enhanced_format, stats->transferID,
			    stats->startTime, stats->endTime,
			    buffer, &buffer[sizeof(buffer)/2],
			    stats->jitter*1000.0, stats->cntError, stats->cntDatagrams,
			    (100.0 * stats->cntError) / stats->cntDatagrams,
			    (meantransit * 1e3),
			    stats->transit.minTransit*1e3,
			    stats->transit.maxTransit*1e3,
			    (stats->transit.cntTransit < 2) ? 0 : sqrt(stats->transit.m2Transit / (stats->transit.cntTransit - 1)) / 1e3,
			    (stats->IPGcnt / stats->IPGsum),
			    ((stats->arrivalSum > 0.0) ? Lbuf : 0),
			    (meantransit > 0.0) ? (NETPOWERCONSTANT * ((double) bytesxfered) / (double) (stats->endTime - stats->startTime) / meantransit) : 0);
		    }
		}
		if (stats->latency_histogram) {
		    histogram_print(stats->latency_histogram, stats->startTime, stats->endTime, stats->free);
		}
		if (stats->framelatency_histogram) {
		    histogram_print(stats->framelatency_histogram, stats->startTime, stats->endTime, stats->free);
		}
	    } else {
		printf( report_bw_jitter_loss_suppress_enhanced_format, stats->transferID,
			stats->startTime, stats->endTime,
			buffer, &buffer[sizeof(buffer)/2],
			0.0, stats->cntError,
			stats->cntDatagrams,
			0.0,0.0,0.0,0.0,0.0,0.0);
	    }
	} else {
	    printf( report_bw_jitter_loss_format, stats->transferID,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2],
		    stats->jitter*1000.0, stats->cntError, stats->cntDatagrams,
		    (100.0 * stats->cntError) / stats->cntDatagrams);
	}
	if ( stats->cntOutofOrder > 0 ) {
	    printf( report_outoforder,
		    stats->transferID, stats->startTime,
		    stats->endTime, stats->cntOutofOrder );
	}
	if (stats->l2counts.cnt) {
	    printf( report_l2statistics,
		    stats->transferID, stats->startTime,
		    stats->endTime, stats->l2counts.cnt, stats->l2counts.lengtherr,
		    stats->l2counts.udpcsumerr, stats->l2counts.unknown);
	}
    }

    if ( stats->free == 1) {
	if (stats->mUDP == (char)kMode_Client ) {
	    printf( report_datagrams, stats->transferID, stats->cntDatagrams );
	}
    }
}


/*
 * Prints multiple transfer reports in default style
 */
void reporter_multistats (struct TransferInfo *stats) {

    byte_snprintf( buffer, sizeof(buffer)/2, (double) stats->TotalLen,
                   toupper( (int)stats->mFormat));
    byte_snprintf( &buffer[sizeof(buffer)/2], sizeof(buffer)/2,
                   ((double) stats->TotalLen) / (stats->endTime - stats->startTime),
                   stats->mFormat);

    if (!stats->mEnhanced) {
	if (stats->mUDP == (char)kMode_Server) {
        // UDP Reporting
	    printf(report_sum_bw_jitter_loss_format,
                stats->startTime, stats->endTime,
                buffer, &buffer[sizeof(buffer)/2],
                stats->jitter*1000.0, stats->cntError, stats->cntDatagrams,
		   (100.0 * stats->cntError) / stats->cntDatagrams);
	} else {
        // TCP Reporting
	    printf(report_sum_bw_format,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2]);
	}
    } else {
	if (stats->mUDP == (char)kMode_Server) {
            // UDP Reporting
	    printf( report_sum_bw_pps_enhanced_format,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2],
		    stats->cntError, stats->cntDatagrams,
		    (stats->IPGcnt ? (stats->IPGcnt / stats->IPGsum) : 0.0));
	} else if (stats->mUDP) {
	    // UDP Enhanced Reporting
	    printf( report_sum_bw_pps_enhanced_format,
		    stats->startTime, stats->endTime,
		    buffer, &buffer[sizeof(buffer)/2],
		    stats->sock_callstats.write.WriteCnt,
		    stats->sock_callstats.write.WriteErr,
		    (stats->IPGcnt ? (stats->IPGcnt / stats->IPGsum) : 0.0));
	} else {
	    // TCP Enhanced Reporting
	    if (stats->mTCP == (char)kMode_Client) {
		printf( report_sum_bw_write_enhanced_format,
			stats->startTime, stats->endTime,
			buffer, &buffer[sizeof(buffer)/2],
			stats->sock_callstats.write.WriteCnt,
			stats->sock_callstats.write.WriteErr,
			stats->sock_callstats.write.TCPretry);
	    } else {
		printf( report_sum_bw_read_enhanced_format,
			stats->startTime, stats->endTime,
			buffer, &buffer[sizeof(buffer)/2],
			stats->sock_callstats.read.cntRead,
			stats->sock_callstats.read.bins[0],
			stats->sock_callstats.read.bins[1],
			stats->sock_callstats.read.bins[2],
			stats->sock_callstats.read.bins[3],
			stats->sock_callstats.read.bins[4],
			stats->sock_callstats.read.bins[5],
			stats->sock_callstats.read.bins[6],
			stats->sock_callstats.read.bins[7]);
	    }
	}
    }
    if ((stats->mUDP == kMode_Server) && stats->cntOutofOrder > 0 ) {
            printf( report_sum_outoforder,
                    stats->startTime,
                    stats->endTime, stats->cntOutofOrder );
    }
    if ((stats->free == 1) && (stats->mUDP == (char)kMode_Client)) {
        printf( report_sum_datagrams, stats->cntDatagrams );
    }
}


/*
 * Prints bidir sum transfer reports in default style
 */
void reporter_bidirstats (struct TransferInfo *stats) {
    byte_snprintf( buffer, sizeof(buffer)/2, (double) stats->TotalLen,
		   toupper( (int)stats->mFormat));
    byte_snprintf( &buffer[sizeof(buffer)/2], sizeof(buffer)/2,
		   ((double) stats->TotalLen) / (stats->endTime - stats->startTime),
		   stats->mFormat);

    // TCP Reporting
    if (!stats->mEnhanced) {
	printf(report_bw_sum_bidir_format, stats->transferID,
	       stats->startTime, stats->endTime,
	       buffer, &buffer[sizeof(buffer)/2]);
    } else {
	printf(report_bw_sum_bidir_enhanced_format, stats->transferID,
	       stats->startTime, stats->endTime,
	       buffer, &buffer[sizeof(buffer)/2]);
    }
}

void reporter_framestats_udp (struct TransferInfo *stats) {
    static char header_printed = 0;
    double bytesxfered;

    byte_snprintf( buffer, sizeof(buffer)/2, (double) stats->TotalLen,
                   toupper( (int)stats->mFormat));
    if (!stats->TotalLen || (stats->endTime < SMALLEST_INTERVAL_SEC)) {
        bytesxfered = 0;
    } else {
        bytesxfered = (double) stats->TotalLen;
    }
    byte_snprintf( &buffer[sizeof(buffer)/2], sizeof(buffer)/2,
                   (bytesxfered / (stats->endTime - stats->startTime)),
		   stats->mFormat);

    // UDP Server Reporting
    if( !header_printed ) {
	printf( "%s", report_frame_jitter_loss_enhanced_header);
	header_printed = 1;
    }
    if (stats->IPGcnt) {
	double frame_transit_time = stats->endTime - stats->startTime;
	// If the min latency is out of bounds of a realistic value
	// assume the clocks are not synched and suppress the
	// latency output
	if ((stats->transit.minTransit > UNREALISTIC_LATENCYMINMAX) ||
	    (stats->transit.minTransit < UNREALISTIC_LATENCYMINMIN)) {
	    printf( report_frame_jitter_loss_suppress_enhanced_format, stats->transferID,
		    stats->startTime, stats->endTime, frame_transit_time,
		    buffer, &buffer[sizeof(buffer)/2], stats->frameID,
		    stats->jitter*1000.0, stats->cntError, stats->cntDatagrams,
		    (100.0 * stats->cntError) / stats->cntDatagrams,
		    (stats->IPGcnt / stats->IPGsum));
	} else {
	    double meantransit = stats->transit.sumTransit / stats->transit.cntTransit;
	    printf( report_frame_jitter_loss_enhanced_format, stats->transferID,
		    stats->startTime, stats->endTime, frame_transit_time,
		    buffer, &buffer[sizeof(buffer)/2], stats->frameID,
		    stats->jitter*1e3, stats->cntError, stats->cntDatagrams,
		    (100.0 * stats->cntError) / stats->cntDatagrams,
		    (meantransit * 1e3),
		    stats->transit.minTransit*1e3,
		    stats->transit.maxTransit*1e3,
		    (stats->transit.cntTransit < 2) ? 0 : sqrt(stats->transit.m2Transit / (stats->transit.cntTransit - 1)) / 1e3,
		    (stats->IPGcnt / stats->IPGsum),
		    (meantransit > 0.0) ? (round((stats->IPGcnt / stats->IPGsum) * meantransit)) : 0,
		    ((meantransit > 0.0) ? (NETPOWERCONSTANT * ((double) bytesxfered) / (double) (stats->endTime - stats->startTime) / meantransit) : 0),
		    stats->isochstats.framecnt, stats->isochstats.framelostcnt);
	}
	if (stats->latency_histogram) {
	    histogram_print(stats->latency_histogram, stats->startTime, stats->endTime, stats->free);
	}
    } else {
	printf( report_frame_jitter_loss_suppress_enhanced_format, stats->transferID,
		stats->startTime, stats->endTime, 0.0,
		buffer, &buffer[sizeof(buffer)/2],
		0.0, stats->cntError,
		stats->cntDatagrams,
		0.0,0.0,0.0,0.0,0.0,0.0);
    }
    if ( stats->cntOutofOrder > 0 ) {
	printf( report_outoforder,
		stats->transferID, stats->startTime,
		stats->endTime, stats->cntOutofOrder );
    }
    if (stats->l2counts.cnt) {
	printf( report_l2statistics,
		stats->transferID, stats->startTime,
		stats->endTime, stats->l2counts.cnt, stats->l2counts.lengtherr,
		stats->l2counts.udpcsumerr, stats->l2counts.unknown);
    }
}

void reporter_framestats_tcp (struct TransferInfo *stats) {
    static char header_printed = 0;
    double bytesxfered;

    byte_snprintf( buffer, sizeof(buffer)/2, (double) stats->TotalLen,
                   toupper( (int)stats->mFormat));
    if (!stats->TotalLen || (stats->endTime < SMALLEST_INTERVAL_SEC)) {
        bytesxfered = 0;
    } else {
        bytesxfered = (double) stats->TotalLen;
    }
    byte_snprintf( &buffer[sizeof(buffer)/2], sizeof(buffer)/2,
                   (bytesxfered / (stats->endTime - stats->startTime)),
		   stats->mFormat);

    // TCP Frame Reporting
    if( !header_printed ) {
	printf( "%s", report_frame_tcp_enhanced_header);
	header_printed = 1;
    }
    if (stats->mTCP == (char)kMode_Client) {
      printf( report_sum_bw_write_enhanced_format,
	      stats->startTime, stats->endTime,
	      buffer, &buffer[sizeof(buffer)/2],
	      stats->sock_callstats.write.WriteCnt,
	      stats->sock_callstats.write.WriteErr,
	      stats->sock_callstats.write.TCPretry);
    } else {
      printf( report_sum_bw_read_enhanced_format,
	      stats->startTime, stats->endTime,
	      buffer, &buffer[sizeof(buffer)/2],
	      stats->sock_callstats.read.cntRead,
	      stats->sock_callstats.read.bins[0],
	      stats->sock_callstats.read.bins[1],
	      stats->sock_callstats.read.bins[2],
	      stats->sock_callstats.read.bins[3],
	      stats->sock_callstats.read.bins[4],
	      stats->sock_callstats.read.bins[5],
	      stats->sock_callstats.read.bins[6],
	      stats->sock_callstats.read.bins[7]);
    }
}


/*
 * Prints server transfer reports in default style
 */
void reporter_serverstats (struct ConnectionInfo *nused, struct TransferInfo *stats) {
    printf( server_reporting, stats->transferID );
    reporter_printstats(stats);
}

/*
 * Report the client or listener Settings in default style
 */
void reporter_reportsettings (struct ReporterData *data) {
    int pid =  (int)  getpid();

    printf( "%s", separator_line );
    switch (data->mThreadMode) {
    case kMode_Listener:
        printf(isEnhanced(data) ? server_pid_port : server_port,
                (isUDP( data ) ? "UDP" : "TCP"),
                data->mPort, pid );
	break;
    default:
        if (!data->mIfrnametx) {
            printf(isEnhanced(data) ? client_pid_port : client_port,
                data->mHost,
                (isUDP( data ) ? "UDP" : "TCP"),
                data->mPort, pid);
        } else {
            printf(client_pid_port_dev, data->mHost,
                  (isUDP( data ) ? "UDP" : "TCP"),
		  data->mPort, pid, data->mIfrnametx);
        }
	break;
    }

    if ( data->mLocalhost != NULL ) {
	if (isEnhanced(data) && !SockAddr_isMulticast(&data->connection.local)) {
	    if (data->mIfrname)
		printf(bind_address_iface, data->mLocalhost, data->mIfrname);
	    else
		printf( bind_address, data->mLocalhost);
	}
	if ((data->mThreadMode != kMode_Client) &&		\
	    SockAddr_isMulticast(&data->connection.local)) {
	    if(!data->mSSMMulticastStr)
		if (!data->mIfrname)
		    printf( join_multicast, data->mLocalhost );
		else
		    printf(join_multicast_starg_dev, data->mLocalhost,data->mIfrname);
	    else if(!data->mIfrname)
		printf( join_multicast_sg, data->mSSMMulticastStr, data->mLocalhost);
	    else
		printf( join_multicast_sg_dev, data->mSSMMulticastStr, data->mLocalhost, data->mIfrname);
        }
    }

    if (isIsochronous(data)) {
	char meanbuf[40];
	char variancebuf[40];
	byte_snprintf(meanbuf, sizeof(meanbuf), data->isochstats.mMean, 'a');
	byte_snprintf(variancebuf, sizeof(variancebuf), data->isochstats.mVariance, 'a');
	printf(client_udp_isochronous, data->isochstats.mFPS, meanbuf, variancebuf, (data->isochstats.mBurstInterval/1000.0), (data->isochstats.mBurstIPG/1000.0));
	if ((data->isochstats.mMean / data->isochstats.mFPS) < ((double) (sizeof(struct UDP_datagram) + sizeof(struct client_hdr_v1) + sizeof(struct client_hdr_udp_isoch_tests)))) {
	    fprintf(stderr, "Warning: Requested mean too small to carry isoch payload, code will auto adjust payload sizes\n");
	}
    } else {
	if ( isUDP( data ) ) {
	    if (data->mThreadMode != kMode_Listener) {
		double delay_target;
		if (data->mUDPRateUnits == kRate_BW) {
		    delay_target = (double) ( data->mBufLen * 8000000.0 / data->mUDPRate);
		} else {
		    delay_target = (1e6 / data->mUDPRate);
		}
#ifdef HAVE_CLOCK_NANOSLEEP
		printf(client_datagram_size, data->mBufLen, delay_target);
#else
  #ifdef HAVE_KALMAN
		printf(client_datagram_size_kalman, data->mBufLen, delay_target);
  #else
		printf(client_datagram_size, data->mBufLen, delay_target);
  #endif
#endif
	    } else {
		printf(server_datagram_size, data->mBufLen);
	    }
	    if ( SockAddr_isMulticast( &data->connection.peer ) ) {
		printf( multicast_ttl, data->info.mTTL);
	    }
	} else if (isEnhanced(data)) {
	    byte_snprintf( buffer, sizeof(buffer), data->mBufLen,
			   toupper( (int)data->info.mFormat));
	    printf("%s: %s\n", ((data->mThreadMode == kMode_Client) ?
				client_write_size : server_read_size), buffer);
	}
    }
    if (isFQPacing(data) && (data->mThreadMode == kMode_Client)) {
	char tmpbuf[40];
	byte_snprintf(tmpbuf, sizeof(tmpbuf), data->FQPacingRate, 'a');
	tmpbuf[39]='\0';
        printf(client_fq_pacing,tmpbuf);
    }
    byte_snprintf( buffer, sizeof(buffer), data->connection.winsize,	\
                   toupper( (int)data->info.mFormat));
    printf( "%s: %s", (isUDP( data ) ?
                                udp_buffer_size : tcp_window_size), buffer );
    if (data->connection.winsize_requested == 0 ) {
        printf( " %s", window_default );
    } else if ( data->connection.winsize != data->connection.winsize_requested ) {
        byte_snprintf( buffer, sizeof(buffer), data->connection.winsize_requested,
                       toupper( (int)data->info.mFormat));
	printf( warn_window_requested, buffer );
    }
    printf( "\n%s", separator_line );
}


/*
 * Report a socket's peer IP address in default style
 */
void *reporter_reportpeer (struct ConnectionInfo *stats, int ID) {
    if ( ID > 0 ) {
        // copy the inet_ntop into temp buffers, to avoid overwriting
        char local_addr[ REPORT_ADDRLEN ];
        char remote_addr[ REPORT_ADDRLEN ];
        struct sockaddr *local = ((struct sockaddr*)&stats->local);
        struct sockaddr *peer = ((struct sockaddr*)&stats->peer);

	char extbuf[2*PEERBUFSIZE];
	extbuf[(2*PEERBUFSIZE)-1] = '\0';
	char *b = &extbuf[0];
	extbuf[0]= '\0';
	if ((stats->winsize_requested) && (stats->winsize != stats->winsize_requested)) {
	      char tmpbuf[PEERBUFSIZE/4];
	      char tmpbuf2[PEERBUFSIZE/4];
	      tmpbuf2[PEERBUFSIZE/4-1]='\0';
	      tmpbuf[PEERBUFSIZE/4-1]='\0';
	      byte_snprintf(tmpbuf, sizeof(tmpbuf), stats->winsize, 'A');
	      byte_snprintf(tmpbuf2, sizeof(tmpbuf2), stats->winsize_requested, 'A');
	      snprintf(b, PEERBUFSIZE, " (WARN: winsize=%s req=%s)", tmpbuf, tmpbuf2);
	}
	b += strlen(b);
	if (isIsochronous(stats)) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (isoch)");
	    b += strlen(b);
	}
	if (isServerReverse(stats)) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (reverse)");
	    b += strlen(b);
	}
	if (isWriteAck(stats)) {
	  int spaceleft = PEERBUFSIZE-strlen(b);
	  if (spaceleft > 10) {
	    char tmpbuf[spaceleft];
	    byte_snprintf(tmpbuf, sizeof(tmpbuf), stats->WriteAckLen, 'A');
	    tmpbuf[spaceleft-1]='\0';
	    snprintf(b, PEERBUFSIZE-strlen(b), " (acks=%s)", tmpbuf);
	    b += strlen(b);
	  }
	}
	if (isTripTime(stats)) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (trip-times)");
	    b += strlen(b);
	    if (stats->mThreadMode == kMode_Server) {
		snprintf(b, PEERBUFSIZE-strlen(b), " (%ld.%ld)", \
			 stats->epochStartTime.tv_sec, (long) stats->epochStartTime.tv_usec);
		b += strlen(b);
	    }
	}
	if (stats->l2mode) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (%s=0x%X)", "l2mode", stats->l2mode);
	    b += strlen(b);
	}
	if (stats->peerversion) {
	    snprintf(b, PEERBUFSIZE-strlen(b), "%s", stats->peerversion);
	    b += strlen(b);
	}
	if (stats->connecttime > 0) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (ct=%4.2f ms)", stats->connecttime);;
	    b += strlen(b);
	}
	if (stats->txholdbacktime > 0) {
	    snprintf(b, PEERBUFSIZE-strlen(b), " (ht=%4.2f s)", stats->txholdbacktime);;
	}
        if ( local->sa_family == AF_INET ) {
            inet_ntop( AF_INET, &((struct sockaddr_in*)local)->sin_addr,
                       local_addr, REPORT_ADDRLEN);
        }
#ifdef HAVE_IPV6
          else {
            inet_ntop( AF_INET6, &((struct sockaddr_in6*)local)->sin6_addr,
                       local_addr, REPORT_ADDRLEN);
        }
#endif

        if ( peer->sa_family == AF_INET ) {
            inet_ntop( AF_INET, &((struct sockaddr_in*)peer)->sin_addr,
                       remote_addr, REPORT_ADDRLEN);
        }
#ifdef HAVE_IPV6
          else {
            inet_ntop( AF_INET6, &((struct sockaddr_in6*)peer)->sin6_addr,
                       remote_addr, REPORT_ADDRLEN);
        }
#endif
	 printf( report_peer,
                ID,
                local_addr,  ( local->sa_family == AF_INET ?
                              ntohs(((struct sockaddr_in*)local)->sin_port) :
#ifdef HAVE_IPV6
			      ntohs(((struct sockaddr_in6*)local)->sin6_port)),
#else
                              0),
#endif
                remote_addr, ( peer->sa_family == AF_INET ?
                              ntohs(((struct sockaddr_in*)peer)->sin_port) :
#ifdef HAVE_IPV6
			      ntohs(((struct sockaddr_in6*)peer)->sin6_port)),
#else
	                      0),
#endif
	         extbuf);
        if ((stats->epochStartTime.tv_sec) && (stats->mThreadMode == kMode_Client)) {
	    struct tm ts;
	    char start_timebuf[80];
#ifdef HAVE_CLOCK_GETTIME
	    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
	    ts = *localtime(&stats->epochStartTime.tv_sec);
	    strftime(start_timebuf, sizeof(start_timebuf), "%Y-%m-%d %H:%M:%S", &ts);
	    struct timespec t1;
	    clock_gettime(CLOCK_REALTIME, &t1);
	    ts = *localtime(&t1.tv_sec);
	    char now_timebuf[80];
	    strftime(now_timebuf, sizeof(now_timebuf), "%Y-%m-%d %H:%M:%S (%Z)", &ts);
	    printf(client_report_epoch_start_current, ID, start_timebuf, stats->epochStartTime.tv_sec, stats->epochStartTime.tv_usec, now_timebuf);
#else
	    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
	    ts = *localtime(&stats->epochStartTime.tv_sec);
	    strftime(start_timebuf, sizeof(start_timebuf), "%Y-%m-%d %H:%M:%S (%Z)", &ts);
	    printf(client_report_epoch_start, ID, start_timebuf, stats->epochStartTime.tv_sec, stats->epochStartTime.tv_usec);
#endif
	    fflush(stdout);
	}
    }
    return NULL;
}
// end ReportPeer

void reporter_peerversion (struct thread_Settings *inSettings, int upper, int lower) {
    int rel, major, minor, alpha;
    inSettings->peerversion[0] = '\0';

    rel = (upper & 0xFFFF0000) >> 16;
    major = (upper & 0x0000FFFF);
    minor = (lower & 0xFFFF0000) >> 16;
    alpha = (lower & 0x0000000F);
    sprintf(inSettings->peerversion," (peer %d.%d.%d)", rel, major, minor);
    switch(alpha) {
    case 0:
	sprintf(inSettings->peerversion + strlen(inSettings->peerversion) - 1,"-alpha)");
	break;
    case 1:
	sprintf(inSettings->peerversion + strlen(inSettings->peerversion) - 1,"-beta)");
	break;
    case 2:
	sprintf(inSettings->peerversion + strlen(inSettings->peerversion) - 1,"-rc)");
	break;
    case 3:
	break;
    default:
	sprintf(inSettings->peerversion + strlen(inSettings->peerversion) - 1, "-unk)");
    }
}
/* -------------------------------------------------------------------
 * Report the MSS and MTU, given the MSS (or a guess thereof)
 * ------------------------------------------------------------------- */

// compare the MSS against the (MTU - 40) to (MTU - 80) bytes.
// 40 byte IP header and somewhat arbitrarily, 40 more bytes of IP options.

#define checkMSS_MTU( inMSS, inMTU ) (inMTU-40) >= inMSS  &&  inMSS >= (inMTU-80)

void reporter_reportMSS (int inMSS, struct thread_Settings *inSettings) {
    if ( inMSS <= 0 ) {
        printf( report_mss_unsupported, inSettings->mSock );
    } else {
        char* net;
        int mtu = 0;

        if ( checkMSS_MTU( inMSS, 1500 ) ) {
            net = "ethernet";
            mtu = 1500;
        } else if ( checkMSS_MTU( inMSS, 4352 ) ) {
            net = "FDDI";
            mtu = 4352;
        } else if ( checkMSS_MTU( inMSS, 9180 ) ) {
            net = "ATM";
            mtu = 9180;
        } else if ( checkMSS_MTU( inMSS, 65280 ) ) {
            net = "HIPPI";
            mtu = 65280;
        } else if ( checkMSS_MTU( inMSS, 576 ) ) {
            net = "minimum";
            mtu = 576;
            printf( "%s", warn_no_pathmtu );
        } else {
            mtu = inMSS + 40;
            net = "unknown interface";
        }

        printf( report_mss,
                inSettings->mSock, inMSS, mtu, net );
    }
}
// end ReportMSS

#ifdef __cplusplus
} /* end extern "C" */
#endif
