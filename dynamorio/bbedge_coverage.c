#include <stdint.h>
#include "dr_api.h"

struct ThreadBBInfo {
    thread_id_t thread_id;
    app_pc last_bb;
    struct ThreadBBInfo *next;
};

struct ThreadBBInfoHead {
    struct ThreadBBInfo *next;
    struct ThreadBBInfo *last;
};

uint32_t total_threads = 0;
uint32_t active_thread = 1;
static struct ThreadBBInfoHead *thread_list = 0;

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

FILE * mmap_fh = NULL;

static void
output_to_files(void)
{
    char *filename = (char *) "test_output.coverage";

    FILE *fh = fopen(filename, "w+"); 

    struct BasicBlockRecord *cur = global_bbs->next;

    while (cur != NULL)
    {
        fprintf(fh, "0x%08x,0x%08x\n", (uint32_t) cur->bb_source, (uint32_t) cur->bb_dest);
        cur = cur->next;
    }

    fclose(fh);
}

static struct ThreadBBInfo * find_tinfo_entry(thread_id_t tid)
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

static bool found_bb_record(app_pc source, app_pc dest)
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

static void
record_bb_edge(app_pc bb_addr)
{
    struct ThreadBBInfo *tinfo = find_tinfo_entry(active_thread);

    if (tinfo->last_bb && tinfo->last_bb != bb_addr &&
        !found_bb_record(tinfo->last_bb, bb_addr)
       )
    {
        dr_fprintf(STDOUT, "New observed block 0x%08x 0x%08x\n", tinfo->last_bb, bb_addr);

        struct BasicBlockRecord *new_record = dr_global_alloc(sizeof(struct BasicBlockRecord));

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

static void
handle_thread_init(void *drcontext)
{
    //Need to figure out how to get thread id from drcontext
    thread_id_t tid = dr_get_thread_id(drcontext);

    dr_fprintf(STDERR, "New thread id %d\n", (uint32_t) tid);

    if (tid >= total_threads)
    {
        if (thread_list == 0)
        {
            thread_list = dr_global_alloc(sizeof(struct ThreadBBInfoHead));
            struct ThreadBBInfo *new_tinfo = dr_global_alloc(sizeof(struct ThreadBBInfo));

            new_tinfo->thread_id = tid;
            new_tinfo->last_bb = 0;
            new_tinfo->next = 0;

            thread_list->next = new_tinfo;
            thread_list->last = new_tinfo;
        }
        else
        {
            struct ThreadBBInfo *new_tinfo = dr_global_alloc(sizeof(struct ThreadBBInfo));

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

static void
handle_thread_deinit(void *drcontext)
{
    //Need to figure out how to get thread id from drcontext
}

static void
handle_module_load(void *drcontext, const module_data_t *info, bool loaded)
{
}

static void
handle_module_unload(void *drcontext, const module_data_t *info)
{
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    (void) thread_list;
    dr_set_client_name("bbedge_coverage", "www.google.com");

    dr_register_exit_event(output_to_files);

    dr_register_bb_event(instrument_basic_block);

    //Intiialize global_bbs list of observed basic blocks
    global_bbs = dr_global_alloc(sizeof(struct BasicBlockRecordHead));

    global_bbs->next = NULL;
    global_bbs->last = NULL;

    //Register module load handler
    dr_register_module_load_event(handle_module_load);

    //Register module unload handler
    dr_register_module_unload_event(handle_module_unload);

    //Register thread initialization event for recording which thread
    dr_register_thread_init_event(handle_thread_init);

    //Register thread deinitialization event for recording which thread
    dr_register_thread_exit_event(handle_thread_deinit);

    dr_log(NULL, LOG_ALL, 1, "Client initialized\n");

    if (dr_is_notify_on()) {
        dr_fprintf(STDERR, "Client bbedge_coverage is running\n");
    }
}
