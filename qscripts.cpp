/*
Quick execute script: a plugin to speedup IDA scripts development.

This plugin replaces the regular "Recent scripts" and "Execute Script" dialogs and allows you to develop
scripts in your favorite editor and execute them directly in IDA.

(c) Elias Bachaalany <elias.bachaalany@gmail.com>
*/
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <loader.hpp>
#include <idp.hpp>
#include <expr.hpp>
#include <kernwin.hpp>
#include <diskio.hpp>
#include <registry.hpp>
#pragma warning(pop)
#include "utils_impl.cpp"

namespace IDAICONS { enum
{
    FLASH         = 171,          // Flash icon
    FLASH_EDIT    = 173,          // A flash icon with the pencil on it
    BPT_DISABLED  = 62,           // A gray filled circle crossed (disabled breakpoint)
    RED_DOT       = 59,           // Filled red circle (used to designate an active breakpoint)
    GRAY_X_CIRCLE = 175,          // A filled gray circle with an X in it
}; }

//-------------------------------------------------------------------------
// Some constants
static constexpr int  IDA_MAX_RECENT_SCRIPTS    = 512;
static constexpr char IDAREG_RECENT_SCRIPTS[]   = "RecentScripts";
static constexpr char UNLOAD_SCRIPT_FUNC_NAME[] = "__quick_unload_script";

//-------------------------------------------------------------------------
struct script_info_t
{
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

//-------------------------------------------------------------------------
// Non-modal scripts chooser
struct qscripts_chooser_t: public chooser_t
{
private:
    bool m_b_filemon_timer_active;
    qtimer_t m_filemon_timer = nullptr;

    int opt_change_interval  = 500;
    int opt_clear_log        = 0;
    int opt_show_filename    = 0;
    int opt_exec_unload_func = 0;

    script_info_t selected_script;

    int normalize_filemon_interval(const int change_interval) const
    {
        return qmax(300, change_interval);
    }

    const char *get_selected_script_file()
    {
        return selected_script.script_file.c_str();
    }

    bool is_monitor_active() const { return m_b_filemon_timer_active; }

    void clear_selected_script()
    {
        selected_script.script_file.qclear();
        selected_script.modified_time = 0;
        // ...and deactivate the monitor
        activate_monitor(false);
    }

    const bool has_selected_script()
    {
        return !selected_script.script_file.empty();
    }

    // Executes a script file and makes it the active script
    bool execute_script(script_info_t *script_info)
    {
        bool remove_script = true;
        bool exec_ok = false;

        // Pause the file monitor timer while executing a script
        bool old_state = activate_monitor(false);
        do
        {
            auto script_file = script_info->script_file.c_str();

            // First things first: always take the file's modification timestamp first so not to visit it again in the file monitor timer
            if (!get_file_modification_time(script_file, script_info->modified_time))
            {
                msg("Script file '%s' not found!\n", script_file);
                break;
            }

            const char *script_ext = get_file_extension(script_file);
            extlang_object_t elang(nullptr);
            if (script_ext == nullptr || (elang = find_extlang_by_ext(script_ext)) == nullptr)
            {
                msg("Unknown script language detected for '%s'!\n", script_file);
                break;
            }

            remove_script = false;

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
                msg("QScrits failed to compile script file: '%s':\n%s", script_file, errbuf.c_str());
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

        if (remove_script)
            clear_selected_script();

        return !remove_script && exec_ok;
    }

    // Save or load the options
    void saveload_options(bool bsave)
    {
        struct options_t
        {
            const char *name;
            int vtype;
            void *pval;
        } int_options [] =
        {
            {"QScripts_interval",             VT_LONG, &opt_change_interval},
            {"QScripts_clearlog",             VT_LONG, &opt_clear_log},
            {"QScripts_showscriptname",       VT_LONG, &opt_show_filename},
            {"QScripts_exec_unload_func",     VT_LONG, &opt_exec_unload_func},
            {"QScripts_selected_script_name", VT_STR,  &selected_script.script_file}
        };

        for (auto &opt: int_options)
        {
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
                {
                    reg_write_string(opt.name, ((qstring *)opt.pval)->c_str());
                }
                else
                { 
                    reg_read_string(((qstring *)opt.pval), opt.name);
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

            // Check if file is modified and execute it
            qtime64_t cur_mtime;
            const char *script_file = selected_script.script_file.c_str();
            if (!get_file_modification_time(script_file, cur_mtime))
            {
                // Script no longer exists
                clear_selected_script();
                msg("Active script '%s' no longer exists!\n", script_file);
                break;
            }

            // Script is up to date, no need to execute it again
            if (cur_mtime != selected_script.modified_time)
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
        si.script_file   = script_file;
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
        cols->at(0) = si->script_file;
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
        selected_script = m_scripts[n];
        execute_script(&selected_script);
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
        qstring script_file = m_scripts[n].script_file;
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
