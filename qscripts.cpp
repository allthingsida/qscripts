/*
Quick execute script: a plugin to speedup IDA scripts development.

This plugin replaces the regular "Recent scripts" and "Execute Script" dialogs and allows you to develop
scripts in your favorite editor and execute them directly in IDA.

(c) Elias Bachaalany <elias.bachaalany@gmail.com>
*/

#include <loader.hpp>
#include <idp.hpp>
#include <expr.hpp>
#include <kernwin.hpp>
#include <diskio.hpp>
#include <registry.hpp>

#include "utils_impl.cpp"

//-------------------------------------------------------------------------
// Some constants
static constexpr int  IDA_MAX_RECENT_SCRIPTS    = 512;
static constexpr char IDAREG_RECENT_SCRIPTS[]   = "RecentScripts";
static constexpr char UNLOAD_SCRIPT_FUNC_NAME[] = "__quick_unload_script";

//-------------------------------------------------------------------------
struct script_info_t
{
    // While we can detect the language from the file's extension, we store to speed up rendering
    extlang_t *lang = nullptr;
    qstring script_file;
    qtime64_t modified_time;
    bool operator==(const script_info_t &rhs) const
    {
        return script_file == rhs.script_file;
    }

    script_info_t(const char *script_file = nullptr)
    {
        if (script_file != nullptr)
            this->script_file = script_file;
    }
};
using scripts_info_t = qvector<script_info_t>;

// Script execution options
struct script_exec_options_t
{
    int change_interval  = 500;
    int clear_log        = 0;
    int show_filename    = 0;
    int exec_unload_func = 0;
};

//-------------------------------------------------------------------------
// Non-modal scripts chooser
struct scripts_chooser_t: public chooser_t
{
protected:
    static constexpr uint32 flags_ = CH_KEEP | CH_RESTORE | CH_ATTRS |
        CH_CAN_DEL | CH_CAN_EDIT | CH_CAN_INS | CH_CAN_REFRESH;

    static constexpr int widths_[2] = { 10, 70 };
    static constexpr char *const header_[2] = { "Language", "Script" };

    extlangs_t m_langs;
    qstring m_browse_scripts_filter;
    scripts_info_t m_scripts;
    ssize_t m_nactive = -1;
    bool m_b_filemon_timer_paused;
    script_exec_options_t m_options;

    qtimer_t m_filemon_timer = nullptr;

    extlang_t *detect_file_lang(const char *script_file)
    {
        auto pext = strrchr(script_file, '.');
        if (pext == nullptr)
            return nullptr;

        ++pext;
        for (auto lang: m_langs)
        {
            if (streq(lang->fileext, pext))
                return lang;
        }
        return nullptr;
    }

    int normalize_filemon_interval(int change_interval)
    {
        return qmax(300, change_interval);
    }

    // Executes a script file and remembers its modified time stamp
    bool execute_active_script()
    {
        // Assume failure
        bool ok = false;

        // Pause the file monitor timer while executing a script
        m_b_filemon_timer_paused = true;
        do 
        {
            auto &si = m_scripts[m_nactive];
            auto script_file = si.script_file.c_str();

            // First things first: always take the file's modification timestamp first so not to visit it again in the file monitor timer
            if (!get_file_modification_time(script_file, si.modified_time))
            {
                msg("Script file '%s' not found!\n", script_file);
                break;
            }

            qstring errbuf;

            if (m_options.clear_log)
                msg_clear();

            // Silently call the unload script function
            if (m_options.exec_unload_func)
            {
                idc_value_t result;
                si.lang->call_func(&result, UNLOAD_SCRIPT_FUNC_NAME, &result, 0, &errbuf);
            }

            if (m_options.show_filename)
                msg("Executing %s\n", script_file);

            bool ok = si.lang->compile_file(script_file, &errbuf);
            if (!ok)
            {
                msg("Failed to compile script file: '%s':\n%s", script_file, errbuf.c_str());
                break;
            }

            // Special case for IDC scripts: we have to call 'main'
            if (streq(si.lang->fileext, IDC_LANG_EXT))
            {
                idc_value_t result;
                ok = si.lang->call_func(&result, "main", &result, 0, &errbuf);
                if (!ok)
                {
                    msg("Failed to run IDC main function '%s':\n%s", script_file, errbuf.c_str());
                    break;
                }
            }
            ok = true;
        } while (false);
        m_b_filemon_timer_paused = false;
        return ok;
    }

    // Add a new script file and properly populate its script info object
    script_info_t *add_script(
        const char *script_file,
        bool silent = false,
        bool unique = true)
    {
        if (unique)
        {
            auto p = m_scripts.find({ script_file });
            if (p != m_scripts.end())
                return &*p;
        }

        qtime64_t mtime;
        if (!get_file_modification_time(script_file, mtime))
        {
            if (!silent)
                msg("Script file not found: '%s'\n", script_file);
            return nullptr;
        }

        auto lang = detect_file_lang(script_file);
        if (lang == nullptr)
        {
            if (!silent)
                msg("Unknown script language detected!\n");

            return nullptr;
        }

        auto &si         = m_scripts.push_back();
        si.script_file   = script_file;
        si.modified_time = mtime;
        si.lang          = lang;
        return &si;
    }

    // Rebuilds the scripts list while maintaining the active script when possible
    ssize_t build_scripts_list(const char *find_script = nullptr)
    {
        // Remember active script and invalidate its index
        qstring active_script;
        bool has_active_script = m_nactive >= 0 && size_t(m_nactive) < m_scripts.size();
        if (has_active_script)
            active_script = m_scripts[m_nactive].script_file;

        m_nactive = -1;

        // Read all scripts
        qstrvec_t scripts_list;
        reg_read_strlist(&scripts_list, IDAREG_RECENT_SCRIPTS);

        // Rebuild the list
        ssize_t idx = 0, find_idx = -1;
        m_scripts.qclear();
        for (auto &script_file: scripts_list)
        {
            // Restore active script
            if (has_active_script && active_script == script_file)
                m_nactive = idx;

            // Optionally, find the index of a script by name
            if (find_script != nullptr && streq(script_file.c_str(), find_script))
                find_idx = idx;

            add_script(script_file.c_str());
            ++idx;
        }
        return find_idx;
    }

    // Save or load the options
    void saveload_options(bool bsave)
    {
        struct int_options_t
        {
            const char *name;
            int *pval;
        } int_options [] =
        {
            {"QScripts_interval",         &m_options.change_interval},
            {"QScripts_clearlog",         &m_options.clear_log},
            {"QScripts_showscriptname",   &m_options.show_filename},
            {"QScripts_exec_unload_func", &m_options.exec_unload_func},
        };

        for (auto &iopt: int_options)
        {
            if (bsave)
                reg_write_int(iopt.name, *iopt.pval);
            else
                *iopt.pval = reg_read_int(iopt.name, *iopt.pval);
        }
        if (!bsave)
            m_options.change_interval = normalize_filemon_interval(m_options.change_interval);
    }

    static int idaapi s_filemon_timer_cb(void *ud)
    {
        return ((scripts_chooser_t *)ud)->filemon_timer_cb();
    }

    int filemon_timer_cb()
    {
        do 
        {
            if (m_nactive == -1)
                break;

            auto &si = m_scripts[m_nactive];

            // Check if file is modified and execute it
            qtime64_t cur_mtime;
            if (!get_file_modification_time(si.script_file.c_str(), cur_mtime))
            {
                // Script no longer exists
                m_nactive = -1;
                msg("Active script '%s' no longer exists!\n", si.script_file.c_str());
                break;
            }

            if (cur_mtime != si.modified_time)
                execute_active_script();
        } while (false);

        return m_options.change_interval;
    }

    bool config_dialog()
    {
        static const char form[] =
            "Options\n"
            "\n"
            "<#Controls the refresh rate of the script change monitor#Script monitor ~i~nterval:D:100:10::>\n"
            "<#Clear the output window before re-running the script#C~l~ear the output window:C>\n"
            "<#Display the name of the file that is automatically executed#Show ~f~ile name when execution:C>\n"
            "<#Execute a function called '__quick_unload_script' before reloading the script#Execute the ~u~nload script function:C>>\n"
            "\n"
            "\n";

        // Copy values to the dialog
        sval_t interval = m_options.change_interval;
        union
        {
            ushort n;
            struct
            {
                ushort b_clear_log : 1;
                ushort b_show_filename : 1;
                ushort b_exec_unload_func : 1;
            };
        } chk_opts;
        chk_opts.n = 0;
        chk_opts.b_clear_log        = m_options.clear_log;
        chk_opts.b_show_filename    = m_options.show_filename;
        chk_opts.b_exec_unload_func = m_options.exec_unload_func;

        if (ask_form(form, &interval, &chk_opts.n) > 0)
        {
            // Copy values from the dialog
            m_options.change_interval  = normalize_filemon_interval(int(interval));
            m_options.clear_log        = chk_opts.b_clear_log;
            m_options.show_filename    = chk_opts.b_show_filename;
            m_options.exec_unload_func = chk_opts.b_exec_unload_func;

            // Save the options directly
            saveload_options(true);
            return true;
        }
        return false;
    }

    const void *get_obj_id(size_t *len) const override
    {
        // Allow a single instance
        *len = sizeof(this);
        return (const void *)this;
    }

    size_t idaapi get_count() const override
    {
        return m_scripts.size();
    }

    void idaapi get_row(
        qstrvec_t *cols,
        int *icon,
        chooser_item_attrs_t *attrs,
        size_t n) const override
    {
        auto si = &m_scripts[n];
        cols->at(0) = si->lang->name;
        cols->at(1) = si->script_file;
        if (n == m_nactive)
            attrs->flags = CHITEM_BOLD;
    }

    // Activate a script and execute it
    cbret_t idaapi enter(size_t n) override
    {
        m_nactive = n;
        execute_active_script();
        return cbret_t(n, chooser_base_t::ALL_CHANGED);
    }

    // Add a new script
    cbret_t idaapi ins(ssize_t) override
    {
        const char *script_file = ask_file(false, "", m_browse_scripts_filter.c_str());
        if (script_file == nullptr)
            return {};

        reg_update_strlist(IDAREG_RECENT_SCRIPTS, script_file, IDA_MAX_RECENT_SCRIPTS);
        ssize_t idx = build_scripts_list(script_file);
        return cbret_t(qmax(idx, 0), chooser_base_t::ALL_CHANGED);
    }

    // Remove a script from the list
    cbret_t idaapi del(size_t n) override
    {
        qstring script_file = m_scripts[n].script_file;
        reg_update_strlist(IDAREG_RECENT_SCRIPTS, nullptr, IDA_MAX_RECENT_SCRIPTS, script_file.c_str());
        build_scripts_list();
        return adjust_last_item(n);
    }

    // Use it to show the configuration dialog
    cbret_t idaapi edit(size_t n) override
    {
        config_dialog();
        return cbret_t(n, chooser_base_t::NOTHING_CHANGED);
    }

    void idaapi closed() override
    {
        if (m_filemon_timer != nullptr)
        {
            unregister_timer(m_filemon_timer);
            m_filemon_timer = nullptr;
        }
    }

    // Initializes the chooser and populates the script files from the last run
    bool init() override
    {
        // Collect all installed external languages
        collect_extlangs(&m_langs, false);

        //
        // Build the filter
        //
        m_browse_scripts_filter = "FILTER Script files|";

        // All scripts
        for (auto lang: m_langs)
            m_browse_scripts_filter.cat_sprnt("*.%s;", lang->fileext);

        m_browse_scripts_filter.remove_last();
        m_browse_scripts_filter.append("|");

        // Language specific filters
        for (auto lang: m_langs)
            m_browse_scripts_filter.cat_sprnt("%s scripts|*.%s|", lang->name, lang->fileext);

        m_browse_scripts_filter.remove_last();
        m_browse_scripts_filter.append("\nSelect script file to load");

        //
        // Load scripts and register the monitor
        //
        saveload_options(false);
        build_scripts_list();

        m_b_filemon_timer_paused = false;
        m_filemon_timer = register_timer(
            m_options.change_interval,
            s_filemon_timer_cb,
            this);

        return true;
    }

    scripts_chooser_t(const char *title_ = "QScripts")
        : chooser_t(flags_, qnumber(widths_), widths_, header_, title_)
    {
        popup_names[POPUP_EDIT] = "~O~ptions";
    }

public:
    static void show()
    {
        static scripts_chooser_t singleton;
        singleton.choose();
    }
};

//-------------------------------------------------------------------------
int idaapi init(void)
{
    return PLUGIN_KEEP;
}

//--------------------------------------------------------------------------
void idaapi term(void)
{
}

//--------------------------------------------------------------------------
bool idaapi run(size_t arg)
{
    scripts_chooser_t::show();
    return true;
}

//--------------------------------------------------------------------------
static const char comment[] = "Develop IDA scripts faster in your favorite text editor";

static const char help[] =
    "An alternative scripts manager that lets you develop in an external editor and run them fast in IDA\n"
    "\n"
    "Just press ENTER on the script to activate it and then go back to your editor to continue development.\n"
    "\n"
    "Each time you update your script, it will be automatically invoked in IDA\n\n"
    "\n"
    "QScripts is developed by Elias Bachaalany. Please see https://github.com/0xeb/ida-qscripts for more information\n"
    "\n"
    "\0"
    __DATE__ " " __TIME__ "\n"
    "\n";

#ifdef _DEBUG
    static const char wanted_hotkey[] = "Alt-Shift-A";
#else
    static const char wanted_hotkey[] = "Alt-Shift-F9";
#endif

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
    IDP_INTERFACE_VERSION,
    0,                    // plugin flags
    init,                 // initialize

    term,                 // terminate. this pointer may be NULL.

    run,                  // invoke plugin

    comment,              // long comment about the plugin
                        // it could appear in the status line
                        // or as a hint

    help,                 // multiline help about the plugin

    "QScripts",           // the preferred short name of the plugin
    wanted_hotkey         // the preferred hotkey to run the plugin
};
