#include <stdint.h>
#include <string.h>
#include <stdlib.h>
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

app_pc instrument_start_address = 0;
app_pc instrument_end_address = 0;

const char *instrument_module_name = NULL;

const char *output_prefix = NULL;

static void
parse_arguments(int argc, const char *argv[])
{
    const char *arg = NULL;
    /*
        Valid arguments are 
            -start_address 0xaddress
            -end_address 0xaddress
            -module_name module_name
            -prefix output_prefix
    */
    for (int i = 1; i < argc; i++)
    {
        arg = argv[i];
        if(!strcmp(arg, "-start_address"))
        {
            instrument_start_address = (app_pc) strtoul(argv[i+1], NULL, 16);
            dr_fprintf(STDERR, "Only instrumenting addresses after 0x%08x\n", instrument_start_address);
            i++;
        }
        else if (!strcmp(arg, "-end_address"))
        {
            instrument_end_address = (app_pc) strtoul(argv[i+1], NULL, 16);
            dr_fprintf(STDERR, "Only instrumenting addresses before 0x%08x\n", instrument_end_address);
            i++; 
        }
        else if (!strcmp(arg, "-module_name"))
        {
            instrument_module_name = argv[i+1];
            dr_fprintf(STDERR, "Only printing code contained in module %s\n", instrument_module_name);
            i++;
        }
        else if (!strcmp(arg, "-output_prefix"))
        {
            output_prefix = argv[i+1];
            dr_fprintf(STDERR, "Output filename prefix is %s\n", output_prefix);
            i++;
        }
        else
        {
            dr_fprintf(STDERR, "Incorrect argument.\n");
            exit(0);
        }
    }
}

static void
output_to_files(void)
{
    char *filename = (char *) "test_output.coverage";

    if (output_prefix != 0)
    {
        filename = dr_global_alloc(strlen(output_prefix) + strlen(".coverage") + 1);
        memset(filename, 0x00, strlen(output_prefix) + strlen(".coverage") + 1);
        strncpy(filename, output_prefix, strlen(output_prefix));
        strncat(filename, ".coverage", strlen(".coverage"));
    }

    FILE *fh = fopen(filename, "w+"); 

    struct BasicBlockRecord *cur = global_bbs->next;

    while (cur != NULL)
    {
        fprintf(fh, "0x%08x,0x%08x\n", (uint32_t) cur->bb_source, (uint32_t) cur->bb_dest);
        cur = cur->next;
    }

    fclose(fh);

    fclose(mmap_fh);
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
        //dr_fprintf(STDOUT, "New observed block 0x%08x 0x%08x\n", tinfo->last_bb, bb_addr);

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
    if (instrument_start_address != 0 && instrument_end_address != 0)
    {
        app_pc bb_addr = dr_fragment_app_pc(tag);

        if (bb_addr < instrument_start_address || bb_addr > instrument_end_address)
        {
            return DR_EMIT_DEFAULT;
        }
    }

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
    dr_fprintf(STDOUT, "Module loaded %s\n", info->full_path);

    fprintf(mmap_fh, "%s,0x%08x,0x%08x\n", info->full_path, (uint32_t) info->entry_point, (uint32_t) info->end);

    //Check to see if this is the module we were told to instrument code in
    //
    //If so then set start and end address to entire range off module image.
    if (instrument_start_address == 0 && instrument_end_address == 0 && instrument_module_name != NULL)
    {
        char *fname_ptr = strrchr(info->full_path, '/');

        if (!strncmp(fname_ptr+1, instrument_module_name, strlen(instrument_module_name)))
        {
            instrument_start_address = info->entry_point;
            instrument_end_address = info->end;
        }
    }

    //Probably missing some edge cases here due to module loading
    //unloading but probably okay most of the time.
}

static void
handle_module_unload(void *drcontext, const module_data_t *info)
{
    dr_fprintf(STDOUT, "Module unloaded %s\n", info->full_path);
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    (void) thread_list;
    dr_set_client_name("bbedge_coverage", "arc2205@columbia.edu");

    parse_arguments(argc, argv);

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

    //Open mmap_fh for writing dynamic module loading info to
    char *mmap_filename = (char *) "test_output.mmap";

    if (output_prefix != 0)
    {
        mmap_filename = dr_global_alloc(strlen(output_prefix) + strlen(".mmap") + 1);
        memset(mmap_filename, 0x00, strlen(output_prefix) + strlen(".mmap") + 1);
        strncpy(mmap_filename, output_prefix, strlen(output_prefix));
        strncat(mmap_filename, ".mmap", strlen(".mmap"));
    }

    mmap_fh = fopen(mmap_filename, "wb+");

    dr_log(NULL, LOG_ALL, 1, "Client initialized\n");

    if (dr_is_notify_on()) {
        dr_fprintf(STDERR, "Client bbedge_coverage is running\n");
    }
}
