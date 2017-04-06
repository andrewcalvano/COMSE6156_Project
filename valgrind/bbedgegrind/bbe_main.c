
/*--------------------------------------------------------------------*/
/*--- BBedgegrind: A valgrind tool for recording                   ---*/
/*---              basic block edge transition coverage.           ---*/ 
/*---                                                   bbe_main.c ---*/
/*--------------------------------------------------------------------*/

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"

static void bbe_post_clo_init(void)
{
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

    sbOut = deepCopyIRSBExceptStmts(bb);

    i = 0;
    while (i < bb->stmts_used && bb->stmts[i]->tag != Ist_IMark) {
        addStmtToIRSB( sbOut, bb->stmts[i] );
        i++;
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

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(bbe_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
