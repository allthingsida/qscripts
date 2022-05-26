#include "idasdk.h"

#pragma pack(push, 1)
struct file_header_t
{
    char sig[4];      // Signature == "CHNK"
    char cpuname[10]; // Processor name (for set_processor_type())
    int32 nchunks;    // Number of chunks
    int32 entrypoint; // The entry point address
};

struct chunk_t
{
    int32 base;     // base address
    int32 sz;       // size
};
#pragma pack(pop)

static int idaapi accept_file(
    qstring* fileformatname,
    qstring* processor,
    linput_t* li,
    const char* filename)
{
    file_header_t fh;
    lread(li, &fh, sizeof(file_header_t));
    if (strncmp(fh.sig, "CHNK", 4) != 0)
        return 0;

    *fileformatname = "Chunk file loader";
    *processor = fh.cpuname;

    return 1;
}

//--------------------------------------------------------------------------
//
//      load file into the database.
//
void idaapi load_file(linput_t* li, ushort neflag, const char* fileformatname)
{
    // Read the header to detect the number of chunks, proc name, etc.
    file_header_t fh;
    lread(li, &fh, sizeof(file_header_t));

    set_processor_type(fh.cpuname, SETPROC_USER);

    for (int32 i = 0; i < fh.nchunks; ++i)
    {
        qstring seg_name;
        seg_name.sprnt("chunk%d", i);

        chunk_t chkinfo;
        lread(li, &chkinfo, sizeof(chkinfo));

        ea_t start_ea = chkinfo.base;
        ea_t end_ea = chkinfo.base + chkinfo.sz;
        add_segm(
            0,
            start_ea, 
            end_ea, 
            seg_name.c_str(),
            "CODE",
            0);

        // Now read the actual data
        file2base(li, qltell(li), start_ea, end_ea, 1);
    }
    inf_set_start_ea(fh.entrypoint);
    inf_set_start_ip(fh.entrypoint);
    inf_set_start_cs(0);
    add_entry(0, fh.entrypoint, "start", 1, 0);
}

bool test_accept_file(linput_t *li, const char *fname)
{
    qstring format_name, procname;
    if (accept_file(&format_name, &procname, li, fname))
    {
        msg("Recognized format name: %s\n", format_name.c_str());
        msg("Suggest proc module   : %s\n", procname.c_str());
        return true;
    }
    else
    {
        msg("Not recognized!\n");
        return false;
    }
}

bool main()
{
    msg_clear();

    auto fname = R"(C:\Users\elias\Projects\github\ida-qscripts\samples\chunk1.bin)";
    auto li = open_linput(fname, false);

    if (!test_accept_file(fname))
        return false;
    
    load_file(li, 0, fname);
    close_linput(li);
}