/*---------------------------------------------------------------
 * Copyright (c) 2019
 * Broadcom Corporation
 * All Rights Reserved.
 *---------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated
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
 * Neither the name of Broadcom Coporation,
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
 *
 * packet_ring.h
 * Suppport for packet rings between threads
 *
 * by Robert J. McMahon (rjmcmahon@rjmcmahon.com, bob.mcmahon@broadcom.com)
 * -------------------------------------------------------------------
 */
#ifndef PACKETRINGC_H
#define PACKETRINGC_H

#include "Condition.h"

typedef struct ReportStruct {
    intmax_t packetID;
    intmax_t packetLen;
    struct timeval packetTime;
    struct timeval sentTime;
    int errwrite;
    int emptyreport;
    int socket;
    int l2errors;
    int l2len;
    int expected_l2len;
    struct timeval isochStartTime;
    intmax_t prevframeID;
    intmax_t frameID;
    intmax_t burstsize;
    intmax_t burstperiod;
    intmax_t remaining;
} ReportStruct;

typedef struct PacketRing {
  // producer and consumer
  // must be an atomic type, e.g. int
  // otherwise reads/write can be torn
  int producer;
  int consumer;
  int maxcount;
  int consumerdone;
  int awaitcounter;
  ReportStruct metapacket;

  // Use a condition variables
  // o) await_consumer - producer waits for the consumer thread to
  //    make space or end (signaled by the consumer)
  // o) awake_consumer - signal the consumer thread to to run
  //    (signaled by the producer)
  Condition await_consumer;
  Condition *awake_consumer;
  ReportStruct *data;
} PacketRing;

extern PacketRing * init_packetring (int count, Condition *awake_consumer);
extern void enqueue_packetring(PacketRing *pr, ReportStruct *metapacket);
extern ReportStruct *dequeue_packetring(PacketRing * pr);
#ifdef HAVE_THREAD_DEBUG
extern int getcount_packetring(PacketRing *pr);
#endif
#endif