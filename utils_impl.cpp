//-------------------------------------------------------------------------
const char *get_file_extension(const char *script_file)
{
    auto pext = strrchr(script_file, '.');
    if (pext == nullptr)
        return nullptr;

    return ++pext;
}

//-------------------------------------------------------------------------
struct collect_extlangs: extlang_visitor_t
{
	extlangs_t *langs;

	virtual ssize_t idaapi visit_extlang(extlang_t *extlang) override
	{
		langs->push_back(extlang);
		return 0;
	}

	collect_extlangs(extlangs_t *langs, bool select)
	{
		langs->qclear();
		this->langs = langs;
		for_all_extlangs(*this, select);
	}
};

//-------------------------------------------------------------------------
// Utility function to return a file's last modification timestamp
bool get_file_modification_time(
	const char *filename,
	qtime64_t &mtime)
{
	qstatbuf stat_buf;
	if (qstat(filename, &stat_buf) != 0)
		return false;
	else
		return mtime = stat_buf.qst_mtime, true;
}
