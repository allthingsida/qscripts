import idaapi
import dep

try:
    trigger_ver += 1
except:
    trigger_ver = 1

print(f"version {trigger_ver}: Even if you save this file, this script won't re-execute. Instead, create the trigger file.")

print("----------------------")