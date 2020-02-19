/*
Quick execute script: a plugin to speedup IDA scripts development.

This plugin replaces the regular "Recent scripts" and "Execute Script" dialogs and allows you to develop
scripts in your favorite editor and execute them directly in IDA.

(c) Elias Bachaalany <elias.bachaalany@gmail.com>
*/
#include <unordered_map>
#include <string>
#include <regex>
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <loader.hpp>
#include <idp.hpp>
#include <expr.hpp>
#include <prodir.h>
#include <kernwin.hpp>
#include <diskio.hpp>
#include <registry.hpp>
#pragma warning(pop)
#include "utils_impl.cpp"

namespace IDAICONS { enum
{
    FLASH                           = 171,          // Flash icon
    FLASH_EDIT                      = 173,          // A flash icon with the pencil on it
    BPT_DISABLED                    = 62,           // A gray filled circle crossed (disabled breakpoint)
    EYE_GLASSES_EDIT                = 43,           // Eye glasses with a small pencil overlay
    RED_DOT                         = 59,           // Filled red circle (used to designate an active breakpoint)
    GRAPH_WITH_FUNC                 = 78,           // Nodes in a graph icon with a smaller function icon overlapped on top of the graph
    GRAY_X_CIRCLE                   = 175,          // A filled gray circle with an X in it
}; }

//-------------------------------------------------------------------------
// Some constants
static constexpr int  IDA_MAX_RECENT_SCRIPTS    = 512;
static constexpr char IDAREG_RECENT_SCRIPTS[]   = "RecentScripts";
static constexpr char UNLOAD_SCRIPT_FUNC_NAME[] = "__quick_unload_script";

//-------------------------------------------------------------------------
// Structure to describe a file and its metadata
struct fileinfo_t
{
    std::string file_path;
    qtime64_t modified_time;
    bool operator==(const fileinfo_t &rhs) const
    {
        return file_path == rhs.file_path;
    }

    fileinfo_t(const char *script_file = nullptr)
    {
        if (script_file != nullptr)
            this->file_path = script_file;
    }

    virtual void clear()
    {
        file_path.clear();
        modified_time = 0;
    }

    // Checks if the current script has been modified
    // Optionally updates the time stamp to the latest one if modified
    // Returns:
    // -1: file no longer exists
    //  0: no modification
    //  1: modified
    int get_modification_status(bool update_mtime=true)
    {
        qtime64_t cur_mtime;
        const char *script_file = this->file_path.c_str();
        if (!get_file_modification_time(script_file, cur_mtime))
            return -1;

        // Script is up to date, no need to execute it again
        if (cur_mtime == modified_time)
            return 0;

        if (update_mtime)
            modified_time = cur_mtime;
        return 1;
    }
};

// Script file
using script_info_t = fileinfo_t;
// Script files
using scripts_info_t = qvector<script_info_t>;

// Dependency script info
struct dep_script_info_t: fileinfo_t
{
    // Each dependency script can have its own reload command
    qstring reload_cmd;

    const bool has_reload_directive() const { return !reload_cmd.empty(); }
};

// Active script information along with its dependencies
struct active_script_info_t: script_info_t
{
    // The dependencies index files. First entry is for the main script's deps
    qvector<fileinfo_t> dep_indices;

    // The list of dependency scripts
    std::unordered_map<std::string, dep_script_info_t> dep_scripts;

    // Checks to see if we have a dependency on a given file
    const bool has_dep(const std::string &dep_file) const
    {
        return dep_scripts.find(dep_file) != dep_scripts.end();
    }

    // If no dependency index files have been modified, we return 0
    // Return 1 if one of them has been modified or -1 if one of them has gone missing.
    // In both latter cases, we have to recompute our dependencies
    int is_any_dep_index_modified(bool update_mtime = true)
    {
        int r = 0;
        for (auto &dep_file: dep_indices)
        {
            r = dep_file.get_modification_status(update_mtime);
            if (r != 0)
                break;
        }
        return r;
    }

    bool add_dep_index(const char *dep_file)
    {
        fileinfo_t fi;
        if (!get_file_modification_time(dep_file, fi.modified_time))
            return false;

        fi.file_path = dep_file;
        dep_indices.push_back(std::move(fi));
        return true;
    }

    active_script_info_t &operator=(const script_info_t &rhs)
    {
        if (this != &rhs)
        {
            file_path     = rhs.file_path;
            modified_time = rhs.modified_time;
        }
        dep_scripts.clear();
        dep_indices.qclear();
        return *this;
    }

    void clear() override
    {
        script_info_t::clear();
        dep_indices.qclear();
        dep_scripts.clear();
    }

    void invalidate_all_scripts()
    {
        modified_time = 0;
        // Invalidate all but the index file itself
        for (auto &kv: dep_scripts)
            kv.second.modified_time = 0;
    }
};

//-------------------------------------------------------------------------
// Non-modal scripts chooser
struct qscripts_chooser_t: public chooser_t
{
private:
    bool m_b_filemon_timer_active;
    qtimer_t m_filemon_timer = nullptr;
    static std::regex RE_EXPANDER;

    int opt_change_interval  = 500;
    int opt_clear_log        = 0;
    int opt_show_filename    = 0;
    int opt_exec_unload_func = 0;

    active_script_info_t selected_script;

    inline int normalize_filemon_interval(const int change_interval) const
    {
        return qmax(300, change_interval);
    }

    const char *get_selected_script_file()
    {
        return selected_script.file_path.c_str();
    }

    bool parse_deps_for_script(const char *script_file, bool main_file=true)
    {
        // Parse the dependency index file
        qstring dep_file;
        dep_file.sprnt("%s.deps.qscripts", script_file);
        FILE *fp = qfopen(dep_file.c_str(), "r");
        if (fp == nullptr)
            return false;

        selected_script.add_dep_index(dep_file.c_str());

        qstring reload_cmd;
        // Parse each line
        for (qstring line = dep_file; qgetline(&line, fp) != -1;)
        {
            line.trim2();

            // Skip comment lines
            if (strncmp(line.c_str(), "//", 2) == 0)
                continue;

            // Parse special directives for the main index file only
            if (main_file)
            {
                if (strncmp(line.c_str(), "/reload ", 8) == 0)
                {
                    reload_cmd = line.c_str() + 8;
                    continue;
                }
            }

            // From here on, any other line is an expandable string leading to a script file
            expand_string(line, line, script_file);

            if (!qisabspath(line.c_str()))
            {
                qstring dir_name = script_file;
                qdirname(dir_name.begin(), dir_name.size(), script_file);

                qstring full_path;
                full_path.sprnt("%s" SDIRCHAR "%s", dir_name.c_str(), line.c_str());
                line = full_path;
            }

            // Always normalize the final script path
            normalize_path_sep(line);

            // Skip dependency scripts that (do not|no longer) exist
            dep_script_info_t dep_script;
            if (!get_file_modification_time(line.c_str(), dep_script.modified_time))
                continue;

            // Add script
            dep_script.file_path = line.c_str();
            dep_script.reload_cmd = reload_cmd;
            selected_script.dep_scripts[line.c_str()] = std::move(dep_script);

            parse_deps_for_script(line.c_str(), false);
        }
        qfclose(fp);

        return true;
    }

    void set_selected_script(script_info_t &script)
    {
        // Activate script
        selected_script = script;

        // Recursively parse the dependencies and the index files
        parse_deps_for_script(script.file_path.c_str(), true);
    }

    void clear_selected_script()
    {
        selected_script.clear();
        // ...and deactivate the monitor
        activate_monitor(false);
    }

    const bool has_selected_script()
    {
        return !selected_script.file_path.empty();
    }

    bool is_monitor_active() const { return m_b_filemon_timer_active; }

    void expand_string(qstring &input, qstring &output, const char *script_file)
    {
        output = std::regex_replace(
            input.c_str(), 
            RE_EXPANDER, 
            [this, script_file](auto &m) -> std::string
            { 
                qstring match1 = m.str(1).c_str();
                if (strncmp(match1.c_str(), "basename", 8) == 0)
                {
                    char *basename, *ext;
                    qstring wrk_str;
                    get_basename_and_ext(script_file, &basename, &ext, wrk_str);
                    return basename;
                }
                else if (strncmp(match1.c_str(), "env:", 4) == 0)
                {
                    qstring env;
                    if (qgetenv(match1.begin() + 4, &env))
                        return env.c_str();
                }
                return m.str(1);
            }
        ).c_str();
    }

    bool execute_reload_directive(dep_script_info_t &dep_script_file)
    {
        qstring err;
        const char *script_file = dep_script_file.file_path.c_str();

        do
        {
            auto ext = get_file_ext(script_file);
            extlang_object_t elang(find_extlang_by_ext(ext == nullptr ? "" : ext));
            if (elang == nullptr)
            {
                err.sprnt("unknown script language detected for '%s'!\n", script_file);
                break;
            }

            qstring reload_cmd;
            expand_string(dep_script_file.reload_cmd, reload_cmd, script_file);

            if (!elang->eval_snippet(reload_cmd.c_str(), &err))
                break;
            return true;
        } while (false);

        msg("QScripts failed to reload script file: '%s':\n%s", script_file, err.c_str());

        return false;
    }

    // Executes a script file
    bool execute_script(script_info_t *script_info)
    {
        bool exec_ok = false;

        // Pause the file monitor timer while executing a script
        bool old_state = activate_monitor(false);
        do
        {
            auto script_file = script_info->file_path.c_str();

            // First things first: always take the file's modification timestamp first so not to visit it again in the file monitor timer
            if (!get_file_modification_time(script_file, script_info->modified_time))
            {
                msg("Script file '%s' not found!\n", script_file);
                break;
            }

            const char *script_ext = get_file_ext(script_file);
            extlang_object_t elang(nullptr);
            if (script_ext == nullptr || (elang = find_extlang_by_ext(script_ext)) == nullptr)
            {
                msg("Unknown script language detected for '%s'!\n", script_file);
                break;
            }

            if (opt_clear_log)
                msg_clear();

            // Silently call the unload script function
            qstring errbuf;
            if (opt_exec_unload_func)
            {
                idc_value_t result;
                elang->call_func(&result, UNLOAD_SCRIPT_FUNC_NAME, &result, 0, &errbuf);
            }

            if (opt_show_filename)
                msg("QScripts executing %s...\n", script_file);

            exec_ok = elang->compile_file(script_file, &errbuf);
            if (!exec_ok)
            {
                msg("QScripts failed to compile script file: '%s':\n%s", script_file, errbuf.c_str());
                break;
            }

            // Special case for IDC scripts: we have to call 'main'
            if (elang->is_idc())
            {
                idc_value_t result;
                exec_ok = elang->call_func(&result, "main", &result, 0, &errbuf);
                if (!exec_ok)
                {
                    msg("QScripts failed to run the IDC main() of file '%s':\n%s", script_file, errbuf.c_str());
                    break;
                }
            }
        } while (false);
        activate_monitor(old_state);

        return exec_ok;
    }

    enum {
        OPTID_INTERVAL       = 0x0001, 
        OPTID_CLEARLOG       = 0x0002,
        OPTID_SHOWNAME       = 0x0004,
        OPTID_UNLOADEXEC     = 0x0008,
        OPTID_SELSCRIPT      = 0x0010,

        OPTID_ONLY_SCRIPT    = OPTID_SELSCRIPT,
        OPTID_ALL_BUT_SCRIPT = 0xffff & ~OPTID_ONLY_SCRIPT,
        OPTID_ALL            = 0xffff,
    };

    // Save or load the options
    void saveload_options(bool bsave, int what_ids = OPTID_ALL)
    {
        enum { STD_STR = 1000 };
        struct options_t
        {
            int id;
            const char *name;
            int vtype;
            void *pval;
        } int_options [] =
        {
            {OPTID_INTERVAL,   "QScripts_interval",             VT_LONG, &opt_change_interval},
            {OPTID_CLEARLOG,   "QScripts_clearlog",             VT_LONG, &opt_clear_log},
            {OPTID_SHOWNAME,   "QScripts_showscriptname",       VT_LONG, &opt_show_filename},
            {OPTID_UNLOADEXEC, "QScripts_exec_unload_func",     VT_LONG, &opt_exec_unload_func},
            {OPTID_SELSCRIPT,  "QScripts_selected_script_name", STD_STR, &selected_script.file_path}
        };

        for (auto &opt: int_options)
        {
            if ((what_ids & opt.id) == 0)
                continue;

            if (opt.vtype == VT_LONG)
            {
                if (bsave)
                    reg_write_int(opt.name, *(int *)opt.pval);
                else
                    *(int *)opt.pval = reg_read_int(opt.name, *(int *)opt.pval);
            }
            else if (opt.vtype == VT_STR)
            {
                if (bsave)
                    reg_write_string(opt.name, ((qstring *)opt.pval)->c_str());
                else
                    reg_read_string(((qstring *)opt.pval), opt.name);
            }
            else if (opt.vtype == STD_STR)
            {
                if (bsave)
                {
                    reg_write_string(opt.name, ((std::string *)opt.pval)->c_str());
                }
                else
                {
                    qstring tmp;
                    reg_read_string(&tmp, opt.name);
                    *((std::string *)opt.pval) = tmp.c_str();
                }
            }
        }

        if (!bsave)
            opt_change_interval = normalize_filemon_interval(opt_change_interval);
    }

    static int idaapi s_filemon_timer_cb(void *ud)
    {
        return ((qscripts_chooser_t *)ud)->filemon_timer_cb();
    }

    int filemon_timer_cb()
    {
        do 
        {
            // No active script, do nothing
            if (!is_monitor_active() || !has_selected_script())
                break;

            // Check if the active script or its dependencies are changed:
            // 1. Dependency file --> repopulate it and execute active script
            // 2. Any dependencies --> reload if needed and //
            // 3. Active script --> execute it again
            auto &dep_scripts = selected_script.dep_scripts;

            // Let's check the dependencies index files first
            auto mod_stat = selected_script.is_any_dep_index_modified();
            if (mod_stat == 1)
            {
                // Force re-parsing of the index file
                dep_scripts.clear();
                set_selected_script(selected_script);

                // Let's invalidate all the scripts time stamps so we ensure they are re-interpreted again
                selected_script.invalidate_all_scripts();

                // Refresh the UI
                refresh_chooser(QSCRIPTS_TITLE);

                // Just leave and come back fast so we get a chance to re-evaluate everything
                return 1; // (1 ms)
            }
            // Dependency index file is gone
            else if (mod_stat == -1 && !dep_scripts.empty())
            {
                // Let's just check the active script
                dep_scripts.clear();
            }

            // Check the dependency scripts
            bool dep_script_changed = false;
            bool brk = false;
            for (auto &kv: dep_scripts)
            {
                auto &dep_script = kv.second;
                if (dep_script.get_modification_status() == 1)
                {
                    dep_script_changed = true;
                    if (     dep_script.has_reload_directive()
                         && !execute_reload_directive(dep_script))
                    {
                        brk = true;
                        break;
                    }
                }
            }
            if (brk)
                break;

            // Check the main script
            if ((mod_stat = selected_script.get_modification_status()) == -1)
            {
                // Script no longer exists
                clear_selected_script();
                msg("QScripts detected that the active script '%s' no longer exists!\n", get_selected_script_file());
                break;
            }

            // Script or its dependencies changed?
            if (dep_script_changed || mod_stat == 1)
                execute_script(&selected_script);
        } while (false);
        return opt_change_interval;
    }

protected:
    static constexpr uint32 flags_ = 
        CH_KEEP    | CH_RESTORE  | CH_ATTRS   |
        CH_CAN_DEL | CH_CAN_EDIT | CH_CAN_INS | CH_CAN_REFRESH;

    static int widths_[1];
    static char *const header_[1];
    static char ACTION_DEACTIVATE_MONITOR_ID[];
    static char ACTION_EXECUTE_SELECTED_SCRIPT_ID[];
    static action_desc_t deactivate_monitor_action;
    static action_desc_t execute_selected_script_action;

    scripts_info_t m_scripts;
    ssize_t m_nselected = NO_SELECTION;

    struct qscript_action_handler_t: action_handler_t
    {
    protected:
        qscripts_chooser_t *ch;

        
        bool is_correct_widget(action_update_ctx_t *ctx)
        {
            return ctx->widget_title == ch->title;
        }
    public:
        void setup(qscripts_chooser_t *ch, action_desc_t &ah)
        {
            this->ch = ch;
            ah.handler = this;
            register_action(ah);
        }
    };

    struct deactivate_script_ah_t: qscript_action_handler_t
    {
        virtual action_state_t idaapi update(action_update_ctx_t *ctx) override
        {
            if (!is_correct_widget(ctx))
                return AST_DISABLE_FOR_WIDGET;
            else
                return ch->is_monitor_active() ? AST_ENABLE : AST_DISABLE;
        }

        virtual int idaapi activate(action_activation_ctx_t *ctx) override
        {
            ch->activate_monitor(false);
            refresh_chooser(QSCRIPTS_TITLE);
            return 1;
        }
    };

    struct exec_selected_script_ah_t: qscript_action_handler_t
    {
        virtual action_state_t idaapi update(action_update_ctx_t *ctx) override
        {
            if (!is_correct_widget(ctx))
                return AST_DISABLE_FOR_WIDGET;
            else
                return ctx->chooser_selection.empty() ? AST_DISABLE : AST_ENABLE;
        }

        virtual int idaapi activate(action_activation_ctx_t *ctx) override
        {
            if (!ctx->chooser_selection.empty())
                ch->execute_script_at(ctx->chooser_selection.at(0));
            return 1;
        }
    };

    deactivate_script_ah_t    deactivate_monitor_ah;
    exec_selected_script_ah_t execute_selected_script_ah;

    // Add a new script file and properly populate its script info object
    // and returns a borrowed reference
    const script_info_t *add_script(
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

        auto &si         = m_scripts.push_back();
        si.file_path     = script_file;
        si.modified_time = mtime;
        return &si;
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
        union
        {
            ushort n;
            struct
            {
                ushort b_clear_log        : 1;
                ushort b_show_filename    : 1;
                ushort b_exec_unload_func : 1;
            };
        } chk_opts;
        // Load previous options first (account for multiple instances of IDA)
        saveload_options(false);
        
        chk_opts.n = 0;
        chk_opts.b_clear_log        = opt_clear_log;
        chk_opts.b_show_filename    = opt_show_filename;
        chk_opts.b_exec_unload_func = opt_exec_unload_func;
        sval_t interval             = opt_change_interval;

        if (ask_form(form, &interval, &chk_opts.n) > 0)
        {
            // Copy values from the dialog
            opt_change_interval  = normalize_filemon_interval(int(interval));
            opt_clear_log        = chk_opts.b_clear_log;
            opt_show_filename    = chk_opts.b_show_filename;
            opt_exec_unload_func = chk_opts.b_exec_unload_func;

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
        cols->at(0) = si->file_path.c_str();
        if (n == m_nselected)
        {
            if (is_monitor_active())
            {
                attrs->flags = CHITEM_BOLD;
                *icon = IDAICONS::FLASH_EDIT;
            }
            else
            {
                attrs->flags = CHITEM_ITALIC;
                *icon = IDAICONS::RED_DOT;
            }
        }
        else if (is_monitor_active() && selected_script.has_dep(si->file_path))
        {
            *icon = IDAICONS::EYE_GLASSES_EDIT;
        }
        else
        {
            *icon = IDAICONS::GRAY_X_CIRCLE;
        }
    }

    // Activate a script and execute it
    cbret_t idaapi enter(size_t n) override
    {
        m_nselected = n;

        // Set as the selected script and execute it
        set_selected_script(m_scripts[n]);
        if (execute_script(&selected_script))
            saveload_options(true, OPTID_ONLY_SCRIPT);

        // ...and activate the monitor even if the script fails
        activate_monitor();

        return cbret_t(n, chooser_base_t::ALL_CHANGED);
    }

    // Add a new script
    cbret_t idaapi ins(ssize_t) override
    {
        qstring filter;
        get_browse_scripts_filter(filter);
        const char *script_file = ask_file(false, "", filter.c_str());
        if (script_file == nullptr)
            return {};

        reg_update_strlist(IDAREG_RECENT_SCRIPTS, script_file, IDA_MAX_RECENT_SCRIPTS);
        ssize_t idx = build_scripts_list(script_file);
        return cbret_t(qmax(idx, 0), chooser_base_t::ALL_CHANGED);
    }

    // Remove a script from the list
    cbret_t idaapi del(size_t n) override
    {
        auto &script_file = m_scripts[n].file_path;
        reg_update_strlist(IDAREG_RECENT_SCRIPTS, nullptr, IDA_MAX_RECENT_SCRIPTS, script_file.c_str());
        build_scripts_list();

        // Active script removed?
        if (m_nselected == NO_SELECTION)
            clear_selected_script();

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
        unregister_action(ACTION_DEACTIVATE_MONITOR_ID);
        unregister_action(ACTION_EXECUTE_SELECTED_SCRIPT_ID);
        saveload_options(true);
    }

    static void get_browse_scripts_filter(qstring &filter)
    {
        // Collect all installed external languages
        extlangs_t langs;
        collect_extlangs(&langs, false);

        // Build the filter
        filter = "FILTER Script files|";

        for (auto lang: langs)
            filter.cat_sprnt("*.%s;", lang->fileext);

        filter.remove_last();
        filter.append("|");

        // Language specific filters
        for (auto lang: langs)
            filter.cat_sprnt("%s scripts|*.%s|", lang->name, lang->fileext);

        filter.remove_last();
        filter.append("\nSelect script file to load");
    }

    // Initializes the chooser and populates the script files from the last run
    bool init() override
    {
        deactivate_monitor_ah.setup(this, deactivate_monitor_action);
        execute_selected_script_ah.setup(this, execute_selected_script_action);
        return true;
    }

public:
    static char *QSCRIPTS_TITLE;

    qscripts_chooser_t(const char *title_ = QSCRIPTS_TITLE)
        : chooser_t(flags_, qnumber(widths_), widths_, header_, title_)
    {
        popup_names[POPUP_EDIT] = "~O~ptions";
    }

    bool activate_monitor(bool activate = true)
    {
        bool old = m_b_filemon_timer_active;
        m_b_filemon_timer_active = activate;
        return old;
    }

    // Rebuilds the scripts list and returns the index of the `find_script` if needed
    ssize_t build_scripts_list(const char *find_script = nullptr)
    {
        // Remember active script and invalidate its index
        bool b_has_selected_script = has_selected_script();
        qstring selected_script;
        if (b_has_selected_script)
            selected_script = get_selected_script_file();

        // De-selected the current script in the hope of finding it again in the list
        m_nselected = NO_SELECTION;

        // Read all scripts
        qstrvec_t scripts_list;
        reg_read_strlist(&scripts_list, IDAREG_RECENT_SCRIPTS);

        // Rebuild the list
        ssize_t idx = 0, find_idx = NO_SELECTION;
        m_scripts.qclear();
        for (auto &script_file: scripts_list)
        {
            // Restore active script
            if (b_has_selected_script && selected_script == script_file)
                m_nselected = idx;

            // Optionally, find the index of a script by name
            if (find_script != nullptr && streq(script_file.c_str(), find_script))
                find_idx = idx;

            // We skip non-existent scripts
            if (add_script(script_file.c_str(), true) != nullptr)
                ++idx;
        }
        return find_idx;
    }

    void execute_last_selected_script()
    {
        if (has_selected_script())
            execute_script(&selected_script);
    }

    void execute_script_at(ssize_t n)
    {
        if (n >=0 && n < ssize_t(m_scripts.size()))
            execute_script(&m_scripts[n]);
    }

    void show()
    {
        build_scripts_list();

        auto r = choose(m_nselected);

        TWidget *widget;
        if (r == 0 && (widget = find_widget(QSCRIPTS_TITLE)) != nullptr)
        {
            attach_action_to_popup(
                widget,
                nullptr,
                ACTION_DEACTIVATE_MONITOR_ID);
            attach_action_to_popup(
                widget,
                nullptr,
                ACTION_EXECUTE_SELECTED_SCRIPT_ID);
        }
    }

    bool start_monitor()
    {
        // Load the options
        saveload_options(false);

        // Register the monitor
        m_b_filemon_timer_active = false;
        m_filemon_timer = register_timer(
            opt_change_interval,
            s_filemon_timer_cb,
            this);
        return m_filemon_timer != nullptr;
    }

    void stop_monitor()
    {
        if (m_filemon_timer != nullptr)
        {
            unregister_timer(m_filemon_timer);
            m_filemon_timer = nullptr;
            m_b_filemon_timer_active = false;
        }
    }

    virtual ~qscripts_chooser_t()
    {
        stop_monitor();
    }
};

std::regex qscripts_chooser_t::RE_EXPANDER                   = std::regex(R"(\$(.+?)\$)");
int qscripts_chooser_t::widths_[1]                           = { 70 };
char *const qscripts_chooser_t::header_[1]                   = { "Script" };
char *qscripts_chooser_t::QSCRIPTS_TITLE                     = "QScripts";
char qscripts_chooser_t::ACTION_DEACTIVATE_MONITOR_ID[]      = "qscript:deactivatemonitor";
char qscripts_chooser_t::ACTION_EXECUTE_SELECTED_SCRIPT_ID[] = "qscript:execselscript";

action_desc_t qscripts_chooser_t::deactivate_monitor_action = ACTION_DESC_LITERAL(
    ACTION_DEACTIVATE_MONITOR_ID, 
    "Deactivate script monitor",
    nullptr,
    "Ctrl+D",
    nullptr,
    IDAICONS::BPT_DISABLED);

action_desc_t qscripts_chooser_t::execute_selected_script_action = ACTION_DESC_LITERAL(
    ACTION_EXECUTE_SELECTED_SCRIPT_ID,
    "Execute selected script",
    nullptr,
    "Shift+Enter",
    nullptr,
    IDAICONS::FLASH);

qscripts_chooser_t *g_qscripts_ui;

//-------------------------------------------------------------------------
int idaapi init(void)
{
    g_qscripts_ui = new qscripts_chooser_t();
    if (!g_qscripts_ui->start_monitor())
    {
        msg("QScripts: Failed to install monitor!\n");
        delete g_qscripts_ui;
        g_qscripts_ui = nullptr;

        return PLUGIN_SKIP;
    }
    return PLUGIN_KEEP;
}

//--------------------------------------------------------------------------
bool idaapi run(size_t arg)
{
    switch (arg)
    {
        // Full UI run
        case 0:
        {
            g_qscripts_ui->show();
            break;
        }
        // Execute the selected script
        case 1:
        {
            g_qscripts_ui->execute_last_selected_script();
            break;
        }
        // Activate the scripts monitor
        case 2:
        {
            g_qscripts_ui->activate_monitor(true);
            refresh_chooser(g_qscripts_ui->QSCRIPTS_TITLE);
            break;
        }
        // Deactivate the scripts monitor
        case 3:
        {
            g_qscripts_ui->activate_monitor(false);
            refresh_chooser(g_qscripts_ui->QSCRIPTS_TITLE);
            break;
        }
    }

    return true;
}

//--------------------------------------------------------------------------
void idaapi term(void)
{
    delete g_qscripts_ui;
}

//--------------------------------------------------------------------------
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
    0,
    init,
    term,
    run,
    "Develop IDA scripts faster in your favorite text editor",
    help,
    qscripts_chooser_t::QSCRIPTS_TITLE,
    wanted_hotkey
};
