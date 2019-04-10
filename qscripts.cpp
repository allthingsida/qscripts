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

#include "utils_impl.cpp"

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

static constexpr char UNLOAD_SCRIPT_FUNC_NAME[] = "__quick_unload_script";

// Script execution options
struct script_exec_options_t
{
	int change_interval = 500;
	bool clear_log = false;
	bool show_filename = false;
	bool exec_unload_func = false;
};

// Options version number
static constexpr int OPTIONS_VERSION = 1;

//-------------------------------------------------------------------------
// non-modal call instruction chooser
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

	void invalidate_active_script()
	{
		m_nactive = -1;
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

	// Save or load the scripts and options
	bool saveload_scripts(bool bsave)
	{
		qstring scripts_file;
		scripts_file.sprnt("%s/qscripts.txt", get_user_idadir());

		FILE *fp = qfopen(scripts_file.c_str(), bsave ? "w" : "r");
		if (fp == nullptr)
		{
			msg("Failed to %s the scripts info file '%s'\n", (bsave ? "save" : "load"), scripts_file.c_str());
			return false;
		}

		// Save all the script file names
		if (bsave)
		{
			// Serialize the options
			qfprintf(fp, ";%d|%d|%d|%d|%d\n", 
				OPTIONS_VERSION,                      // 1
				m_options.change_interval, 			  // 2
				m_options.clear_log ? 1 : 0,		  // 3
				m_options.show_filename ? 1 : 0,	  // 4
				m_options.exec_unload_func ? 1 : 0);  // 5

			for (auto &script_info: m_scripts)
				qfprintf(fp, "%s\n", script_info.script_file.c_str());
		}
		// Load all the script file names
		else
		{
			m_scripts.qclear();
			qstring line;

			// First line contains the serialized options
			if (qgetline(&line, fp) != -1)
			{
				if (line.size() > 1 && line[0] == ';')
				{
					char *sptr;
					int icfg = 1;
					for (auto tok = qstrtok(&line[1], "|", &sptr); tok != nullptr; tok = qstrtok(nullptr, "|", &sptr), ++icfg)
					{
						bool *bptr = nullptr;
						// Invalid options?!
						switch (icfg)
						{
							case 1:
								// Invalid configuration, abort
								if (atoi(tok) != OPTIONS_VERSION)
									tok = nullptr;
								break;
							case 2:
								m_options.change_interval = normalize_filemon_interval(atoi(tok));
								break;
							case 3:
								bptr = &m_options.clear_log;
								break;
							case 4:
								bptr = &m_options.show_filename;
								break;
							case 5:
								bptr = &m_options.exec_unload_func;
								break;
						}
						if (bptr != nullptr && tok != nullptr)
							*bptr = atoi(tok) != 0;
					}
				}
				else
				{
					add_script(line.c_str());
				}
			}

			while (qgetline(&line, fp) != -1)
				add_script(line.c_str(), false, false);

			invalidate_active_script();
		}
		qfclose(fp);
		return true;
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
				invalidate_active_script();
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
			"<#Clear the message log before re-running the script#C~l~ear message window before execution:C>\n"
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
		} bool_opts;
		bool_opts.n = 0;
		bool_opts.b_clear_log        = m_options.clear_log        ? 1 : 0;
		bool_opts.b_show_filename    = m_options.show_filename    ? 1 : 0;
		bool_opts.b_exec_unload_func = m_options.exec_unload_func ? 1 : 0;

		if (ask_form(form, &interval, &bool_opts.n) > 0)
		{
			// Copy values from the dialog
			m_options.change_interval  = normalize_filemon_interval(int(interval));
			m_options.clear_log        = bool_opts.b_clear_log        != 0;
			m_options.show_filename    = bool_opts.b_show_filename    != 0;
			m_options.exec_unload_func = bool_opts.b_exec_unload_func != 0;
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
		size_t n) const	override
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
		auto si = add_script(script_file);
		if (si == nullptr)
			return {};
		else
			return cbret_t(si - m_scripts.begin(), chooser_base_t::ALL_CHANGED);
	}

	// Remove a script from the list
	cbret_t idaapi del(size_t n) override
	{
		invalidate_active_script();
		m_scripts.erase(&m_scripts[n]);
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
		// Save the scripts list and options
		saveload_scripts(true);
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
		saveload_scripts(false);

		m_b_filemon_timer_paused = false;
		m_filemon_timer = register_timer(
			m_options.change_interval,
			s_filemon_timer_cb,
			this);

		return true;
	}

	scripts_chooser_t(const char *title_ = "Quick execute script")
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
private:

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

  "Quick execute scripts", // the preferred short name of the plugin
  wanted_hotkey         // the preferred hotkey to run the plugin
};
