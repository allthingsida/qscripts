# Enumerating segments

if idaapi.get_segm_qty() > 0:
    for seg_ea in idautils.Segments():
        seg = idaapi.getseg(seg_ea)
        print(f"Segment at {seg.start_ea:#08x} - {seg.end_ea:#08x}")

else:
    print("No segments found!")

