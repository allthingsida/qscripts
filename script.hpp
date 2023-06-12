#pragma once

#define QSCRIPTS_LOCAL ".qscripts"
static constexpr char UNLOAD_SCRIPT_FUNC_NAME[] = "__quick_unload_script";

//-------------------------------------------------------------------------
// File modification state
enum class filemod_status_e
{
    not_found,
    not_modified,
    modified
};

// Structure to describe a file and its metadata
struct fileinfo_t
{
    qstring file_path;
    qtime64_t modified_time;

    fileinfo_t(const char* file_path = nullptr): modified_time(0)
    {
        if (file_path != nullptr)
            this->file_path = file_path;
    }

    inline const bool empty() const
    {
        return file_path.empty();
    }

    inline const char* c_str()
    {
        return file_path.c_str();
    }

    bool operator==(const fileinfo_t &rhs) const
    {
        return file_path == rhs.file_path;
    }

    virtual void clear()
    {
        file_path.clear();
        modified_time = 0;
    }

    bool refresh(const char *file_path = nullptr)
    {
        if (file_path != nullptr)
            this->file_path = file_path;

        return get_file_modification_time(this->file_path, &modified_time);
    }

    // Checks if the current script has been modified
    // Optionally updates the time stamp to the latest one if modified
    filemod_status_e get_modification_status(bool update_mtime=true)
    {
        qtime64_t cur_mtime;
        const char *script_file = this->file_path.c_str();
        if (!get_file_modification_time(script_file, &cur_mtime))
        {
            if (update_mtime)
                modified_time = 0;
            return filemod_status_e::not_found;
        }

        // Script is up to date, no need to execute it again
        if (cur_mtime == modified_time)
            return filemod_status_e::not_modified;

        if (update_mtime)
            modified_time = cur_mtime;

        return filemod_status_e::modified;
    }

    void invalidate()
    {
        modified_time = 0;
    }
};

//-------------------------------------------------------------------------
// Dependency script info
struct script_info_t: fileinfo_t
{
    using fileinfo_t::fileinfo_t;

    // Each dependency script can have its own reload command
    qstring reload_cmd;

    // Base path if this dependency is part of a package
    qstring pkg_base;

    const bool has_reload_directive() const { return !reload_cmd.empty(); }
};

// Script files
using scripts_info_t = qvector<script_info_t>;

//-------------------------------------------------------------------------
// Active script information along with its dependencies
struct active_script_info_t: script_info_t
{
    // Trigger file
    fileinfo_t trigger_file;

    // Trigger file options
    bool b_keep_trigger_file;

    // The dependencies index files. First entry is for the main script's deps
    qvector<fileinfo_t> dep_indices;

    // The list of dependency scripts
    std::unordered_map<std::string, script_info_t> dep_scripts;

    // Checks to see if we have a dependency on a given file
    const script_info_t *has_dep(const qstring &dep_file) const
    {
        auto p = dep_scripts.find(dep_file.c_str());
        return p == dep_scripts.end() ? nullptr : &p->second;
    }

    // Is this trigger based or dependency based?
    const bool trigger_based() { return !trigger_file.empty(); }

    // If no dependency index files have been modified, return 0.
    // Return 1 if one of them has been modified or -1 if one of them has gone missing.
    // In both latter cases, we have to recompute our dependencies
    filemod_status_e is_any_dep_index_modified(bool update_mtime = true)
    {
        filemod_status_e r = filemod_status_e::not_modified;
        for (auto &dep_file: dep_indices)
        {
            r = dep_file.get_modification_status(update_mtime);
            if (r != filemod_status_e::not_modified)
                break;
        }
        return r;
    }

    bool add_dep_index(const char *dep_file)
    {
        fileinfo_t fi;
        if (!get_file_modification_time(dep_file, &fi.modified_time))
            return false;

        fi.file_path = dep_file;
        dep_indices.push_back(std::move(fi));
        return true;
    }

    void clear() override
    {
        script_info_t::clear();
        dep_indices.qclear();
        dep_scripts.clear();
        trigger_file.clear();
        b_keep_trigger_file = false;
        reload_cmd.clear();
        pkg_base.clear();
    }

    void invalidate_all_scripts()
    {
        invalidate();

        // Invalidate all but the index file itself
        for (auto &kv: dep_scripts)
            kv.second.invalidate();
    }
};
