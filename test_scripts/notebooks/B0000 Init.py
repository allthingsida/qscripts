import os
import sys
import idaapi
import idautils
import idc

VERSION_B = globals().get('VERSION_B', 0) + 1

print(f"B)Initialized; version: {VERSION_B}")