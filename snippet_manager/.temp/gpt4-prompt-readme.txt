Please generate a professional README.md file, for the snipper manager IDA Pro plugin.

Here are the functionality that this plugin provides.
Also create a simple (empty/template) section for "INSTALLATION" instructions that I will fill in later:

def save_snippets(output_folder=''):
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

def load_snippets(input_folder=''):
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
)

At the end, give me a download link for the README.md file.
Do not show me the contents, instead directly emit to the file to be downloaded.
Add an "Examples" section illustrating the usage of the exported extension calls.

Basically, to emit the README.md file that I should download, just use the code interpreter to write a whole large string of the contents (as you will generate it).

