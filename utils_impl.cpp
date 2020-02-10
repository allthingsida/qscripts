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

//-------------------------------------------------------------------------
void normalize_path_sep(qstring &path)
{
#ifdef __FAT__
    path.replace("/", SDIRCHAR);
#else
    path.replace("\\", SDIRCHAR);
#endif
}

//-------------------------------------------------------------------------
bool get_basename_and_ext(
    const char *path, 
    char **basename,
    char **ext,
    qstring &wrk_buf)
{
    wrk_buf = path;
    qsplitfile(wrk_buf.begin(), basename, ext);
    if ((*basename = qstrrchr(wrk_buf.begin(), DIRCHAR)) != nullptr)
        return ++(*basename), true;
    else
        return false;
}

//-------------------------------------------------------------------------
// Utility function similar to Python's re.sub().
// Based on https://stackoverflow.com/a/37516316
namespace std
{
    template<class BidirIt, class Traits, class CharT, class UnaryFunction>
    std::basic_string<CharT> regex_replace(BidirIt first, BidirIt last,
        const std::basic_regex<CharT, Traits> &re, UnaryFunction f)
    {
        std::basic_string<CharT> s;

        typename std::match_results<BidirIt>::difference_type positionOfLastMatch = 0;
        auto endOfLastMatch = first;

        auto callback = [&](const std::match_results<BidirIt> &match)
        {
            auto positionOfThisMatch = match.position(0);
            auto diff = positionOfThisMatch - positionOfLastMatch;

            auto startOfThisMatch = endOfLastMatch;
            std::advance(startOfThisMatch, diff);

            s.append(endOfLastMatch, startOfThisMatch);
            s.append(f(match));

            auto lengthOfMatch = match.length(0);

            positionOfLastMatch = positionOfThisMatch + lengthOfMatch;

            endOfLastMatch = startOfThisMatch;
            std::advance(endOfLastMatch, lengthOfMatch);
        };

        std::regex_iterator<BidirIt> begin(first, last, re), end;
        std::for_each(begin, end, callback);

        s.append(endOfLastMatch, last);

        return s;
    }

    template<class Traits, class CharT, class UnaryFunction>
    std::string regex_replace(const std::string &s,
        const std::basic_regex<CharT, Traits> &re, UnaryFunction f)
    {
        return regex_replace(s.cbegin(), s.cend(), re, f);
    }
}
