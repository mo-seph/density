import re
def parse(t):
    tok = re.findall(r'"(?:[^"\\]|\\.)*"|[()]|[^\s()]+', t)
    st=[[]]
    for k in tok:
        if k=="(": st.append([])
        elif k==")":
            x=st.pop(); st[-1].append(x)
        else: st[-1].append(k[1:-1] if k.startswith('"') else k)
    return st[0][0]
def find(n, name): return [c for c in n if isinstance(c, list) and c and c[0]==name]
def nets(path):
    root=parse(open(path).read()); out={}
    for n in find(find(root,"nets")[0],"net"):
        nm=find(n,"name")[0][1]
        out[nm]=[(find(x,"ref")[0][1], find(x,"pin")[0][1]) for x in find(n,"node")]
    return out
