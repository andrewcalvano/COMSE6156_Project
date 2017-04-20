/*
    An implementation of the basic block edge coverage
    tool for the COMS_E6156 Final Project - Andrew Calvano
*/
#include "pin.H"
#include <stdio.h>
#include <iostream>
#include <fstream>

KNOB<string> KnobOutputPrefix(KNOB_MODE_WRITEONCE, "pintool", "p", "test_output", "Output prefix for mmap and coverage files.");
KNOB<string> KnobModuleName(KNOB_MODE_WRITEONCE, "pintool", "m", "", "Module name to instrument");
KNOB<UINT32> KnobStartAddress(KNOB_MODE_WRITEONCE, "pintool", "s", "0", "Start address to instrument");
KNOB<UINT32> KnobEndAddress(KNOB_MODE_WRITEONCE, "pintool", "e", "0xFFFFFFFF", "End address to instrument");

struct ThreadBBInfo {
    THREADID thread_id;
    ADDRINT last_bb;
    struct ThreadBBInfo *next;
};

struct ThreadBBInfoHead {
    struct ThreadBBInfo *next;
    struct ThreadBBInfo *last;
};

static UINT32 total_threads = 0;
static THREADID active_thread = 0;

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
static UINT32 end_address = 0xFFFFFFFF;
std::string instrument_library_name;
std::string output_prefix;

//std::ofstream mmap_file;
FILE *mmap_fh = NULL;

static struct ThreadBBInfo * find_tinfo_entry(THREADID tid)
{
    struct ThreadBBInfo *cur;

    cur = thread_list->next;

    while (cur != NULL)
    {
        if (cur->thread_id == tid)
            return cur;

        cur = cur->next;
    }

    return NULL;
}

static bool found_bb_record(ADDRINT source, ADDRINT dest)
{
    struct BasicBlockRecord *cur;

    cur = global_bbs->next;
    while (cur != NULL)
    {

        if (source == cur->bb_source && dest == cur->bb_dest)
        {
            //VG_(printf)("Already covered pair 0x%08x,0x%08x\n", source, dest);
            return 1;
        }

        cur = cur->next; 
    }

    return 0;
}

VOID instrument_basic_block(ADDRINT bb_addr)
{
    struct ThreadBBInfo *tinfo = find_tinfo_entry(active_thread);

    if (tinfo->last_bb && tinfo->last_bb != bb_addr &&
        !found_bb_record(tinfo->last_bb, bb_addr)
       )
    {
        struct BasicBlockRecord *new_record = (struct BasicBlockRecord *) malloc(sizeof(struct BasicBlockRecord));

        if (global_bbs->last != NULL)
            global_bbs->last->next = new_record;

        if (global_bbs->next == NULL)
            global_bbs->next = new_record;

        global_bbs->last = new_record;

        new_record->bb_source = tinfo->last_bb;
        new_record->bb_dest = bb_addr;
        new_record->next = NULL;
    }
    tinfo->last_bb = bb_addr;
}

VOID trace(TRACE trace, VOID *v)
{
    bool module_instrument = KnobModuleName.Value() != "none";

    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        ADDRINT bb_addr = BBL_Address(bbl);

        if (!module_instrument && (bb_addr >= start_address && bb_addr <= end_address))
        {
            BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) instrument_basic_block, IARG_ADDRINT, bb_addr, IARG_END);
        }
        else if ((start_address != 0 && end_address != 0xFFFFFFFF) &&
                 (bb_addr >= start_address && bb_addr <= end_address)
                )
        {
            BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) instrument_basic_block, IARG_ADDRINT, bb_addr, IARG_END);
        }
    }
}

VOID image_loaded(IMG image, VOID *v)
{
    const std::string img_name = IMG_Name(image);

    //char *fname_ptr = strrchr(img_name.c_str(), '/'); 

    //cerr << std::string(fname_ptr+1) << " has been loaded" << endl;

    uint32_t start = IMG_LowAddress(image);

    uint32_t end = IMG_HighAddress(image);

    std::string::size_type pos = img_name.rfind('/');

    if (pos != std::string::npos)
    {
        pos += 1;

        std::string test_string = img_name.substr(pos);

        if (test_string == KnobModuleName.Value())
        {
            cerr << "Found module to instrument ";
            cerr << "at start " << start;
            cerr << " at end " << end << endl;
            start_address = start;
            end_address = end;
        }
    }

    fprintf(mmap_fh, "%s,0x%08x,0x%08x\n", img_name.c_str(), start, end);
}

VOID image_unloaded(IMG image, VOID *v)
{
}

VOID thread_start(THREADID tid, CONTEXT *ctx, INT32 flags, VOID *v)
{
    std::cout << "ThreadID " << tid << " started." << std::endl;
    if ( tid >= total_threads )
    {
        if (thread_list == 0)
        {
            thread_list = (struct ThreadBBInfoHead *) malloc(sizeof(struct ThreadBBInfoHead));

            struct ThreadBBInfo *new_tinfo = (struct ThreadBBInfo *) malloc(sizeof(struct ThreadBBInfo));

            new_tinfo->thread_id = tid;
            new_tinfo->last_bb = 0;
            new_tinfo->next = 0;

            thread_list->next = new_tinfo;
            thread_list->last = new_tinfo;
        }
        else
        {
            struct ThreadBBInfo *new_tinfo = (struct ThreadBBInfo *) malloc(sizeof(struct ThreadBBInfo));

            new_tinfo->thread_id = tid;
            new_tinfo->last_bb = 0;
            new_tinfo->next = 0;

            thread_list->last->next = new_tinfo;
            thread_list->last = new_tinfo;
        }
        total_threads = tid + 1;
    }
    active_thread = tid;
}

VOID thread_stop(THREADID tid, const CONTEXT *ctx, INT32 flags, VOID *v)
{
    std::cout << "ThreadID " << tid << " stopped." << std::endl;
}

VOID fini(INT32 code, VOID *v)
{
    //Write observed basic block information to the output file
    //const char *filename = "test_output.coverage";
    std::string filename = KnobOutputPrefix.Value() + std::string(".coverage");

    FILE *fh = fopen(filename.c_str(), "w+");

    struct BasicBlockRecord *cur = global_bbs->next;

    while (cur != NULL)
    {
        fprintf(fh, "0x%08x,0x%08x\n", (uint32_t) cur->bb_source, (uint32_t) cur->bb_dest);
        cur = cur->next;
    }

    fclose(fh);

    fclose(mmap_fh);
}

INT32 Usage()
{
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << std::endl;
    return -1;
}

int main(int argc, char *argv[])
{
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    start_address = KnobStartAddress.Value();
    end_address = KnobEndAddress.Value();

    if (KnobModuleName.Value() != std::string("none"))
    {
        if (start_address != 0 && end_address != 0xFFFFFFFF)
        {
            cerr << "Can not instrument a module name and an address range yet." << endl;
            return 0;
        }
    }

    PIN_AddThreadStartFunction(thread_start, NULL);

    PIN_AddThreadFiniFunction(thread_stop, NULL);

    TRACE_AddInstrumentFunction(trace, 0);

    IMG_AddInstrumentFunction(image_loaded, NULL);

    IMG_AddUnloadFunction(image_unloaded, NULL);

    PIN_AddFiniFunction(fini, 0);

    //Allocate observed basic block list
    global_bbs = (struct BasicBlockRecordHead *) malloc(sizeof(struct BasicBlockRecordHead));

    global_bbs->next = NULL;
    global_bbs->last = NULL;

    //const char *mmap_filename = "test_output.mmap";
    std::string mmap_filename = KnobOutputPrefix.Value() + std::string(".mmap");

    mmap_fh = fopen(mmap_filename.c_str(), "wb+");
    
    PIN_StartProgram();
    
    return 0;
}
