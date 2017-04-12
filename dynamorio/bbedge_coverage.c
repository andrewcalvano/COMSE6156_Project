#include <stdint.h>
#include "dr_api.h"

struct BasicBlockRecord {
    app_pc bb_source;
    app_pc bb_dest;
    struct BasicBlockRecord *next;
};

struct BasicBlockRecordHead {
    struct BasicBlockRecord *next;
    struct BasicBlockRecord *last;
};

struct BasicBlockRecordHead *global_bbs = NULL;

static void
output_to_files(void)
{
}

static void
record_bb_edge(app_pc bb_addr)
{
    dr_fprintf(STDOUT, "Analysis bb address: 0x%08x\n", bb_addr);
}

static dr_emit_flags_t
instrument_basic_block(void *drcontext, void *tag, instrlist_t *bb,
                       bool for_trace, bool translating
                      )
{
    //Need to insert a call to our stub to manage our linked list of 
    //observed basic block transitions
    dr_insert_clean_call(drcontext, bb, instrlist_first(bb),
                         (void*)record_bb_edge,
                         false,
                         1,
                         OPND_CREATE_INT32(dr_fragment_app_pc(tag))
                        );

    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_set_client_name("bbedge_coverage", "www.google.com");

    dr_register_exit_event(output_to_files);

    dr_register_bb_event(instrument_basic_block);

    //Intiialize global_bbs list of observed basic blocks
    global_bbs = dr_global_alloc(sizeof(struct BasicBlockRecordHead));

    dr_log(NULL, LOG_ALL, 1, "Client initialized\n");

    if (dr_is_notify_on()) {
        dr_fprintf(STDERR, "Client bbedge_coverage is running\n");
    }
}
