import os
import sys
import idaapi
import idautils
import idc

VERSION = globals().get('VERSION', 0) + 1

print(f"Initialized; version: {VERSION}")