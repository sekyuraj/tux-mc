#!/usr/bin/env bash
# Hot methods in a JFR file: self and inclusive share per thread (default the render thread).
#   tools/jfr-top.sh <file.jfr> [thread|*] [stack depth for self]
#   tools/jfr-top.sh <file.jfr> alloc     allocation sites by weight
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")}
f=$1 mode=${2:-Render thread} depth=${3:-1}
if [[ $mode == alloc ]]; then
    "$JAVA_HOME/bin/jfr" print --json --stack-depth 12 --events jdk.ObjectAllocationSample "$f" | python3 -I -c '
import json,sys,collections
ev=json.load(sys.stdin)["recording"]["events"]; w=collections.Counter(); th=collections.Counter(); tot=0
for e in ev:
    v=e["values"]; tot+=v["weight"]
    th[(v["eventThread"] or {}).get("javaName","?").split("-")[0][:28]]+=v["weight"]
    fr=v["stackTrace"]["frames"] if v["stackTrace"] else []
    w[" < ".join(x["method"]["type"]["name"].split("/")[-1]+"."+x["method"]["name"] for x in fr[:3])]+=v["weight"]
print("allocated GB %.1f" % (tot/1e9))
for k,c in th.most_common(8): print("%5.1f%% %s" % (100*c/tot,k))
print("---")
for k,c in w.most_common(30): print("%5.1f%% %s" % (100*c/tot,k))'
    exit
fi
"$JAVA_HOME/bin/jfr" print --json --stack-depth 64 --events jdk.ExecutionSample "$f" | python3 -I -c '
import json,sys,collections
thread=sys.argv[1]; depth=int(sys.argv[2])
ev=json.load(sys.stdin)["recording"]["events"]; s=collections.Counter(); inc=collections.Counter(); n=0
for e in ev:
    t=e["values"]["sampledThread"]["javaName"] or ""
    if thread!="*" and t!=thread: continue
    fr=e["values"]["stackTrace"]["frames"] if e["values"]["stackTrace"] else []
    if not fr: continue
    n+=1
    names=[x["method"]["type"]["name"].split("/")[-1]+"."+x["method"]["name"] for x in fr]
    s[" < ".join(names[:depth])]+=1
    for m in set(names): inc[m]+=1
print("samples",n); print("--- self")
for k,c in s.most_common(30): print("%5.1f%% %s"%(100*c/n,k))
print("--- inclusive")
for k,c in inc.most_common(70): print("%5.1f%% %s"%(100*c/n,k))' "$mode" "$depth"
