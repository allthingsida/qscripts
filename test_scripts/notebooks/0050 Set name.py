# Rename setuff
z = globals().get('z', 0) + 1
idaapi.set_name(idc.here(), f"my_function_{z}")
