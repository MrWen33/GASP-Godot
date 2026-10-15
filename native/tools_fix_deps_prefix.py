# cl.exe (Chinese UI, no English language pack) prints "/showIncludes" lines in GBK, CMake stores the detected prefix as UTF-8:
# rewrite ninja's msvc_deps_prefix with the GBK bytes so header dependencies are tracked.
import re, sys
p = sys.argv[1]
b = open(p, "rb").read()
gbk = "注意: 包含文件:".encode("gbk") + b" "
nb = re.sub(rb"msvc_deps_prefix = [^\r\n]*", lambda m: b"msvc_deps_prefix = " + gbk, b)
if nb != b:
    open(p, "wb").write(nb)
