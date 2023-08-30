"""
Snippet loader/saver for IDA Pro

by Elias Bachaalany / @allthingsida
"""

# TODO:
# - snippetmanager: run snippets in order (optional prefix RE; comes with stock REs); save_combined()
# - - save combined()
# - save_clean() --> delete previous snippets and save
# - stock regular expressions: \d+

import os
from typing import Union
import idaapi, idc

LANG_EXT_MAP = {
    "Python": "py",
    "IDC": "idc",
}

R_LANG_EXT_MAP = {v: k for k, v in LANG_EXT_MAP.items()}

# --------------------------------------------------------------
class snippet_t:
    def __init__(self, 
                 lang: str, 
                 name: str, 
                 body: str, /, 
                 netnode_idx: int = idaapi.BADNODE, 
                 index: int =-1):
        self.lang : str = lang
        self.name : str = name
        self.body : str = body
        self.netnode_idx : int = netnode_idx
        """Underlying netnode index"""
        self.index : int = index
        """Position in the index table"""

    def __repr__(self) -> str:
        return f"snippet_t({self.lang}, {self.name}, {self.body}, {self.netnode_idx}, {self.index})"

    def __str__(self) -> str:
        return f"lang: {self.lang}; title: {self.name}; index: {self.index}"

    def save(self, index: int) -> None:
        """Saves a node and updates the netnode index that was used to save it"""
        node = idaapi.netnode()
        node.create()

        node.supset(1, self.lang)
        node.supset(0, self.name)
        node.setblob(self.body.encode('utf-8'), 0, 'X')

        self.index = index
        self.netnode_idx = node.index()

    @staticmethod
    def from_file(file_name: str) -> Union[None, 'snippet_t']:
        """Constructs a snippet from a file.
        Its slot index and netnode index are not known yet.
        Call save() to save it to the database and get the netnode index
        """
        if not os.path.exists(file_name):
            return None

        basename = os.path.basename(file_name)
        ext = os.path.splitext(basename)[1].lstrip('.').lower()
        if not (lang := R_LANG_EXT_MAP.get(ext)):
            return None

        with open(file_name, 'r') as f:
            body = f.read()

        name = os.path.splitext(basename)[0]
        return snippet_t(lang, name, body)

    @staticmethod
    def from_netnode(netnode_idx: int, slot_idx: int, fast: bool = False):
        node = idaapi.netnode(netnode_idx)
        body = None if fast else node.getblob(0, 'X')
        return snippet_t(node.supstr(1),
                         node.supstr(0),
                         body.decode('utf-8').rstrip('\x00') if body else "",
                         netnode_idx=netnode_idx,
                         index=slot_idx)

# --------------------------------------------------------------
class snippet_manager_t:
    def __init__(self):
        self.index = idaapi.netnode()
        self.index.create("$ scriptsnippets")

    def delete(self, snippet: snippet_t) -> bool:
        if snippet.netnode_idx == idaapi.BADNODE:
            return False

        self.index.altdel(snippet.index)
        snippet.netnode_idx = idaapi.BADNODE
        idaapi.netnode(snippet.netnode_idx).kill()
        return True

    def delete_all(self) -> None:
        snippets = self.retrieve_snippets(fast=True)
        for snip in snippets:
            self.delete(snip)

        self.index.altdel_all()

    def load_from_folder(self, folder : str = '') -> bool:
        """Imports snippets from a folder."""
        # If no input directory is specified, use the default one
        if not folder:
            folder = os.path.join(os.path.dirname(idc.get_idb_path()), '.snippets')
            # For the default directory, create it if it doesn't exist
            if not os.path.exists(folder):
                os.makedirs(folder)

        # For a custom directory, check if it exists
        if not os.path.exists(folder):
            return False

        # Delete previous snippets (and reset index)
        self.delete_all()

        # Enumerate files
        snippets = []
        for file in os.listdir(folder):
            file = os.path.join(folder, file)
            if snippet := snippet_t.from_file(file):
                snippets.append(snippet)

        # Sort by snippet name
        snippets.sort(key=lambda x: x.name)

        for isnippet, snippet in enumerate(snippets):
            snippet.save(isnippet)
            self.index.altset(isnippet, snippet.netnode_idx + 1)

        return True

    def save_to_folder(self, folder: str = '') -> tuple[bool, str]:
        if not folder:
            folder = os.path.join(os.path.dirname(idc.get_idb_path()), '.snippets')

        if not os.path.exists(folder):
            os.makedirs(folder)

        try:
            snippets = self.retrieve_snippets()
            for snip in snippets:
                outfile_name = os.path.join(folder, "%s.%s" % (snip.name, LANG_EXT_MAP[snip.lang]))
                with open(outfile_name, 'w') as outfile:
                    outfile.write(snip.body)
        except Exception as e:
            return (False, f'Failed to save: {e!s}')
        return (True, f'Saved {len(snippets)} snippets to {folder}')


    def retrieve_snippets(self, fast: bool = False) -> list[snippet_t]:
        """Load all snippets from the database"""
        snip_idx = self.index.altfirst()
        if snip_idx == idaapi.BADNODE:
            return []

        snippets = []
        while snip_idx != idaapi.BADNODE:
            snip_node_idx = self.index.altval(snip_idx) - 1
            snippet = snippet_t.from_netnode(snip_node_idx, snip_idx, fast=fast)
            snippets.append(snippet)

            snip_idx = self.index.altnext(snip_idx)

        return snippets


# --------------------------------------------------------------
# Register some functions into the extension part of the IDA API
ext = getattr(idaapi, 'ext', None)
if not ext:
    ext = idaapi.ext = idaapi.object_t()

_sm = snippet_manager_t()

def save_snippets(output_folder: str =''):
    """
    Save snippets to a specified output folder. If no folder is specified, 
    the function uses the current directory by default.

    Args:
    output_folder (str): The folder where the snippets will be saved. 
                         Defaults to current directory if not specified.

    Returns: 
    Result of the operation performed by the save_to_folder method.
    """
    return _sm.save_to_folder(output_folder)

def load_snippets(input_folder: str =''):
    """
    Load snippets from a specified input folder. If no folder is specified, 
    the function uses the current directory by default.

    Args:
    input_folder (str): The folder from where the snippets will be loaded. 
                        Defaults to current directory if not specified.

    Returns: 
    Result of the operation performed by the load_from_folder method.
    """
    return _sm.load_from_folder(input_folder)

def delete_snippets():
    """
    Delete all existing snippets.

    Returns: 
    Result of the operation performed by the delete_all method.
    """
    return _sm.delete_all()

ext.snippets = idaapi.object_t(
    save=save_snippets,
    load=load_snippets,
    delete=delete_snippets,
    man=_sm,
)

# --------------------------------------------------------------
def _test_load(with_body=False):
    idaapi.msg_clear()
    global sm
    sm = _sm
    snippets = sm.retrieve_snippets()
    for isnip, snip in enumerate(snippets):
        print(f"#{isnip:03d} {snip!s}")
        if with_body:
            print("<body>\n%s" % snip.body)
            print("</body>\n------")

# --------------------------------------------------------------
class snippetman_plugmod_t(idaapi.plugmod_t):
    def run(self, _):
        print("""Please use the following methods to:
    - save snippets: idaapi.ext.snippets.save()
    - load snippets: idaapi.ext.snippets.load()
    - delete snippets: idaapi.ext.snippets.delete()
    - access the snippet manager object: idaapi.ext.snippets.man
""")
        return 0

class snippetman_plugin_t(idaapi.plugin_t):
    flags = idaapi.PLUGIN_MULTI
    comment = "Snippet manager plugin"
    help = "Run the plugin to get the full help message"
    wanted_name = "Snippet manager"
    wanted_hotkey = ""

    def init(self):
        return snippetman_plugmod_t()

def PLUGIN_ENTRY() -> idaapi.plugin_t:
    return snippetman_plugin_t()

# --------------------------------------------------------------
if __name__ == "__main__":
    _test_load(False)
