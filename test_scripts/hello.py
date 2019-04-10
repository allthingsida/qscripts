try:
    var1 = var1 + 1
except:
    var1 = 0

def __quick_unload_script():
    print("Unloaded: %s" % str(var1))

print("Just edit and save! (%s)" % str(var1))

