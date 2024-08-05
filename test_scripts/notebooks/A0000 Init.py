import os
import sys
import idaapi
import idautils
import idc

VERSION_A = globals().get('VERSION_A', 0) + 1

print(f"A)Initialized; version: {VERSION_A}")
