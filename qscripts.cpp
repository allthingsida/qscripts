/*
Quick execute script: a plugin to speedup IDA scripts development.

This plugin replaces the regular "Recent scripts" and "Execute Script" dialogs and allows you to develop
scripts in your favorite editor and execute them directly in IDA.

(c) Elias Bachaalany <elias.bachaalany@gmail.com>
*/
#include <unordered_map>
#include <string>
#include <regex>
#include <filesystem>
#include <unordered_set>
#include "ida.h"

#include "utils_impl.cpp"
#include "script.hpp"

//-------------------------------------------------------------------------
// Some constants
static constexpr int  IDA_MAX_RECENT_SCRIPTS    = 512;
static constexpr char IDAREG_RECENT_SCRIPTS[]   = "RecentScripts";

//-------------------------------------------------------------------------
// Non-modal scripts chooser
struct qscripts_chooser_t: public plugmod_t, public chooser_t, public event_listener_t
{
    using chooser_t::operator delete;
    using chooser_t::operator new;

private:
    action_manager_t am;

    bool m_b_filemon_timer_active = false;
    qtimer_t m_filemon_timer = nullptr;
    const std::regex RE_EXPANDER = std::regex(R"(\$(.+?)\$)");

    int opt_change_interval  = 500;
    int opt_clear_log        = 0;
    int opt_show_filename    = 0;
    int opt_exec_unload_func = 0;
    int opt_with_undo        = 0;

    active_script_info_t selected_script;
    script_info_t* action_active_script = nullptr;

    struct expand_ctx_t
    {
        // input
        qstring script_file;
        bool    main_file;

        // working
        qstring base_dir;
        qstring pkg_base;
        qstring reload_cmd;
    };

    inline int normalize_filemon_interval(const int change_interval) const
    {
        return qmax(300, change_interval);
    }

    const char *get_selected_script_file()
    {
        return selected_script.file_path.c_str();
    }

    ssize_t idaapi on_event(ssize_t code, va_list va) override
    {
        if (code == ui_finish_populating_widget_popup)
            am.on_ui_finish_populating_widget_popup(va);

        return 0;
    }

    bool make_meta_filename(
        const char* filename,
        const char* extension,
        qstring& out,
        bool local_only = false)
    {
        // Check the .qscripts folder
        char dir[2048];
        if (qdirname(dir, sizeof(dir), filename))
        {
            out.sprnt("%s" SDIRCHAR QSCRIPTS_LOCAL SDIRCHAR "%s.%s", dir, qbasename(filename), extension);
            if (qfileexist(out.c_str()))
                return true;
        }

        if (local_only)
            return false;

        // Check the actual script folder
        out.sprnt("%s.%s", filename, extension);
        return qfileexist(out.c_str());
    }

    bool find_deps_file(
        const char* filename,
        qstring& out)
    {
        return      make_meta_filename(filename, "deps", out, true)
                ||  make_meta_filename(filename, "deps.qscripts", out);

    }
    
    bool parse_deps_for_script(expand_ctx_t &ctx)
    {
        // Parse the dependency index file
        qstring dep_file;
        if (!find_deps_file(ctx.script_file.c_str(), dep_file))
            return false;

        FILE *fp = qfopen(dep_file.c_str(), "r");
        if (fp == nullptr)
            return false;

        // Get the dependency file directory
        ctx.base_dir.resize(ctx.script_file.size());
        qdirname(ctx.base_dir.begin(), ctx.base_dir.size(), ctx.script_file.c_str());
        ctx.base_dir.resize(strlen(ctx.base_dir.c_str()));

        // Add the dependency file to the active script
        selected_script.add_dep_index(dep_file.c_str());

        static auto get_value = [](const char* str, const char* key, int key_len) -> const char *
        {
            if (strncmp(str, key, key_len) != 0)
                return nullptr;
            // Empty value?
            if (str[key_len] == '\0')
                return "";
            else
                return str + key_len + 1;
        };

        // Parse each line
        for (qstring line = dep_file; qgetline(&line, fp) != -1;)
        {
            line.trim2();

            // Skip comment lines (';', '//' and '#')
            if (line.empty() || strncmp(line.c_str(), "//", 2) == 0 || line[0] == '#' || line[0] == ';')
                continue;

            // Parse special directives (some apply only for the main selected script)
            if (auto val = get_value(line.c_str(), "/pkgbase", 8))
            {
                if (ctx.main_file)
                {
                    ctx.pkg_base = val;
                    expand_file_name(ctx.pkg_base, ctx);
                    make_abs_path(ctx.pkg_base, ctx.base_dir.c_str(), true);
                }
                continue;
            }
            else if (auto val = get_value(line.c_str(), "/notebook.cells_re", 18))
            {
                if (ctx.main_file)
                {
                    selected_script.notebook.cells_re = std::regex(val);
                    continue;
                }
            }
            else if (auto val = get_value(line.c_str(), "/notebook.activate", 18))
            {
                if (ctx.main_file)
                {
                    int act;
                    if (qstrcmp(val, "exec_main") == 0)
                        act = notebook_ctx_t::act_exec_main;
                    else if (qstrcmp(val, "exec_all") == 0)
                        act = notebook_ctx_t::act_exec_all;
                    else
                        act = notebook_ctx_t::act_exec_none;

                    selected_script.notebook.activation_action = act;
                    continue;
                }
            }
            else if (auto val = get_value(line.c_str(), "/notebook", 9))
            {
                if (ctx.main_file)
                {
                    selected_script.b_is_notebook = true;
                    selected_script.notebook.title = val;
                    continue;
                }
            }
            else if (auto val = get_value(line.c_str(), "/reload", 7))
            {
                if (ctx.main_file)
                    ctx.reload_cmd = val;
                continue;
            }
            else if (auto trigger_file = get_value(line.c_str(), "/triggerfile", 12))
            {
                if (auto keep = get_value(trigger_file, "/keep", 5))
                {
                    trigger_file = keep;
                    selected_script.b_keep_trigger_file = true;
                }

                if (ctx.main_file)
                {
                    selected_script.trigger_file.refresh(trigger_file);
                    expand_file_name(selected_script.trigger_file.file_path, ctx);
                }
                continue;
            }

            // From here on, the *line* variable is an expandable string leading to a script file
            ctx.script_file = line;
            expand_file_name(line, ctx);
            normalize_path_sep(line);

            // Skip dependency scripts that (do not|no longer) exist
            script_info_t dep_script;
            if (!get_file_modification_time(line, &dep_script.modified_time))
                continue;

            // Add script
            dep_script.file_path  = line.c_str();
            dep_script.reload_cmd = ctx.reload_cmd;
            dep_script.pkg_base   = ctx.pkg_base;

            selected_script.dep_scripts[line.c_str()] = std::move(dep_script);

            expand_ctx_t sub_ctx = ctx;
            sub_ctx.script_file  = line;
            sub_ctx.main_file    = false;
            parse_deps_for_script(sub_ctx);
        }
        qfclose(fp);

        return true;
    }

    void expand_file_name(qstring &filename, const expand_ctx_t &ctx)
    {
        expand_string(filename, filename, ctx);
        make_abs_path(filename, ctx.base_dir.c_str(), true);
    }

    void populate_initial_notebook_cells()
    {
        auto& cell_files = selected_script.notebook.cell_files;
        auto current_path = std::filesystem::path(selected_script.file_path.c_str()).parent_path();
        selected_script.notebook.base_path = current_path.string();

        enumerate_files(
            current_path, 
            selected_script.notebook.cells_re, 
            [&cell_files](const std::string& filename)
            {
                qtime64_t mtime;
                get_file_modification_time(filename, &mtime);
                cell_files[filename] = mtime;
                return true;
            }
        );
    }
    
    void set_selected_script(script_info_t &script)
    {
        // Activate a new script
        selected_script.clear();
        selected_script.refresh(script.file_path.c_str());

        // Recursively parse the dependencies and the index files
        expand_ctx_t main_ctx = { script.file_path.c_str(), true };
        parse_deps_for_script(main_ctx);

        // If a notebook is selected, let's capture all the cell files
        if (selected_script.is_notebook())
            populate_initial_notebook_cells();
    }

    void clear_selected_script()
    {
        action_active_script = nullptr;
        selected_script.clear();
        // ...and deactivate the monitor
        activate_monitor(false);
    }

    const bool has_selected_script()
    {
        return !selected_script.file_path.empty();
    }

    bool is_monitor_active()          const { return m_b_filemon_timer_active; }
    bool is_filemon_timer_installed() const { return m_filemon_timer != nullptr; }

    std::string expand_pkgmodname(const expand_ctx_t& ctx)
    {
        auto dep_file = selected_script.has_dep(ctx.script_file.c_str());
        qstring pkg_base = dep_file == nullptr ? selected_script.pkg_base : dep_file->pkg_base;

        // If the script file is in the package base, then replace the path separators with '.'
        if (strncmp(ctx.script_file.c_str(), pkg_base.c_str(), pkg_base.length()) == 0)
        {
            qstring s = ctx.script_file.c_str() + pkg_base.length() + 1;
            s.replace(SDIRCHAR, ".");
            // Drop the extension too
            auto idx = s.rfind('.');
            if (idx != -1)
                s.resize(idx);

            return s.c_str();
        }
        return "";
    }
    
    // Dynamic string expansion   Description
    // ------------------------   -----------
    // basename                   Returns the basename of the input file
    // env:Variable_Name          Expands the 'Variable_Name'
    // pkgbase                    Sets the current pkgbase path
    // pkgmodname                 Expands the file name using the pkgbase into the form: 'module.submodule1.submodule2'
    // pkgparentmodname           Expands the file name using the pkgbase into the form up to the parent module: 'module.submodule1'
    // ext                        Add-on suffix including bitness and extension (example: 64.dll, .so, 64.so, .dylib, etc.)
    void expand_string(
        qstring &input, 
        qstring &output, 
        const expand_ctx_t& ctx)
    {
        output = std::regex_replace(
            input.c_str(),
            RE_EXPANDER,
            [this, ctx](auto &m) -> std::string
            {
                qstring match1 = m.str(1).c_str();

                if (strncmp(match1.c_str(), "pkgmodname", 10) == 0)
                {
					return expand_pkgmodname(ctx);
                }
                else if (strncmp(match1.c_str(), "pkgparentmodname", 16) == 0)
                {
                    std::string pkgmodname = expand_pkgmodname(ctx);
                    size_t pos = pkgmodname.rfind('.');
                    return pos == std::string::npos ? pkgmodname : pkgmodname.substr(0, pos);
                }
                else if (strncmp(match1.c_str(), "ext", 3) == 0)
                {
                    static_assert(LOADER_DLL[0] == '*');
                    return LOADER_DLL + 1;
                }
                else if (strncmp(match1.c_str(), "pkgbase", 7) == 0)
                {
                    return ctx.pkg_base.c_str();
                }
                else if (strncmp(match1.c_str(), "basename", 8) == 0)
                {
                    char *basename, *ext;
                    qstring wrk_str;
                    get_basename_and_ext(ctx.script_file.c_str(), &basename, &ext, wrk_str);
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

    bool execute_reload_directive(
        script_info_t &dep_script_file,
        qstring &err,
        bool silent=true)
    {
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
            expand_ctx_t ctx;
            ctx.script_file = script_file;
            ctx.pkg_base = dep_script_file.pkg_base;
            expand_string(dep_script_file.reload_cmd, reload_cmd, ctx);

            if (!elang->eval_snippet(reload_cmd.c_str(), &err))
            {
                err.sprnt(
                    "QScripts failed to reload script file: '%s'\n"
                    "Reload command used: %s\n"
                    "Error: %s\n",                  
                    script_file, reload_cmd.c_str(), err.c_str());
                break;
            }
            return true;
        } while (false);

        if (!silent)
			msg("%s", err.c_str());

        return false;
    }

    bool execute_script(script_info_t *script_info, bool with_undo)
    {
        if (with_undo)
        {
            action_active_script = script_info;
            auto r = process_ui_action(ACTION_EXECUTE_SCRIPT_WITH_UNDO_ID);
            action_active_script = nullptr;
            return r;
        }
        return execute_script_sync(script_info);
    }

    // Executes a script file
    bool execute_script_sync(script_info_t *script_info)
    {
        bool exec_ok = false;

        // Pause the file monitor timer while executing a script
        bool old_state = activate_monitor(false);
        do
        {
            auto script_file = script_info->file_path.c_str();

            // First things first: always take the file's modification time-stamp first so not to visit it again in the file monitor timer
            if (!get_file_modification_time(script_file, &script_info->modified_time))
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

            exec_ok = elang->compile_file(
                script_file, 
#if IDA_SDK_VERSION >= 900
                nullptr,  // requested_namespace
#endif
                &errbuf);
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

    enum 
    {
        OPTID_INTERVAL       = 0x0001,
        OPTID_CLEARLOG       = 0x0002,
        OPTID_SHOWNAME       = 0x0004,
        OPTID_UNLOADEXEC     = 0x0008,
        OPTID_SELSCRIPT      = 0x0010,
        OPTID_WITHUNDO       = 0x0020,

        OPTID_ONLY_SCRIPT    = OPTID_SELSCRIPT,
        OPTID_ALL_BUT_SCRIPT = 0xffff & ~OPTID_ONLY_SCRIPT,
        OPTID_ALL            = 0xffff,
    };

    // Save or load the options
    void saveload_options(bool bsave, int what_ids = OPTID_ALL)
    {
        enum { QSTR = 1000 };
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
            {OPTID_SELSCRIPT,  "QScripts_selected_script_name", QSTR, &selected_script.file_path},
            {OPTID_WITHUNDO,   "QScripts_with_undo",            VT_LONG, &opt_with_undo}
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
            else if (opt.vtype == QSTR)
            {
                if (bsave)
                {
                    reg_write_string(opt.name, ((qstring *)opt.pval)->c_str());
                }
                else
                {
                    qstring tmp;
                    reg_read_string(&tmp, opt.name);
                    *((qstring *)opt.pval) = tmp.c_str();
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

    // Monitor callback
    int filemon_timer_cb()
    {
        do
        {
            // No active script, do nothing
            if (!is_monitor_active() || !has_selected_script())
                break;

            std::unique_ptr<active_script_info_t> notebook_cell_script;
            active_script_info_t* work_script = &selected_script;

            //
            // Notebook mode
            //
            if (selected_script.is_notebook())
            {
                auto& last_active_cell = selected_script.notebook.last_active_cell;
                auto& cell_files = selected_script.notebook.cell_files;
                auto current_path = std::filesystem::path(selected_script.file_path.c_str()).parent_path();
                std::unordered_set<std::string> present_files;

                std::string active_cell;
                enumerate_files(
                    current_path, 
                    selected_script.notebook.cells_re, 
                    [&present_files, &last_active_cell, &active_cell, &cell_files](const std::string& filename)
                    {
                        present_files.insert(filename);
                        auto p = cell_files.find(filename);

                        qtime64_t mtime;
                        get_file_modification_time(filename.c_str(), &mtime);

                        // New file?
                        if (p == cell_files.end())
                        {
                            cell_files[filename] = mtime;
                        }
                        // File was modified?
                        else if (p->second != mtime)
                        {
                            last_active_cell = active_cell = filename;
                            p->second = mtime;
                            // Stop enumeration; next interval we pick up the rest
                            return false;
                        }
                        return true;
                    }
                );

                // Remove missing files from cell_files
                for (auto it = cell_files.begin(); it != cell_files.end();)
                {
                    if (present_files.find(it->first) == present_files.end())
                        it = cell_files.erase(it);
                    else
                        ++it;
                }

                // If not modified cell file, then do nothing
                if (active_cell.empty())
                    break;

                // ...use the same metadata as the notebook main script, but just execute the given cell
                notebook_cell_script.reset(new active_script_info_t(selected_script));
                work_script = notebook_cell_script.get();
                work_script->file_path = active_cell.c_str();
            }

            //
            // Trigger mode
            // 
           
            // In trigger file mode, just wait for the trigger file to be created
            if (selected_script.trigger_based())
            {
                // The monitor waits until the trigger file is created or modified
                auto trigger_status = selected_script.trigger_file.get_modification_status(true);
                if (trigger_status != filemod_status_e::modified)
                    break;

                // Delete the trigger file
                if (!selected_script.b_keep_trigger_file)
                    qunlink(selected_script.trigger_file.c_str());

                // Always execute the main script even if it was not changed
                selected_script.invalidate();
                // ...and proceed with QScript logic
            }

            // Check if the active script or its dependencies are changed:
            // 1. Dependency file --> repopulate it and execute active script
            // 2. Any dependencies --> reload if needed and //
            // 3. Active script --> execute it again
            auto &dep_scripts = work_script->dep_scripts;

            // Let's check the dependencies index files first
            auto mod_stat = work_script->is_any_dep_index_modified();
            if (mod_stat == filemod_status_e::modified)
            {
                // Force re-parsing of the index file
                dep_scripts.clear();
                set_selected_script(selected_script);

                // Let's invalidate all the scripts time stamps so we ensure they are re-interpreted again
                work_script->invalidate_all_scripts();

                // Refresh the UI
                refresh_chooser(QSCRIPTS_TITLE);

                // Just leave and come back fast so we get a chance to re-evaluate everything
                return 1; // (1 ms)
            }
            // Dependency index file is gone
            else if (mod_stat == filemod_status_e::not_found && !dep_scripts.empty())
            {
                // Let's just check the active script
                dep_scripts.clear();
            }

            //
            // Check the dependency scripts
            //
            bool dep_script_changed = false;
            bool brk = false;
            for (auto &kv: dep_scripts)
            {
                auto &dep_script = kv.second;
                if (dep_script.get_modification_status() == filemod_status_e::modified)
                {
                    qstring err;
                    dep_script_changed = true;
                    if (     dep_script.has_reload_directive()
                         && !execute_reload_directive(dep_script, err, false))
                    {
                        brk = true;
                        break;
                    }
                }
            }
            if (brk)
                break;

            // Check the main script
            mod_stat = work_script->get_modification_status();
            if (mod_stat == filemod_status_e::not_found)
            {
                // Script no longer exists
                msg(
                    "QScripts detected that the active script '%s' no longer exists!\n", 
                    work_script->file_path.c_str());
                clear_selected_script();
                break;
            }

            // Script or its dependencies changed?
            if (dep_script_changed || mod_stat == filemod_status_e::modified)
                execute_script(work_script, opt_with_undo);
        } while (false);
        return opt_change_interval;
    }

protected:
    static constexpr uint32 flags_ =
        CH_KEEP    | CH_RESTORE  | CH_ATTRS   | CH_NOIDB |
        CH_CAN_DEL | CH_CAN_EDIT | CH_CAN_INS | CH_CAN_REFRESH;

    static constexpr int widths_[2]               = { 20, 70 };
    static constexpr const char *const header_[2] = { "Script", "Path" };

    static constexpr const char *ACTION_DEACTIVATE_MONITOR_ID        = "qscripts:deactivatemonitor";
    static constexpr const char *ACTION_EXECUTE_SELECTED_SCRIPT_ID   = "qscripts:execselscript";
    static constexpr const char *ACTION_EXECUTE_SCRIPT_WITH_UNDO_ID  = "qscripts:execscriptwithundo";
    static constexpr const char *ACTION_EXECUTE_NOTEBOOK_ID          = "qscripts:executenotebook";

    scripts_info_t m_scripts;
    ssize_t m_nselected = NO_SELECTION;

    static bool is_correct_widget(action_update_ctx_t* ctx)
    {
        return ctx->widget_title == QSCRIPTS_TITLE;
    }


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
        if (!get_file_modification_time(script_file, &mtime))
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
            "<#Execute a function called '__quick_unload_script' before reloading the script#Execute the u~n~load script function:C>\n"
            "<#The executed scripts' side effects can be reverted with IDA's Undo#Allow QScripts execution to be ~u~ndo-able:C>>\n"

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
                ushort b_with_undo        : 1;
            };
        } chk_opts;
        // Load previous options first (account for multiple instances of IDA)
        saveload_options(false);

        chk_opts.n = 0;
        chk_opts.b_clear_log        = opt_clear_log;
        chk_opts.b_show_filename    = opt_show_filename;
        chk_opts.b_exec_unload_func = opt_exec_unload_func;
        chk_opts.b_with_undo        = opt_with_undo;
        sval_t interval             = opt_change_interval;

        if (ask_form(form, &interval, &chk_opts.n) > 0)
        {
            // Copy values from the dialog
            opt_change_interval  = normalize_filemon_interval(int(interval));
            opt_clear_log        = chk_opts.b_clear_log;
            opt_show_filename    = chk_opts.b_show_filename;
            opt_exec_unload_func = chk_opts.b_exec_unload_func;
            opt_with_undo        = chk_opts.b_with_undo;

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
        auto path = si->file_path.c_str();
        auto name = strrchr(path, DIRCHAR);
        cols->at(0) = name == nullptr ? path : name + 1;
        cols->at(1) = path;
        if (n == m_nselected)
        {
            if (is_monitor_active())
            {
                attrs->flags = CHITEM_BOLD;
                *icon = selected_script.is_notebook() ? IDAICONS::NOTEPAD_1 : IDAICONS::KEYBOARD_GRAY;
            }
            else
            {
                attrs->flags = CHITEM_ITALIC;
                *icon = IDAICONS::RED_DOT;
            }
        }
        else if (is_monitor_active() && selected_script.has_dep(si->file_path) != nullptr)
        {
            // Mark as a dependency
            *icon = IDAICONS::EYE_GLASSES_EDIT;
        }
        else
        {
            // Mark as an inactive file
            *icon = IDAICONS::GRAY_X_CIRCLE;
        }
    }

    // Activate a script and execute it
    cbret_t idaapi enter(size_t n) override
    {
        bool exec_ok = false;

        m_nselected = n;

        // Set as the selected script and execute it
        set_selected_script(m_scripts[n]);

        if (   selected_script.is_notebook() 
            && selected_script.notebook.activation_action == notebook_ctx_t::act_exec_none)
        {
            // Notebook, execute nothing on activation
            msg("Selected notebook: %s\n",
                selected_script.notebook.title.c_str());
        }
        else if (   selected_script.is_notebook()
                 && selected_script.notebook.activation_action == notebook_ctx_t::act_exec_all)
        {
            // Notebook, execute all scripts on activation
            msg("Executing all scripts for notebook: %s\n", 
                selected_script.notebook.title.c_str());

            execute_notebook_cells(&selected_script);
        }
        else
        {
            exec_ok = execute_script(&selected_script, opt_with_undo);
        }

        if (exec_ok)
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
        const char *script_file = ask_file(false, "", "%s", filter.c_str());
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

    void setup_ui()
    {
        am.add_action(
            AMAHF_NONE,
            ACTION_DEACTIVATE_MONITOR_ID,
            "QScripts: Deactivate script monitor",
            "Ctrl-D",
            FO_ACTION_UPDATE([this],
                return this->is_correct_widget(ctx) ? AST_ENABLE_FOR_WIDGET : AST_DISABLE_FOR_WIDGET;
            ),
            FO_ACTION_ACTIVATE([this]) {
                if (this->is_monitor_active())
                {
                    this->clear_selected_script();
                    refresh_chooser(QSCRIPTS_TITLE);
                }
                return 1;
            },
            nullptr,
            IDAICONS::DISABLED);

        am.add_action(
            AMAHF_NONE,
            ACTION_EXECUTE_SELECTED_SCRIPT_ID,
            "QScripts: Execute selected script",
            "Shift-Enter",
            FO_ACTION_UPDATE([this],
                return this->is_correct_widget(ctx) ? AST_ENABLE_FOR_WIDGET : AST_DISABLE_FOR_WIDGET;
            ),
            FO_ACTION_ACTIVATE([this]) {
                if (!ctx->chooser_selection.empty())
                    this->execute_script_at(ctx->chooser_selection.at(0));
                return 1;
            },
            "Execute script without activating it",
            IDAICONS::FLASH);

        am.add_action(
            AMAHF_IDA_POPUP,
            ACTION_EXECUTE_SCRIPT_WITH_UNDO_ID,
            "QScripts: Execute last active script",
            "Alt-Shift-X",
            FO_ACTION_UPDATE([this],
                return AST_ENABLE_ALWAYS;
            ),
            FO_ACTION_ACTIVATE([this]) 
            {
                if (is_monitor_active())
                {
                    script_info_t* work_script = nullptr;
                    std::unique_ptr<active_script_info_t> cell_script;
                    if (selected_script.is_notebook())
                    {
                        if (!selected_script.notebook.last_active_cell.empty())
                        {
                            cell_script.reset(new active_script_info_t(selected_script));
                            if (work_script = cell_script.get())
                                work_script->file_path = selected_script.notebook.last_active_cell.c_str();
                        }
                    }
                    else if (action_active_script != nullptr)
                        work_script = action_active_script;
                    else if (this->has_selected_script())
                        work_script = &selected_script;

                    if (work_script != nullptr)
                        this->execute_script_sync(work_script);
                }
                return 1;
            },
            "An action to programmatically execute the active script",
            IDAICONS::FLASH);

        am.add_action(
            AMAHF_IDA_POPUP,
            ACTION_EXECUTE_NOTEBOOK_ID,
            "QScripts: Execute all notebook cells",
            "",
            FO_ACTION_UPDATE([this],
                return AST_ENABLE_ALWAYS;
            ),
            FO_ACTION_ACTIVATE([this]) 
            {
                if (this->has_selected_script() && selected_script.is_notebook())
                    this->execute_notebook_cells(&selected_script);
                return 1;
            },
            "An action to programmatically execute the active script",
            IDAICONS::NOTEPAD_1);

        hook_event_listener(HT_UI, this);
    }

public:
    static constexpr const char *QSCRIPTS_TITLE = "QScripts";

    qscripts_chooser_t(const char *title_ = QSCRIPTS_TITLE)
        : chooser_t(flags_, qnumber(widths_), widths_, header_, title_), am(this)
    {
        popup_names[POPUP_EDIT] = "~O~ptions";
        setup_ui();
        saveload_options(false);
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

    void execute_last_selected_script(bool with_undo=false)
    {
        if (has_selected_script())
            execute_script(&selected_script, with_undo);
    }

    void execute_script_at(ssize_t n)
    {
        if (n >=0 && n < ssize_t(m_scripts.size()))
            execute_script(&m_scripts[n], opt_with_undo);
    }

    void execute_notebook_cells(active_script_info_t *script)
    {
        auto& cell_files = script->notebook.cell_files;

        active_script_info_t cell_script = *script;
        enumerate_files(
            script->notebook.base_path,
            script->notebook.cells_re,
            [this, &cell_script, &cell_files](const std::string& filename)
            {
                qtime64_t mtime;
                get_file_modification_time(filename, &mtime);
                cell_files[filename] = mtime;
                cell_script.file_path = filename.c_str();
                // Execute script and stop enumeration if one cell fails to execute
                return this->execute_script_sync(&cell_script);
            }
        );
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

    bool install_filemon_timer()
    {
        m_filemon_timer = register_timer(
            opt_change_interval,
            s_filemon_timer_cb,
            this);
        return is_filemon_timer_installed();
    }

    void uninstall_filemon_timer()
    {
        if (is_filemon_timer_installed())
        {
            unregister_timer(m_filemon_timer);
            m_filemon_timer = nullptr;
        }
        activate_monitor(false);
    }

    bool idaapi run(size_t arg) override
    {
        switch (arg)
        {
            // Full UI run
            case 0:
            {
                if (!is_filemon_timer_installed())
                {
                    if (!install_filemon_timer())
                        msg("QScripts: failed to start the file monitor.\n");
                    else
                        msg("QScripts: file monitor started successfully.\n");
                }

                show();
                break;
            }
            // Execute the selected script
            case 1:
            {
                execute_last_selected_script();
                break;
            }
            // Activate the scripts monitor
            case 2:
            {
                activate_monitor(true);
                refresh_chooser(QSCRIPTS_TITLE);
                break;
            }
            // Deactivate the scripts monitor
            case 3:
            {
                activate_monitor(false);
                refresh_chooser(QSCRIPTS_TITLE);
                break;
            }
        }

        return true;
    }

    virtual ~qscripts_chooser_t()
    {
        uninstall_filemon_timer();
    }
};

//-------------------------------------------------------------------------
plugmod_t *idaapi init(void)
{
    auto plg = new qscripts_chooser_t();
    if (!plg->install_filemon_timer())
    {
        if (::debug & IDA_DEBUG_PLUGIN)
            msg("QScripts: failed to install the file monitor on startup. Please invoke the UI once to attempt try again!\n");
    }

    return plg;
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

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,
    init,
    nullptr,
    nullptr,
    "QScripts: Develop IDA scripts faster in your favorite text editor",
    help,
    qscripts_chooser_t::QSCRIPTS_TITLE,
#ifdef _DEBUG
    "Alt-Shift-A"
#else
    "Alt-Shift-F9"
#endif
};
