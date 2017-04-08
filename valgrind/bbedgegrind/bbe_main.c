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

struct BasicBlockRecord {
      Addr bb_source;
      struct BasicBlockRecord *next;
};

struct BasicBlockRecordHead {
      struct BasicBlockRecord *next;
      struct BasicBlockRecord *last;	
};

struct BasicBlockRecordHead *global_bbs = NULL;


static Int clo_start_address = 0;
static Int clo_end_address = 0xFFFFFFFF;
static const char *clo_output_prefix = 0;

static Bool bbe_process_cmd_line_option(const HChar* arg)
{
    if( VG_BHEX_CLO(arg, "--start_address", clo_start_address, 0x00, 0xFFFFFFFF)) {}
    else if (VG_BHEX_CLO(arg, "--end_address", clo_end_address, 0x00, 0xFFFFFFFF)) {}
    else if (VG_STR_CLO(arg, "--output_prefix", clo_output_prefix)) {}
    else
        return False;

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

static Bool found_bb_record(Addr addr)
{
  struct BasicBlockRecord *cur;

  cur = global_bbs->next;
  while (cur != NULL)
  {

    if (addr == cur->bb_source)
    {
      VG_(printf)("Already covered address 0x%08x\n", addr);
      return 1;
    }

    cur = cur->next; 
  }

  return 0;
}

static void bbe_bb_instrument(Addr addr)
{
  VG_(printf)("Analyzing at addr: 0x%08x\n", addr);
  if (global_bbs == NULL)
  {
      global_bbs = (struct BasicBlockRecordHead *) VG_(malloc)("block_head", sizeof(struct BasicBlockRecordHead));
      struct BasicBlockRecord *first_bb = VG_(malloc)("block", sizeof(struct BasicBlockRecord));

      first_bb->next = NULL;
      first_bb->bb_source = addr;

      global_bbs->next = first_bb;
      global_bbs->last = first_bb;
  }
  else
  {
    Bool result = found_bb_record(addr);

    if (!found_bb_record(addr))
    {
      VG_(printf)("Creating new basic block record for 0x%08x\n", addr);
      struct BasicBlockRecord *new_record = VG_(malloc)("block", sizeof(struct BasicBlockRecord));
      global_bbs->last->next = new_record;
      global_bbs->last = new_record;

      new_record->bb_source = addr;
      new_record->next = NULL;
    }
  }
}

static void bbe_post_clo_init(void)
{
}

static void bbe_new_mem_mmap(Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle )
{
  AddrInfo test;

  VG_(memset)(&test, 0x00, sizeof(AddrInfo));
  VG_(printf)("New mmap event at address 0x%08x of length 0x%08x\n", a, len);

  VG_(describe_addr)( a, &test);

  VG_(pp_addrinfo)( a, &test);

  //If segment is executable and corresponds to a mapped file (not JIT) then we 
  //should record this so that we know which basic blocks correspond to which code
  //in which file. At termination we can write everything to a file and then do mmap
  //relations offline by having an mmap file or by emitting separate files for each
  //maped code file loaded and executed.
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
    VG_(printf)("Testing in translation routine 0x%08x\n", bb->stmts[i]->Ist.IMark.addr);
    VG_(printf)("Number of IR statements in basic block %d\n", bb->stmts_used);

    Addr test_addr = bb->stmts[i]->Ist.IMark.addr;

    if (test_addr >= clo_start_address && test_addr <= clo_end_address)
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
  const char *filename = "test_output";

  if (clo_output_prefix != 0)
    filename = clo_output_prefix;

  VgFile *fp = VG_(fopen)(filename, VKI_O_CREAT|VKI_O_WRONLY, VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP | VKI_S_IWGRP);

  struct BasicBlockRecord *cur = global_bbs->next;

  while (cur != NULL)
  {
    VG_(fprintf)(fp, "0x%08x\n", cur->bb_source);
    cur = cur->next;
  }

  VG_(fclose)(fp);
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
}

VG_DETERMINE_INTERFACE_VERSION(bbe_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
