# Enumerating functions

if idaapi.get_func_qty() != 0:
    for func_ea in idautils.Functions():
        print(f"Function at 0x{func_ea:#08X}")
else:
    print("No functions found!")