#include <idc.idc>

static main()
{
    Message("Welcome to QScripts!\n");
}

static __fast_unload_script()
{
    Message("IDC: aha! unloaded!\n");
}
