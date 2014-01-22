//---------------------------------------------------------- -*- Mode: C++ -*-
// $Id$
//
// Created 2014/01/21
//
// Author: Mike Ovsiannikov
//
// Copyright 2014 Quantcast Corp.
//
// This file is part of Kosmos File System (KFS).
//
// Licensed under the Apache License, Version 2.0
// (the "License"); you may not use this file except in compliance with
// the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
// \brief Meta server administration and monitoring utility.
//
//----------------------------------------------------------------------------

#include "tools/MonClient.h"
#include "common/MsgLogger.h"
#include "common/MdStream.h"
#include "qcdio/QCUtils.h"
#include "libclient/KfsOps.h"
#include "libclient/KfsClient.h"

#include <iostream>
#include <string>
#include <map>
#include <algorithm>
#include <iomanip>

#include <ctype.h>
#include <unistd.h>

namespace KFS_MON
{
using namespace KFS;
using namespace KFS::client;

using std::map;
using std::make_pair;
using std::string;
using std::cerr;
using std::cout;
using std::setw;

#define KfsForEachMetaOpId(f) \
    f(CHECK_LEASES,                    "debug: run chunk leases check") \
    f(RECOMPUTE_DIRSIZE,               "debug: recompute directories sizes") \
    f(DUMP_CHUNKTOSERVERMAP,           "create chunk server to chunk id map" \
                                       " file used by the off line" \
                                       " re-balance utility and layout" \
                                       " emulator" \
    ) \
    f(DUMP_CHUNKREPLICATIONCANDIDATES, "debug: list content of the chunks" \
                                       " re-replication and recovery queues" \
    ) \
    f(OPEN_FILES,                      "debug: list all chunk leases") \
    f(GET_CHUNK_SERVERS_COUNTERS,      "stats: output chunk server counters") \
    f(GET_CHUNK_SERVER_DIRS_COUNTERS,  "stats: output chunk directories" \
                                       " counters") \
    f(GET_REQUEST_COUNTERS,            "stats: get meta server request" \
                                       " counters")

    static string
ToLower(
    const char* inStr)
{
    string theRet;
    const char* thePtr = inStr;
    while (*thePtr) {
        theRet.push_back((char)tolower(*thePtr++ & 0xFF));
    }
    return theRet;
}

typedef map<
    string,
    pair<const char*, pair<KfsOp_t, const char*> >
> MetaAdminOps;

    static const pair<const MetaAdminOps&, size_t>&
MakeMetaAdminOpsMap()
{
    static MetaAdminOps sOps;
    static size_t       sMaxCmdNameLen = 0;
    if (sOps.empty()) {
        sMaxCmdNameLen = 0;
#define InsertAdminOpEntry(name, comment) \
        { \
            const string theName = ToLower(#name); \
            sOps[theName]  = make_pair(#name, \
                make_pair(CMD_META_##name, comment)); \
            sMaxCmdNameLen = max(sMaxCmdNameLen, theName.size()); \
        }
        KfsForEachMetaOpId(InsertAdminOpEntry)
#undef InsertAdminOpEntry
    }
    static pair<const MetaAdminOps&, size_t> theRet(sOps, sMaxCmdNameLen);
    return theRet;
}
const MetaAdminOps& sMetaAdminOpsMap   = MakeMetaAdminOpsMap().first;
const size_t        sMetaAdminOpMaxLen = MakeMetaAdminOpsMap().second;

    static void
CmdHelp(
    const char* inNamePtr)
{
    if (inNamePtr) {
        MetaAdminOps::const_iterator const theIt =
            sMetaAdminOpsMap.find(ToLower(inNamePtr));
        if (theIt == sMetaAdminOpsMap.end()) {
            cerr << "no such command: " << inNamePtr << "\n";
        } else {
            cout << theIt->first << " -- " <<
                theIt->second.second.second << "\n";
        }
    } else {
        for (MetaAdminOps::const_iterator theIt = sMetaAdminOpsMap.begin();
                theIt != sMetaAdminOpsMap.end();
                ++theIt) {
            cout << setw(sMetaAdminOpMaxLen) << theIt->first << " -- " <<
                theIt->second.second.second << "\n";
        }
    }
}

    static int
Main(
    int    inArgCount,
    char** inArgsPtr)
{
    bool        theHelpFlag           = false;
    const char* theServerPtr          = 0;
    const char* theConfigFileNamePtr  = 0;
    int         thePort               = -1;
    bool        theVerboseLoggingFlag = false;
    int         theRetCode            = 0;

    int theOptChar;
    while ((theOptChar = getopt(inArgCount, inArgsPtr, "hm:s:p:vf:")) != -1) {
        switch (theOptChar) {
            case 'm':
            case 's':
                theServerPtr = optarg;
                break;
            case 'p':
                thePort = atoi(optarg);
                break;
            case 'h':
                theHelpFlag = true;
                break;
            case 'v':
                theVerboseLoggingFlag = true;
                break;
            case 'f':
                theConfigFileNamePtr = optarg;
                break;
            default:
                theRetCode = 1;
                break;
        }
    }

    if (theHelpFlag || ! theServerPtr || thePort < 0) {
        (theHelpFlag ? cout : cerr) <<
            "Usage: " << inArgsPtr[0] << "\n"
            " -m|-s <meta server host name>\n"
            " -p <port>\n"
            " -f <config file name>\n"
            " [-v]\n"
            " --  <cmd> <cmd> ..."
            "Where cmd is one of the following:\n"
        ;
        CmdHelp(0);
        return (theHelpFlag ? theRetCode : 1);
    }

    MsgLogger::Init(0, theVerboseLoggingFlag ?
        MsgLogger::kLogLevelDEBUG : MsgLogger::kLogLevelINFO);

    const ServerLocation theLocation(theServerPtr, thePort);
    MonClient            theClient;
    if (theClient.SetParameters(theLocation, theConfigFileNamePtr) < 0) {
        return 1;
    }
    theClient.SetMaxContentLength(512 << 20);
    for (int i = optind; i < inArgCount; i++) {
        MetaAdminOps::const_iterator const theIt =
            sMetaAdminOpsMap.find(ToLower(inArgsPtr[i]));
        if (theIt == sMetaAdminOpsMap.end()) {
            cerr << "no such command: " << inArgsPtr[i] << "\n";
        } else {
            MetaMonOp theOp(
                theIt->second.second.first,
                theIt->second.first
            );
            const int theRet = theClient.Execute(theLocation, theOp);
            if (theRet < 0) {
                KFS_LOG_STREAM_ERROR << theOp.statusMsg <<
                    " error: " << ErrorCodeToStr(theRet) <<
                KFS_LOG_EOM;
                theRetCode = 1;
            } else {
                if (theOp.contentLength <= 0) {
                    cout << theIt->first << " OK\n";
                } else {
                    cout.write(theOp.contentBuf, theOp.contentLength);
                }
            }
        }
    }
    return theRetCode;
}

}

    int
main(
    int    inArgCount,
    char** inArgsPtr)
{
    return KFS_MON::Main(inArgCount, inArgsPtr);
}
