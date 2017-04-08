/*--------------------------------------------------------------------*/
/*--- BBedgegrind: A valgrind tool for recording                   ---*/
/*---              basic block edge transition coverage.           ---*/ 
/*---                                                   bbe_main.c ---*/
/*--------------------------------------------------------------------*/

#include "pub_tool_basics.h"
#include "pub_tool_vki.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_addrinfo.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"

//Probably should also have a program counter to know when
//the record appeared, if there is an munmap and then a mmap
//at a later time you would want to create a new basic block
//record

/* This structure is used to create transitions
   from separeated basic block executions
   resulting from thread scheduling and context
   switching.
*/
struct ThreadBBInfo {
    Int thread_id;
    Addr last_bb;
    struct ThreadBBInfo *next;
};

struct ThreadBBInfoHead {
    struct ThreadBBInfo *next;
    struct ThreadBBInfo *last;
};

ULong total_threads = 0;
ThreadId active_thread = 1;

static struct ThreadBBInfoHead *thread_list = 0;

struct BasicBlockRecord {
    Addr bb_source;
    Addr bb_dest;
    struct BasicBlockRecord *next;
};

struct BasicBlockRecordHead {
      struct BasicBlockRecord *next;
      struct BasicBlockRecord *last;	
};

struct BasicBlockRecordHead *global_bbs = NULL;

static Int clo_start_address = 0;
static Int clo_end_address = 0xFFFFFFFF;
static const char *clo_library_name = 0;
static const char *clo_output_prefix = 0;

VgFile *mmap_fh = 0;

static Bool bbe_process_cmd_line_option(const HChar* arg)
{
    if( VG_BHEX_CLO(arg, "--start_address", clo_start_address, 0x00, 0xFFFFFFFF)) {}
    else if (VG_BHEX_CLO(arg, "--end_address", clo_end_address, 0x00, 0xFFFFFFFF)) {}
    else if (VG_STR_CLO(arg, "--library_name", clo_library_name)) {}
    else if (VG_STR_CLO(arg, "--output_prefix", clo_output_prefix)) {}
    else
        return False;

    //Ensure clo_start_address and clo_end_address are not supplied if clo_library_name is supplied
    //and vice versa
    if (clo_library_name)
        tl_assert((clo_start_address == 0x00 && clo_end_address == 0xFFFFFFFF));

    return True;
}

static void bbe_print_usage(void)
{
    VG_(printf)(
"    --start_address=<hex_address>       specifies lower limit of addresses to collect coverage for.\n"
"    --end_address=<hex_address>         specifies upper limit of addresses to collect coverage for.\n"
"    --output_prefix=<name>              specifies prefix to dump coverage and mmap data to.\n"
    );
}

static void bbe_print_debug_usage(void)
{
    VG_(printf)(
"    no debug info print usage.\n"
    );
}

static void bbe_thread_call( ThreadId tid, ULong disp )
{
    if (tid >= total_threads) 
    {
        //Need to allocate a new thread structure to hold last
        //bb address
	if (thread_list == 0)
	{
          thread_list = VG_(malloc)("thread_head", sizeof(struct ThreadBBInfoHead));
	  struct ThreadBBInfo *new_tinfo = VG_(malloc)("thread", sizeof(struct ThreadBBInfo));

	  new_tinfo->thread_id = tid;
	  new_tinfo->last_bb = 0;
          new_tinfo->next = 0;

          thread_list->next = new_tinfo;
          thread_list->last = new_tinfo;
	}
        else
        {
 	  struct ThreadBBInfo *new_tinfo = VG_(malloc)("thread", sizeof(struct ThreadBBInfo));

          new_tinfo->thread_id = tid;
          new_tinfo->last_bb = 0; 
          new_tinfo->next = 0;

          thread_list->last->next = new_tinfo;
          thread_list->last = new_tinfo;
        }

        total_threads = tid + 1;
    }
    VG_(printf)("New thread called with tid %d\n", (ULong) tid);
    active_thread = tid;
}

static Bool found_bb_record(Addr source, Addr dest)
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

static struct ThreadBBInfo * find_tinfo_entry(ThreadId tid)
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

static void bbe_bb_instrument(Addr addr)
{
  //VG_(printf)("Analyzing at addr: 0x%08x\n", addr);
  struct ThreadBBInfo *tinfo = find_tinfo_entry(active_thread);

  tl_assert(tinfo != NULL);

  if (tinfo->last_bb && tinfo->last_bb != addr && !found_bb_record(tinfo->last_bb, addr))
  {
    //VG_(printf)("Creating new basic block record for 0x%08x\n", addr);
    struct BasicBlockRecord *new_record = VG_(malloc)("block", sizeof(struct BasicBlockRecord));

    if (global_bbs->last != NULL)
      global_bbs->last->next = new_record;

    if (global_bbs->next == NULL)
      global_bbs->next = new_record;

    global_bbs->last = new_record;

    VG_(printf)("Testing pair 0x%08x,0x%08x\n", tinfo->last_bb, addr);

    new_record->bb_source = tinfo->last_bb;
    new_record->bb_dest = addr;
    new_record->next = NULL;
  }

  tinfo->last_bb = addr;
}

static void bbe_post_clo_init(void)
{
  global_bbs = (struct BasicBlockRecordHead *) VG_(malloc)("block_head", sizeof(struct BasicBlockRecordHead));

  global_bbs->next = NULL;
  global_bbs->last = NULL;

  char *mmap_filename = (char *) "test_output.mmap";

  if (clo_output_prefix != 0)
  {
    mmap_filename = VG_(malloc)("filename", (VG_(strlen)(clo_output_prefix) + VG_(strlen)(".mmap") + 1));
    VG_(memset)(mmap_filename, 0x00, VG_(strlen)(clo_output_prefix) + VG_(strlen)(".mmap") + 1);
    VG_(strncpy)(mmap_filename, clo_output_prefix, VG_(strlen)(clo_output_prefix));
    VG_(strncat)(mmap_filename, ".mmap", VG_(strlen)(".mmap"));
  }

  mmap_fh = VG_(fopen)(mmap_filename, VKI_O_CREAT|VKI_O_WRONLY, VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP | VKI_S_IWGRP);

}

static void bbe_new_mem_mmap(Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle )
{
  AddrInfo test;

  VG_(memset)(&test, 0x00, sizeof(AddrInfo));
  VG_(printf)("New mmap event at address 0x%08x of length 0x%08x\n", a, len);

  VG_(describe_addr)( a, &test);

  VG_(pp_addrinfo)( a, &test);

  if (test.Addr.SegmentKind.segkind == SkFileC && test.Addr.SegmentKind.hasX)
  {
     VG_(fprintf)(mmap_fh, "%s,0x%08x,0x%08x\n", test.Addr.SegmentKind.filename, a, len);

     //If segment is executable and corresponds to a mapped file (not JIT) then we 
     //should record this so that we know which basic blocks correspond to which code
     //in which file. At termination we can write everything to a file and then do mmap
     //relations offline by having an mmap file or by emitting separate files for each
     //maped code file loaded and executed.

     //We need to extract filenames for mmap executable regions and write this to a separate
     //output file.
     //Allow comparison against library names to grab addresses to instrument
     //This is useful for PIE binaries and shared libraries
     //
     //Also make sure the user is not currently instrumeting a range
     //if (clo_start_address == 0x00 && clo_end_address == 0xFFFFFFFF)
     //{
     if (clo_library_name != NULL)
     {
     	char *fname_ptr = VG_(strrchr)(test.Addr.SegmentKind.filename, '/');
     	VG_(printf)("Filename without path %s\n", fname_ptr + 1);

        if(!VG_(strncmp)(fname_ptr+1, clo_library_name, VG_(strlen(clo_library_name))))
        {
			if (clo_start_address == 0 && clo_end_address == 0xFFFFFFFF)
			{
				VG_(printf)("No instrumentation before now");
				clo_start_address = a;
				clo_end_address = a + len;
			}

			//There are some cases left out here that we should probably handle but
			//ignore for now.
		}
     }          
  }
}

static void bbe_die_mem_munmap( Addr a, SizeT len )
{
  VG_(printf)("New munmap call on addr 0x%08x of size 0x%08x\n", a, len);
}

static
IRSB* bbe_instrument ( VgCallbackClosure* closure,
                      IRSB* bb,
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
    Int    i;
    IRSB * sbOut;
    IRDirty *di;
    IRExpr **argv;

    sbOut = deepCopyIRSBExceptStmts(bb);

    i = 0;
    while (i < bb->stmts_used && bb->stmts[i]->tag != Ist_IMark) {
        addStmtToIRSB( sbOut, bb->stmts[i] );
        i++;
    }

    //Print address of super block
    //VG_(printf)("Testing in translation routine 0x%08x\n", bb->stmts[i]->Ist.IMark.addr);
    //VG_(printf)("Number of IR statements in basic block %d\n", bb->stmts_used);

    Addr test_addr = bb->stmts[i]->Ist.IMark.addr;

    //If clo_library_name is set we need to wait until clo_start_address and clo_end_address
    //are set by waiting for the MMAP to occur
    VG_(printf)("Testing expression 1 %d\n", (clo_library_name && (clo_start_address != 0x00 && clo_end_address != 0xFFFFFFFF) &&
        (test_addr >= clo_start_address && test_addr <= clo_end_address)));
    VG_(printf)("Testing expression 2 %d\n", ((!clo_library_name && (test_addr >= clo_start_address && test_addr <= clo_end_address))));
    if ((clo_library_name && (clo_start_address != 0x00 && clo_end_address != 0xFFFFFFFF) &&
        (test_addr >= clo_start_address && test_addr <= clo_end_address)) ||
       ((!clo_library_name && (test_addr >= clo_start_address && test_addr <= clo_end_address)))
       )
    {
      argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord) bb->stmts[i]->Ist.IMark.addr));

      di = unsafeIRDirty_0_N( 0, "bbe_bb_instrument",
			      VG_(fnptr_to_fnentry( &bbe_bb_instrument) ),
			      argv);

      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
    }

    for(; i < bb->stmts_used; i++)
    {
       IRStmt* st = bb->stmts[i];
       if(!st || st->tag == Ist_NoOp) continue;

       switch(st->tag)
       {
           case Ist_NoOp:
           case Ist_AbiHint:
           case Ist_Put:
           case Ist_PutI:
           case Ist_MBE:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_IMark: 

              addStmtToIRSB( sbOut, st );
              break;

           case Ist_WrTmp:
              addStmtToIRSB( sbOut, st );
              break;
           case Ist_Store:
              addStmtToIRSB( sbOut, st );  	
              break;

           case Ist_StoreG:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_LoadG:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_Dirty:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_CAS:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_LLSC:
              addStmtToIRSB( sbOut, st );
              break;

           case Ist_Exit:
              addStmtToIRSB( sbOut, st );
              break;

           default:
              ppIRStmt(st);
              tl_assert(0);
       }
    }

    return sbOut;
}

static void bbe_fini(Int exitcode)
{
  char *filename = (char *) "test_output.coverage";

  if (clo_output_prefix != 0)
  {
    filename = VG_(malloc)("filename", (VG_(strlen)(clo_output_prefix) + VG_(strlen)(".coverage") + 1));
    VG_(memset)((char *) filename, 0x00, VG_(strlen)(clo_output_prefix) + VG_(strlen)(".coverage") + 1);
    VG_(strncpy)((char *) filename, clo_output_prefix, VG_(strlen)(clo_output_prefix));
    VG_(strncat)((char *) filename, ".coverage", VG_(strlen)(".coverage"));
  }

  VgFile *fp = VG_(fopen)(filename, VKI_O_CREAT|VKI_O_WRONLY, VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP | VKI_S_IWGRP);

  struct BasicBlockRecord *cur = global_bbs->next;

  while (cur != NULL)
  {
    VG_(fprintf)(fp, "0x%08x,0x%08x\n", cur->bb_source, cur->bb_dest);
    cur = cur->next;
  }

  VG_(fclose)(fp);

  //Close the mmap_fh
  VG_(fclose)(mmap_fh);
}

static void bbe_pre_clo_init(void)
{
   VG_(details_name)            ("BBEdgegrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("Valgrind Tool To Monitor Basic Block Edge Coverage");
   VG_(details_copyright_author)(
      "Andrew Calvano");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        (bbe_post_clo_init,
                                 bbe_instrument,
                                 bbe_fini);

   VG_(track_new_mem_mmap) ( bbe_new_mem_mmap );

   //Should really keep tabs on VG_(track_die_mem_munmap) also to see if files are ever unloaded from memory
   VG_(track_die_mem_munmap) ( bbe_die_mem_munmap );

   VG_(needs_command_line_options) (bbe_process_cmd_line_option,
               			  bbe_print_usage,
                                  bbe_print_debug_usage);

   //Thread Related Callbacks
   VG_(track_start_client_code)( bbe_thread_call );
}

VG_DETERMINE_INTERFACE_VERSION(bbe_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
