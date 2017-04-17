/*
    An implementation of the basic block edge coverage
    tool for the COMS_E6156 Final Project - Andrew Calvano
*/
#include "pin.H"
#include <iostream>
#include <fstream>

struct ThreadBBInfo {
    THREADID thread_id;
    ADDRINT last_bb;
    struct ThreadBBInfo *next;
};

struct ThreadBBInfoHead {
    struct ThreadBBInfo *next;
    struct ThreadBBInfo *last;
};

static UINT32 total_threads = 0
static THREADID active_thread = 1;

static struct ThreadBBInfoHead *thread_list = 0;

struct BasicBlockRecord {
    ADDRINT bb_source;
    ADDRINT bb_dest;
    struct BasicBlockRecord *next;
};

struct BasicBlockRecordHead {
    struct BasicBlockRecord *next;
    struct BasicBlockRecord *last;
};

struct BasicBlockRecordHead *global_bbs = NULL;

static UINT32 start_address = 0;
static UINT32 end_address = 0;
std::string instrument_library_name;
std::string output_prefix;

std::ofstream mmap_file;

INT32 Usage()
{
    return -1;
}

VOID instrument_basic_block(ADDRINT bb_addr)
{
}

VOID trace(TRACE trace, VOID *v)
{
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) instrument_basic_block, IARG_ADDRINT, BBL_Address(bbl), IARG_END);
    }
}

VOID IMG_loaded(IMG image, VOID *v)
{
}

VOID IMG_unloaded(IMG image, VOID *v)
{
}

VOID thread_start(THREADID tid, CONTEXT *ctx, INT32 flags, VOID *v)
{
}

VOID thread_stop(THREADID tid, CONTEXT *ctx, INT32 flags, VOID *v)
{
}

VOID init()
{
}

VOID fini(INT32 code, VOID *v)
{
}

int main(int argc, char *argv[])
{
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    PIN_StartProgram();
    
    return 0;
}
